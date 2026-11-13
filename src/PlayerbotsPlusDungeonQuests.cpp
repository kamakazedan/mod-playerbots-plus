#include "PlayerbotsPlusDungeonQuests.h"

#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusDungeons.h"
#include "PlayerbotsPlusDungeonsText.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusUtil.h"

#if PBP_BOT_DUNGEONS

#include "Creature.h"
#include "DBCStores.h"
#include "Formulas.h"
#include "GameObject.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestDef.h"
#include "QuestPackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "NewRpgInfo.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "StatsWeightCalculator.h"

#include <algorithm>
#include <chrono>
#include <deque>
#include <random>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <variant>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    // Where someone stands who gives a quest out or takes it back.
    struct Spot
    {
        uint32 map = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        uint32 entry = 0;
        bool go = false;   // a gameobject (a wanted poster, a book) rather than a creature
        uint32 faction = 0;   // a creature's faction template: the other side's are not walked to
    };

    struct DungeonQuest
    {
        uint32 id = 0;
        uint32 map = 0;              // the dungeon it is done in
        int32 level = 0;             // -1: always the player's own
        uint32 minLevel = 0;
        std::vector<uint32> lfg;     // the dungeon finder's dungeons it is done in (a split one's wing)
        std::vector<Spot> enders;    // out in the world
    };

    struct Giver
    {
        Spot spot;
        std::vector<uint32> quests;  // the dungeon quests it gives out
    };

    // The index, built on first use. World thread only, like everything here.
    std::unordered_map<uint32, DungeonQuest> g_quests;
    std::unordered_map<uint32, std::vector<Giver>> g_givers;   // by continent
    bool g_indexed = false;
    uint32 g_giverCount = 0;

    // A dungeon quest done in a wing of a split dungeon is done in that wing's
    // dungeon finder dungeon: the wing of a boss it asks for, or of a boss that
    // drops what it asks for. Every wing when none of that says.
    std::vector<uint32> Wings(Quest const* quest, uint32 map, std::vector<PbpLfgDungeon const*> const& all,
                              std::unordered_map<uint32, std::vector<uint32>> const& droppers)
    {
        std::vector<uint32> every;
        for (PbpLfgDungeon const* d : all)
            every.push_back(d->id);
        if (all.size() < 2)
            return every;

        std::set<uint32> picked;
        auto wingOf = [&](uint32 creature)
        {
            std::string wing;
            uint32 lfgId = 0;
            if (!DcBotRun::WingOf(map, creature, wing, lfgId))
                return;
            for (PbpLfgDungeon const* d : all)
                if (lfgId ? d->id == lfgId : PbpDungeons::SameWing(d->name, wing))
                    picked.insert(d->id);
        };
        for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
            if (quest->RequiredNpcOrGo[i] > 0)
                wingOf(uint32(quest->RequiredNpcOrGo[i]));
        for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            if (uint32 const item = quest->RequiredItemId[i])
                if (auto it = droppers.find(item); it != droppers.end())
                    for (uint32 creature : it->second)
                        wingOf(creature);

        std::vector<uint32> out(picked.begin(), picked.end());
        return out.empty() ? every : out;
    }

    void Index()
    {
        if (g_indexed)
            return;
        g_indexed = true;
        Clock::time_point const start = Clock::now();

        std::unordered_map<uint32, std::vector<PbpLfgDungeon const*>> lfgByMap;
        for (PbpLfgDungeon const& d : PbpDungeons_LfgDungeons())
            lfgByMap[d.map].push_back(&d);

        // Who drops each quest item.
        std::unordered_map<uint32, std::vector<uint32>> droppers;
        if (CreatureQuestItemMap const* items = sObjectMgr->GetCreatureQuestItemMap())
            for (auto const& [creature, list] : *items)
                for (uint32 item : list)
                    droppers[item].push_back(creature);

        for (auto const& [id, quest] : sObjectMgr->GetQuestTemplates())
        {
            if (!quest || quest->GetZoneOrSort() <= 0)
                continue;
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(uint32(quest->GetZoneOrSort()));
            if (!area)
                continue;
            auto const lfgs = lfgByMap.find(area->mapid);
            if (lfgs == lfgByMap.end())
                continue;   // not done in a normal dungeon the dungeon finder has
            switch (quest->GetType())
            {
                case QUEST_TYPE_HEROIC:
                case QUEST_TYPE_RAID:
                case QUEST_TYPE_RAID_10:
                case QUEST_TYPE_RAID_25:
                case QUEST_TYPE_PVP:
                    continue;
                default:
                    break;
            }
            if (quest->IsRepeatable() || quest->IsDaily() || quest->IsWeekly() || quest->IsMonthly() ||
                quest->IsSeasonal() || quest->IsDFQuest())
                continue;
            DungeonQuest q;
            q.id = id;
            q.map = area->mapid;
            q.level = quest->GetQuestLevel();
            q.minLevel = quest->GetMinLevel();
            q.lfg = Wings(quest, area->mapid, lfgs->second, droppers);
            g_quests.emplace(id, std::move(q));
        }

        // Who gives them out and takes them back: creatures and objects.
        std::unordered_map<uint32, std::vector<uint32>> gives, takes, goGives, goTakes;
        auto collect = [](QuestRelations const* relations, std::unordered_map<uint32, std::vector<uint32>>& out)
        {
            if (relations)
                for (auto const& [entry, quest] : *relations)
                    if (g_quests.contains(quest))
                        out[entry].push_back(quest);
        };
        collect(sObjectMgr->GetCreatureQuestRelationMap(), gives);
        collect(sObjectMgr->GetCreatureQuestInvolvedRelationMap(), takes);
        collect(sObjectMgr->GetGOQuestRelationMap(), goGives);
        collect(sObjectMgr->GetGOQuestInvolvedRelationMap(), goTakes);

        // ...and where they stand, out in the world, in the normal phase.
        auto place = [](Spot const& spot, std::unordered_map<uint32, std::vector<uint32>> const& give,
                        std::unordered_map<uint32, std::vector<uint32>> const& take)
        {
            if (auto it = give.find(spot.entry); it != give.end())
            {
                g_givers[spot.map].push_back({ spot, it->second });
                ++g_giverCount;
            }
            if (auto it = take.find(spot.entry); it != take.end())
                for (uint32 quest : it->second)
                    g_quests[quest].enders.push_back(spot);
        };
        auto outdoors = [](uint32 map, uint32 phaseMask)
        {
            MapEntry const* entry = sMapStore.LookupEntry(map);
            return entry && !entry->Instanceable() && (phaseMask & 1);
        };
        for (auto const& [spawn, data] : sObjectMgr->GetAllCreatureData())
        {
            if (!outdoors(data.mapid, data.phaseMask))
                continue;
            for (uint32 entry : { data.id, data.id2, data.id3 })
                if (entry && (gives.contains(entry) || takes.contains(entry)))
                {
                    CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(entry);
                    place({ data.mapid, data.posX, data.posY, data.posZ, entry, false, t ? t->faction : 0u }, gives,
                          takes);
                }
        }
        for (auto const& [spawn, data] : sObjectMgr->GetAllGOData())
            if (outdoors(data.mapid, data.phaseMask) && (goGives.contains(data.id) || goTakes.contains(data.id)))
                place({ data.mapid, data.posX, data.posY, data.posZ, data.id, true, 0u }, goGives, goTakes);

        LOG_INFO("module.playerbotsplus",
                 "[Playerbots Plus] Bot dungeons: {} dungeon quests known, given out at {} places ({} ms).",
                 g_quests.size(), g_giverCount,
                 std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    }

    // ---- what each bot is up to --------------------------------------------------------

    enum class WalkKind : uint8
    {
        Take,
        HandIn
    };

    struct Walk
    {
        WalkKind kind = WalkKind::Take;
        Spot spot;
        std::vector<uint32> quests;
        Clock::time_point since;
        Clock::time_point arrived;   // HandIn: got there (playerbots' questing gets the first go)
        bool there = false;
        uint32 nudges = 0;
    };

    std::unordered_map<uint64, Walk> g_walks;
    std::unordered_map<uint64, Clock::time_point> g_lookedAt;
    std::unordered_map<uint64, std::unordered_map<uint32, uint32>> g_tries;   // runs at each quest
    std::unordered_map<uint64, std::unordered_set<uint32>> g_gaveUp;          // not taken again
    std::unordered_map<uint64, std::unordered_set<uint64>> g_unreached;       // spots a walk to never got to
    std::unordered_set<uint64> g_holders;                                     // hold a dungeon quest
    std::deque<Clock::time_point> g_recentWalks;
    PbpDungeonQuestsStats g_stats;
    uint32 g_walkMs = 0, g_scanMs = 0, g_lookMs = 0;

    uint32 FreeSlots(Player* bot)
    {
        uint32 free = 0;
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            if (!bot->GetQuestSlotQuestId(slot))
                ++free;
        return free;
    }

    // Let a quest go, as a player does from the quest log.
    void Drop(Player* bot, uint32 questId)
    {
        uint16 const slot = bot->FindQuestSlot(questId);
        if (slot >= MAX_QUEST_LOG_SIZE)
            return;
        WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
        packet << uint8(slot);
        WorldPackets::Quest::QuestLogRemoveQuest removeQuest(std::move(packet));
        removeQuest.Read();
        bot->GetSession()->HandleQuestLogRemoveQuest(removeQuest);
        if (PlayerbotAI* ai = PbpAiOf(bot))
            ai->lowPriorityQuest.erase(questId);
    }

    // A spot, as a key: where it is, to the yard.
    uint64 SpotKey(Spot const& spot)
    {
        return (uint64(spot.map) << 48) ^ (uint64(uint32(int32(spot.x)) & 0xFFFFFF) << 24) ^
               uint64(uint32(int32(spot.y)) & 0xFFFFFF);
    }

    // Someone of the other side (or who would attack it): not one to walk to.
    bool Hostile(Player* bot, Spot const& spot)
    {
        if (spot.go || !spot.faction)
            return false;
        FactionTemplateEntry const* theirs = sFactionTemplateStore.LookupEntry(spot.faction);
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        return theirs && mine && theirs->IsHostileTo(*mine);
    }

    // Somewhere a walk never got to (out of reach, or always something else on
    // the way): not walked to again.
    bool Unreached(Player* bot, Spot const& spot)
    {
        auto it = g_unreached.find(bot->GetGUID().GetRawValue());
        return it != g_unreached.end() && it->second.contains(SpotKey(spot));
    }

    uint32 GreyFor(Player* bot)
    {
        return Acore::XP::GetGrayLevel(uint8(bot->GetLevel()));
    }

    // One it may take now: not taken or done, of its level, not grey, for a
    // dungeon it has not outgrown, not one it gave up on, and one the game lets
    // it take (prerequisites, class, race, reputation...).
    bool Takeable(Player* bot, DungeonQuest const& q, uint32 level, uint32 grey)
    {
        if (q.minLevel > level || PbpDungeons::Grey(q.level, grey))
            return false;
        if (bot->GetQuestStatus(q.id) != QUEST_STATUS_NONE)
            return false;
        if (auto it = g_gaveUp.find(bot->GetGUID().GetRawValue()); it != g_gaveUp.end() && it->second.contains(q.id))
            return false;
        bool fits = false;
        for (uint32 lfg : q.lfg)
            if (PbpLfgDungeon const* d = PbpDungeons_LfgDungeon(lfg); d && level <= d->maxLevel)
                fits = true;
        if (!fits)
            return false;
        Quest const* quest = sObjectMgr->GetQuestTemplate(q.id);
        return quest && bot->CanTakeQuest(quest, false);
    }

    // On its own, out in the world, nothing on, and playerbots' questing idle:
    // free to walk somewhere for a quest.
    bool Idle(Player* bot, PlayerbotAI* ai)
    {
        if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->GetGroup() ||
            bot->IsBeingTeleported() || bot->IsInFlight() || bot->GetTransport() || bot->GetVehicle() || bot->duel ||
            bot->InBattleground() || bot->InBattlegroundQueue())
            return false;
        Map* map = bot->GetMap();
        if (!map || map->Instanceable())
            return false;
        if (!ai || !ai->IsBotAI() || ai->GetMaster() || !ai->HasStrategy("new rpg", BOT_STATE_NON_COMBAT))
            return false;
        if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE || PbpHangout_Has(bot) || PbpDungeons_Busy(bot))
            return false;
        auto const& data = ai->rpgInfo.data;
        if (!std::holds_alternative<NewRpgInfo::Idle>(data) && !std::holds_alternative<NewRpgInfo::Rest>(data) &&
            !std::holds_alternative<NewRpgInfo::WanderNpc>(data) &&
            !std::holds_alternative<NewRpgInfo::WanderRandom>(data) &&
            !std::holds_alternative<NewRpgInfo::GoGrind>(data))
            return false;
        return sRandomPlayerbotMgr.IsRandomBot(bot);
    }

    bool GoingTo(PlayerbotAI* ai, Spot const& spot)
    {
        auto const* camp = std::get_if<NewRpgInfo::GoCamp>(&ai->rpgInfo.data);
        return camp && camp->pos.GetMapId() == spot.map && camp->pos.GetExactDist(spot.x, spot.y, spot.z) < 2.0f;
    }

    void Send(Player* bot, PlayerbotAI* ai, Walk walk, Clock::time_point now)
    {
        walk.since = now;
        ai->rpgInfo.ChangeToGoCamp(WorldPosition(walk.spot.map, walk.spot.x, walk.spot.y, walk.spot.z));
        g_walks[bot->GetGUID().GetRawValue()] = std::move(walk);
        g_recentWalks.push_back(now);
    }

    // A dungeon quest done: off to whoever takes it back, the nearest on this
    // continent within twice the range a giver is walked to.
    bool StartHandIn(Player* bot, PlayerbotAI* ai, Clock::time_point now)
    {
        float const range = g_Pbp.dungeons.questRange * 2.0f;
        Spot const* best = nullptr;
        uint32 bestQuest = 0;
        float bestDist = 0.0f;
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 const id = bot->GetQuestSlotQuestId(slot);
            auto it = id ? g_quests.find(id) : g_quests.end();
            if (it == g_quests.end() || bot->GetQuestStatus(id) != QUEST_STATUS_COMPLETE)
                continue;
            for (Spot const& spot : it->second.enders)
            {
                if (spot.map != bot->GetMapId() || Hostile(bot, spot) || Unreached(bot, spot))
                    continue;
                float const dist = bot->GetExactDist2d(spot.x, spot.y);
                if (dist <= range && (!best || dist < bestDist))
                {
                    best = &spot;
                    bestQuest = id;
                    bestDist = dist;
                }
            }
        }
        if (!best)
            return false;
        Walk w;
        w.kind = WalkKind::HandIn;
        w.spot = *best;
        w.quests = { bestQuest };
        ai->lowPriorityQuest.erase(bestQuest);
        Send(bot, ai, std::move(w), now);
        ++g_stats.handIns;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: {} goes to hand in quest {} ({} yd).",
                     bot->GetName(), bestQuest, uint32(bestDist));
        return true;
    }

    // Quests to take: the nearest giver on this continent, within range, with
    // one for it.
    bool StartTake(Player* bot, PlayerbotAI* ai, Clock::time_point now)
    {
        if (FreeSlots(bot) <= g_Pbp.dungeons.keepLogFree)
            return false;
        auto const givers = g_givers.find(bot->GetMapId());
        if (givers == g_givers.end())
            return false;
        uint32 const level = bot->GetLevel();
        uint32 const grey = GreyFor(bot);
        float const range = g_Pbp.dungeons.questRange;
        Giver const* best = nullptr;
        std::vector<uint32> bestQuests;
        float bestDist = 0.0f;
        for (Giver const& g : givers->second)
        {
            float const dist = bot->GetExactDist2d(g.spot.x, g.spot.y);
            if (dist > range || (best && dist >= bestDist) || Hostile(bot, g.spot) || Unreached(bot, g.spot))
                continue;
            std::vector<uint32> quests;
            for (uint32 id : g.quests)
                if (auto it = g_quests.find(id); it != g_quests.end() && Takeable(bot, it->second, level, grey))
                    quests.push_back(id);
            if (quests.empty())
                continue;
            best = &g;
            bestQuests = std::move(quests);
            bestDist = dist;
        }
        if (!best)
            return false;
        Walk w;
        w.kind = WalkKind::Take;
        w.spot = best->spot;
        w.quests = std::move(bestQuests);
        Send(bot, ai, std::move(w), now);
        ++g_stats.sent;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus",
                     "[Playerbots Plus] Bot dungeons: {} goes to take {} dungeon quest(s) ({} yd).", bot->GetName(),
                     g_walks[bot->GetGUID().GetRawValue()].quests.size(), uint32(bestDist));
        return true;
    }

    Object* Find(Player* bot, Spot const& spot)
    {
        if (spot.go)
            return bot->FindNearestGameObject(spot.entry, 30.0f);
        Creature* creature = bot->FindNearestCreature(spot.entry, 30.0f, true);
        return creature && !creature->IsHostileTo(bot) ? creature : nullptr;
    }

    // At the giver: what it may still take, it takes.
    void TakeAt(Player* bot, PlayerbotAI* ai, Walk const& w)
    {
        Object* giver = Find(bot, w.spot);
        if (!giver)
            return;
        uint32 const level = bot->GetLevel();
        uint32 const grey = GreyFor(bot);
        for (uint32 id : w.quests)
        {
            if (FreeSlots(bot) <= g_Pbp.dungeons.keepLogFree)
                break;
            auto it = g_quests.find(id);
            Quest const* quest = sObjectMgr->GetQuestTemplate(id);
            if (it == g_quests.end() || !quest || !Takeable(bot, it->second, level, grey) ||
                !bot->CanAddQuest(quest, false))
                continue;
            bot->AddQuestAndCheckCompletion(quest, giver);
            QuestStatus const status = bot->GetQuestStatus(id);
            if (status == QUEST_STATUS_NONE)
                continue;
            if (status == QUEST_STATUS_INCOMPLETE)
                ai->lowPriorityQuest.insert(id);
            ++g_stats.taken;
            g_holders.insert(bot->GetGUID().GetRawValue());
            LOG_DEBUG("module.playerbotsplus", "[Playerbots Plus] {} took dungeon quest {} ({}).", bot->GetName(), id,
                      quest->GetTitle());
        }
    }

    // The reward that suits it best, by playerbots' own reckoning of an item for
    // it (the index of the choice; 0 with one or none to choose from).
    uint32 BestReward(Player* bot, Quest const* quest)
    {
        if (quest->GetRewChoiceItemsCount() <= 1)
            return 0;
        StatsWeightCalculator calc(bot);
        uint32 best = 0;
        float bestScore = -1.0f;
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            uint32 const item = quest->RewardChoiceItemId[i];
            if (!item)
                continue;
            float const score = calc.CalculateItem(item);
            if (score > bestScore)
            {
                best = i;
                bestScore = score;
            }
        }
        return best;
    }

    // At the one who takes it back. Playerbots' own questing (which talks to
    // anyone near with something to hand in) gets a minute; then it is handed
    // in here. True when the walk is over.
    bool HandInAt(Player* bot, Walk& w, Clock::time_point now)
    {
        uint32 const id = w.quests.front();
        QuestStatus const status = bot->GetQuestStatus(id);
        if (status != QUEST_STATUS_COMPLETE)
        {
            if (status == QUEST_STATUS_REWARDED || bot->GetQuestRewardStatus(id))
                ++g_stats.handedIn;
            return true;
        }
        if (!w.there)
        {
            w.there = true;
            w.arrived = now;
            return false;
        }
        if (now - w.arrived < std::chrono::minutes(1))
            return false;
        Object* ender = Find(bot, w.spot);
        Quest const* quest = sObjectMgr->GetQuestTemplate(id);
        if (!ender || !quest)
            return true;
        uint32 const choice = BestReward(bot, quest);
        if (bot->CanRewardQuest(quest, choice, false))
        {
            bot->RewardQuest(quest, choice, ender);
            ++g_stats.handedIn;
            LOG_DEBUG("module.playerbotsplus", "[Playerbots Plus] {} handed in dungeon quest {} ({}).", bot->GetName(),
                      id, quest->GetTitle());
        }
        return true;
    }

    void EndWalk(Player* bot, PlayerbotAI* ai, Walk const& w)
    {
        // Off its walk: playerbots finds it something else to do.
        if (bot && ai && GoingTo(ai, w.spot))
            ai->rpgInfo.ChangeToIdle();
    }

    void Walks(Clock::time_point now)
    {
        uint32 const walkMinutes = g_Pbp.dungeons.questWalkMinutes;
        for (auto it = g_walks.begin(); it != g_walks.end();)
        {
            Walk& w = it->second;
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(it->first));
            PlayerbotAI* ai = bot ? PbpAiOf(bot) : nullptr;
            bool const lost = !bot || !bot->IsInWorld() || !ai || bot->GetGroup() || ai->GetMaster() ||
                              bot->GetMapId() != w.spot.map || PbpHangout_Has(bot) ||
                              sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE;
            if (lost || now - w.since >= std::chrono::minutes(walkMinutes))
            {
                ++g_stats.walksLost;
                if (!lost)
                    g_unreached[it->first].insert(SpotKey(w.spot));
                EndWalk(bot, ai, w);
                it = g_walks.erase(it);
                continue;
            }
            if (!bot->IsAlive() || bot->IsInCombat())
            {
                ++it;
                continue;
            }
            // There: at the spot, or near enough that playerbots called it
            // there (its "go camp" ends within ten yards, and it goes on to
            // talk to whoever is about).
            float const dist = bot->GetExactDist(w.spot.x, w.spot.y, w.spot.z);
            bool const going = GoingTo(ai, w.spot);
            if (w.there || dist <= 12.0f || (!going && dist <= 30.0f))
            {
                bool done = true;
                if (w.kind == WalkKind::Take)
                    TakeAt(bot, ai, w);
                else
                    done = HandInAt(bot, w, now);
                if (done)
                {
                    it = g_walks.erase(it);
                    continue;
                }
            }
            else if (!going)
            {
                // Playerbots gave it something else to do on the way: back on
                // its way, a few times.
                if (++w.nudges > 5)
                {
                    ++g_stats.walksLost;
                    g_unreached[it->first].insert(SpotKey(w.spot));
                    it = g_walks.erase(it);
                    continue;
                }
                ai->rpgInfo.ChangeToGoCamp(WorldPosition(w.spot.map, w.spot.x, w.spot.y, w.spot.z));
            }
            ++it;
        }
    }

    // Room in a full log for the dungeon quests it holds.
    void KeepRoom(Player* bot, PlayerbotAI* ai)
    {
        uint32 const keepFree = g_Pbp.dungeons.keepLogFree;
        if (FreeSlots(bot) >= keepFree)
            return;
        uint32 doing = 0;
        if (auto const* quest = std::get_if<NewRpgInfo::DoQuest>(&ai->rpgInfo.data))
            doing = quest->questId;
        uint32 const grey = GreyFor(bot);
        int32 const zone = int32(bot->GetZoneId());
        std::vector<PbpDungeons::LogEntry> log;
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 const id = bot->GetQuestSlotQuestId(slot);
            if (!id)
                continue;
            Quest const* quest = sObjectMgr->GetQuestTemplate(id);
            QuestStatus const status = bot->GetQuestStatus(id);
            PbpDungeons::LogEntry e;
            e.quest = id;
            e.keep = !quest || g_quests.contains(id) || status == QUEST_STATUS_COMPLETE || id == doing;
            e.failed = status == QUEST_STATUS_FAILED;
            e.grey = quest && PbpDungeons::Grey(quest->GetQuestLevel(), grey);
            e.otherZone = quest && quest->GetZoneOrSort() > 0 && quest->GetZoneOrSort() != zone;
            e.level = quest ? bot->GetQuestLevel(quest) : 0;
            log.push_back(e);
        }
        for (uint32 id : PbpDungeons::ToDrop(log, MAX_QUEST_LOG_SIZE, keepFree))
        {
            Drop(bot, id);
            ++g_stats.made;
        }
    }

    // Every bot holding a dungeon quest: marked so playerbots' questing does
    // not go off to do it alone (and unmarked once done, so it may go and hand
    // it in), and grey ones let go once out of their dungeon.
    void Scan()
    {
        g_holders.clear();
        for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
        {
            Player* bot = it->second;
            PlayerbotAI* ai = bot ? PbpAiOf(bot) : nullptr;
            if (!bot || !ai || !bot->IsInWorld())
                continue;
            bool held = false;
            bool const out = !bot->GetGroup() && bot->GetMap() && !bot->GetMap()->Instanceable();
            uint32 const grey = GreyFor(bot);
            for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 const id = bot->GetQuestSlotQuestId(slot);
                auto const q = id ? g_quests.find(id) : g_quests.end();
                if (q == g_quests.end())
                    continue;
                QuestStatus const status = bot->GetQuestStatus(id);
                if (status == QUEST_STATUS_INCOMPLETE && out && PbpDungeons::Grey(q->second.level, grey))
                {
                    Drop(bot, id);
                    ++g_stats.greyGone;
                    LOG_DEBUG("module.playerbotsplus", "[Playerbots Plus] {} let dungeon quest {} go: grey.",
                              bot->GetName(), id);
                    continue;
                }
                held = true;
                if (status == QUEST_STATUS_INCOMPLETE)
                    ai->lowPriorityQuest.insert(id);
                else
                    ai->lowPriorityQuest.erase(id);
            }
            if (held)
                g_holders.insert(bot->GetGUID().GetRawValue());
        }
    }

    void KeepAll()
    {
        for (uint64 raw : g_holders)
            if (Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(raw)))
                if (PlayerbotAI* ai = PbpAiOf(bot); ai && bot->IsInWorld())
                    KeepRoom(bot, ai);
    }

    // A few idle bots are looked at for dungeon quests to hand in or take.
    void Look(Clock::time_point now)
    {
        PbpBotDungeonSettings const& s = g_Pbp.dungeons;
        while (!g_recentWalks.empty() && now - g_recentWalks.front() >= std::chrono::minutes(1))
            g_recentWalks.pop_front();
        if (g_recentWalks.size() >= s.questWalksPerMinute)
            return;

        std::vector<Player*> idle;
        for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
        {
            Player* bot = it->second;
            if (!bot || bot->GetLevel() < 10 || g_walks.contains(bot->GetGUID().GetRawValue()))
                continue;
            auto const looked = g_lookedAt.find(bot->GetGUID().GetRawValue());
            if (looked != g_lookedAt.end() && now - looked->second < std::chrono::minutes(s.questLookMinutes))
                continue;
            if (Idle(bot, PbpAiOf(bot)))
                idle.push_back(bot);
        }
        std::shuffle(idle.begin(), idle.end(), Rng());

        uint32 looked = 0;
        for (Player* bot : idle)
        {
            if (g_recentWalks.size() >= s.questWalksPerMinute || ++looked > 100)
                break;
            g_lookedAt[bot->GetGUID().GetRawValue()] = now;
            PlayerbotAI* ai = PbpAiOf(bot);
            if (!StartHandIn(bot, ai, now))
                StartTake(bot, ai, now);
        }

        for (auto it = g_lookedAt.begin(); it != g_lookedAt.end();)
            it = now - it->second >= std::chrono::minutes(s.questLookMinutes) ? g_lookedAt.erase(it) : std::next(it);
    }
}

std::vector<PbpDungeonQuestHold> PbpDungeonQuests_Holds(Player* bot)
{
    Index();
    std::vector<PbpDungeonQuestHold> out;
    if (!bot)
        return out;
    uint32 const level = bot->GetLevel();
    uint32 const grey = GreyFor(bot);
    uint32 const runs = g_Pbp.dungeons.questRuns;
    auto const tries = g_tries.find(bot->GetGUID().GetRawValue());
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const id = bot->GetQuestSlotQuestId(slot);
        auto const it = id ? g_quests.find(id) : g_quests.end();
        if (it == g_quests.end() || bot->GetQuestStatus(id) != QUEST_STATUS_INCOMPLETE)
            continue;
        DungeonQuest const& q = it->second;
        if (PbpDungeons::Grey(q.level, grey))
            continue;
        if (tries != g_tries.end())
            if (auto t = tries->second.find(id); t != tries->second.end() && t->second >= runs)
                continue;
        for (uint32 lfg : q.lfg)
        {
            PbpLfgDungeon const* d = PbpDungeons_LfgDungeon(lfg);
            if (!d || level < d->minLevel || level > d->maxLevel)
                continue;
            auto hold = std::find_if(out.begin(), out.end(),
                                     [lfg](PbpDungeonQuestHold const& h) { return h.lfgDungeon == lfg; });
            if (hold == out.end())
                out.push_back({ lfg, 1 });
            else
                ++hold->quests;
        }
    }
    return out;
}

bool PbpDungeonQuests_ToHandIn(Player* bot)
{
    Index();
    if (!bot)
        return false;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const id = bot->GetQuestSlotQuestId(slot);
        auto const it = id ? g_quests.find(id) : g_quests.end();
        if (it == g_quests.end() || bot->GetQuestStatus(id) != QUEST_STATUS_COMPLETE)
            continue;
        for (Spot const& spot : it->second.enders)
            if (spot.map == bot->GetMapId())
                return true;
    }
    return false;
}

std::vector<uint32_t> PbpDungeonQuests_ForMap(Player* bot, uint32_t mapId)
{
    Index();
    std::vector<uint32_t> out;
    if (!bot)
        return out;
    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        uint32 const id = bot->GetQuestSlotQuestId(slot);
        auto const it = id ? g_quests.find(id) : g_quests.end();
        if (it != g_quests.end() && it->second.map == mapId)
            out.push_back(id);
    }
    return out;
}

void PbpDungeonQuests_RunEnded(Player* bot, uint32_t lfgDungeon, std::vector<uint32_t> const& held)
{
    if (!bot)
        return;
    uint64 const key = bot->GetGUID().GetRawValue();
    // Back out of a dungeon: looked at for quests to hand in as soon as it is idle.
    g_lookedAt.erase(key);
    for (uint32 id : held)
    {
        auto const it = g_quests.find(id);
        if (it == g_quests.end() || bot->GetQuestStatus(id) != QUEST_STATUS_INCOMPLETE)
            continue;   // done (or no longer held)
        std::vector<uint32> const& wings = it->second.lfg;
        if (std::find(wings.begin(), wings.end(), lfgDungeon) == wings.end())
            continue;   // a run of another wing is no try at it
        uint32& tries = g_tries[key][id];
        if (++tries < g_Pbp.dungeons.questRuns)
            continue;
        Drop(bot, id);
        g_gaveUp[key].insert(id);
        g_tries[key].erase(id);
        ++g_stats.triesGone;
        LOG_INFO("module.playerbotsplus",
                 "[Playerbots Plus] Bot dungeons: {} let quest {} go after {} runs without finishing it.",
                 bot->GetName(), id, g_Pbp.dungeons.questRuns);
    }
}

bool PbpDungeonQuests_Walking(Player* bot)
{
    return bot && g_walks.contains(bot->GetGUID().GetRawValue());
}

void PbpDungeonQuests_Update(uint32_t diff)
{
    Index();
    Clock::time_point const now = Clock::now();

    g_walkMs += diff;
    if (g_walkMs >= 1000)
    {
        g_walkMs = 0;
        Walks(now);
        KeepAll();
    }

    g_scanMs += diff;
    if (g_scanMs >= 10000)
    {
        g_scanMs = 0;
        Scan();
    }

    g_lookMs += diff;
    if (g_lookMs >= 10000)
    {
        g_lookMs = 0;
        Look(now);
    }
}

void PbpDungeonQuests_Stop()
{
    for (auto const& [raw, w] : g_walks)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(raw));
        EndWalk(bot, bot ? PbpAiOf(bot) : nullptr, w);
    }
    g_walks.clear();
    g_recentWalks.clear();
}

PbpDungeonQuestsStats PbpDungeonQuests_GetStats()
{
    PbpDungeonQuestsStats out = g_stats;
    out.quests = uint32(g_quests.size());
    out.givers = g_giverCount;
    out.walking = uint32(g_walks.size());
    return out;
}

#else // no mod-dungeon-clear: no bot dungeons to fetch quests for

std::vector<PbpDungeonQuestHold> PbpDungeonQuests_Holds(Player*)
{
    return {};
}

bool PbpDungeonQuests_ToHandIn(Player*)
{
    return false;
}

std::vector<uint32_t> PbpDungeonQuests_ForMap(Player*, uint32_t)
{
    return {};
}

void PbpDungeonQuests_RunEnded(Player*, uint32_t, std::vector<uint32_t> const&) { }

bool PbpDungeonQuests_Walking(Player*)
{
    return false;
}

void PbpDungeonQuests_Update(uint32_t) { }

void PbpDungeonQuests_Stop() { }

PbpDungeonQuestsStats PbpDungeonQuests_GetStats()
{
    return {};
}

#endif
