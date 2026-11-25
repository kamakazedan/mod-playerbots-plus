#include "PlayerbotsPlusRolls.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusLooted.h"
#include "PlayerbotsPlusUtil.h"

#include "Group.h"
#include "ItemTemplate.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "ItemUsageValue.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"

#include <fmt/core.h>

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

namespace
{
    // A roll that offers Disenchant, as it went to a bot.
    struct Start
    {
        uint64_t bot      = 0;
        uint64_t roll     = 0;   // the roll's own guid
        uint32_t item     = 0;
        uint32_t suffix   = 0;
        int32_t  property = 0;
    };
    std::mutex            g_startMutex;
    std::vector<Start>    g_starts;    // under g_startMutex
    constexpr size_t      kMaxStarts = 4096;
    std::atomic<uint64_t> g_dropped{ 0 };
    std::atomic<uint64_t> g_won{ 0 };

    PbpRollsStats g_stats;   // world thread
}

void PbpRollsPacketScript::OnPacketSent(WorldSession* session, WorldPacket const& packet)
{
    // Every packet to every session comes through here: the opcode first.
    const uint16 op = packet.GetOpcode();
    if (op != SMSG_LOOT_START_ROLL && op != SMSG_LOOT_ROLL_WON)
        return;
    if (!session || !session->IsHeadless())
        return;   // a bot's own copy only
    Player* player = session->GetPlayer();
    if (!player)
        return;

    if (op == SMSG_LOOT_ROLL_WON)
    {
        // The roll (u64), slot, item, suffix, property (u32 each), the winner
        // (u64), its number and its vote (u8 each): Group::SendLootRollWon.
        // The winner's own copy goes out on the thread that gives it the
        // dust, straight before it does.
        if (packet.size() >= 34 && packet.read<uint8>(33) == ROLL_DISENCHANT &&
            packet.read<uint64>(24) == player->GetGUID().GetRawValue())
        {
            PbpLooted_DisenchantWon(player);
            g_won.fetch_add(1, std::memory_order_relaxed);
        }
        return;
    }

    // The roll (u64), map, slot, item, suffix, property, count, countdown
    // (u32 each), the votes offered (u8): Group::SendLootStartRollToPlayer.
    if (!g_Pbp.enable || !g_Pbp.rolls.disenchant || packet.size() < 37 ||
        !(packet.read<uint8>(36) & ROLL_FLAG_TYPE_DISENCHANT))
        return;
    Start s;
    s.bot      = player->GetGUID().GetRawValue();
    s.roll     = packet.read<uint64>(0);
    s.item     = packet.read<uint32>(16);
    s.suffix   = packet.read<uint32>(20);
    s.property = int32_t(packet.read<uint32>(24));
    std::lock_guard<std::mutex> lock(g_startMutex);
    if (g_starts.size() < kMaxStarts)
        g_starts.push_back(s);
    else
        g_dropped.fetch_add(1, std::memory_order_relaxed);
}

void PbpRolls_Update()
{
    std::vector<Start> starts;
    {
        std::lock_guard<std::mutex> lock(g_startMutex);
        if (g_starts.empty())
            return;
        starts.swap(g_starts);
    }
    PbpRollSettings const& cfg = g_Pbp.rolls;
    // Only where playerbots would have a bot roll Greed for it anyway, and
    // Disenchant where it knows to.
    if (!g_Pbp.enable || !cfg.disenchant || !sPlayerbotAIConfig.LootRollDisenchant ||
        !sPlayerbotAIConfig.LootGreedRollLevel)
        return;

    for (Start const& s : starts)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(s.bot));
        Group* group = bot ? bot->GetGroup() : nullptr;
        if (!group || !bot->HasSkill(SKILL_ENCHANTING))
            continue;
        PlayerbotAI* ai = PbpAiOf(bot);
        if (!ai || !ai->IsBotAI() || !ai->GetAiObjectContext())
            continue;
        ++g_stats.offered;
        // Playerbots passes on everything under these.
        if (group->GetLootMethod() == MASTER_LOOT || group->GetLootMethod() == FREE_FOR_ALL)
            continue;
        ItemTemplate const* p = sObjectMgr->GetItemTemplate(s.item);
        if (!p || !p->DisenchantID || (p->Class != ITEM_CLASS_WEAPON && p->Class != ITEM_CLASS_ARMOR))
            continue;
        if (p->RequiredDisenchantSkill > bot->GetSkillValue(SKILL_ENCHANTING))
        {
            ++g_stats.unskilled;
            continue;
        }
        if (p->Bonding != BIND_WHEN_PICKED_UP && p->Quality > cfg.disenchantBoeMaxQuality)
        {
            ++g_stats.whole;
            continue;
        }

        // The roll, still open and still offering it, and its vote not cast.
        Roll const* roll = nullptr;
        for (Roll const* r : group->GetRolls())
            if (r && r->itemGUID.GetRawValue() == s.roll && r->isValid())
            {
                roll = r;
                break;
            }
        if (!roll || !(roll->rollVoteMask & ROLL_FLAG_TYPE_DISENCHANT))
        {
            ++g_stats.late;
            continue;
        }
        auto vote = roll->playerVote.find(bot->GetGUID());
        if (vote == roll->playerVote.end() || vote->second != NOT_EMITED_YET)
        {
            ++g_stats.late;
            continue;
        }

        // Only what playerbots would sell, which it rolls Greed for (asked
        // as it asks): a piece it would wear, or keep for anything else, is
        // its own to roll for.
        std::string qualifier = std::to_string(s.item);
        if (const int32_t random = s.property ? s.property : -int32_t(s.suffix))
            qualifier += "," + std::to_string(random);
        ItemUsageValue value(ai);
        value.Qualify(qualifier);
        const ItemUsage usage = value.Calculate();
        if (usage != ITEM_USAGE_AH && usage != ITEM_USAGE_VENDOR && usage != ITEM_USAGE_DISENCHANT)
        {
            ++g_stats.wears;
            continue;
        }

        // The game's own vote. The roll may be settled by it, and gone.
        group->CountRollVote(bot->GetGUID(), ObjectGuid(s.roll), ROLL_DISENCHANT);
        ++g_stats.voted;
    }
}

PbpRollsStats PbpRolls_GetStats()
{
    PbpRollsStats s = g_stats;
    s.won     = g_won.load(std::memory_order_relaxed);
    s.dropped = g_dropped.load(std::memory_order_relaxed);
    return s;
}

PbpBrief PbpRolls_Brief(bool full)
{
    PbpBrief b;
    PbpRollsStats const s = PbpRolls_GetStats();
    const bool on = g_Pbp.enable && g_Pbp.rolls.disenchant && sPlayerbotAIConfig.LootRollDisenchant &&
                    sPlayerbotAIConfig.LootGreedRollLevel;
    b.lines.push_back(fmt::format("Disenchant rolls: {} -- {} rolls offering it went to an enchanter; Disenchant "
                                  "cast for {}; {} Disenchant rolls won (the dust its winner's own).",
                                  on ? "on" : (g_Pbp.rolls.disenchant ? "OFF (playerbots' LootRollDisenchant or "
                                                                        "LootGreedRollLevel is off)"
                                                                      : "OFF"),
                                  s.offered, s.voted, s.won));
    if (full)
        b.lines.push_back(fmt::format("Left alone: {} it would wear or keep the piece, {} not enough Enchanting for "
                                      "it, {} worth more whole (binds when equipped, above "
                                      "Rolls.DisenchantBoEMaxQuality), {} it had voted already or the roll was over. "
                                      "{} notes dropped.",
                                      s.wears, s.unskilled, s.whole, s.late, s.dropped));
    if (s.dropped)
        b.issues.push_back(fmt::format("Disenchant rolls: {} rolls were not looked at, too many at once.", s.dropped));
    return b;
}
