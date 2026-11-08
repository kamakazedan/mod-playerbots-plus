#include "PlayerbotsPlusDuels.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusUtil.h"

#include "CellImpl.h"
#include "DBCStores.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuraDefines.h"

#include "NewRpgInfo.h"
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"

#include <chrono>
#include <list>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr uint32_t kSpellDuel = 7266;       // what a player's Duel does
    constexpr float    kCastRange = 6.0f;       // the duel spell's reach, with room to spare
    constexpr uint32_t kPointId   = 0x44554C;   // MovePoint id of the walk over

    // A bot on its way over to the one it is going to challenge.
    struct Walk
    {
        uint64_t          challenger = 0;
        uint64_t          target     = 0;
        Clock::time_point started{};
        Clock::time_point nextStep{};
    };
    std::vector<Walk> g_walks;
    std::unordered_map<uint64_t, Clock::time_point> g_cooldown;   // a bot duels again from then
    PbpDuelsStats     g_stats;
    Clock::time_point g_nextCheck{};
    Clock::time_point g_nextStep{};
    Clock::time_point g_nextSweep{};

    bool DuelsAllowedWhere(Player* p)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(p->GetAreaId());
        return area && (area->flags & AREA_FLAG_ALLOW_DUELS);
    }

    // Hanging out at a spot (PlayerbotsPlusHangout), and not AFK.
    bool Settled(Player* bot)
    {
        std::string spot, doing;
        bool away = false, justBack = false;
        return PbpHangout_Get(bot, spot, doing, away, justBack) && !away;
    }

    // Nothing on in playerbots' own terms: not questing, grinding or going
    // anywhere in particular.
    bool NothingOn(Player* bot)
    {
        PlayerbotAI* ai = PbpAiOf(bot);
        if (!ai)
            return false;
        auto const& data = ai->rpgInfo.data;
        return std::holds_alternative<NewRpgInfo::Idle>(data) || std::holds_alternative<NewRpgInfo::Rest>(data) ||
               std::holds_alternative<NewRpgInfo::WanderNpc>(data);
    }

    bool Walking(uint64_t guid)
    {
        for (Walk const& w : g_walks)
            if (w.challenger == guid || w.target == guid)
                return true;
        return false;
    }

    // A random bot on its own with time on its hands, in good health,
    // somewhere duels are allowed, that has not duelled lately.
    bool Free(Player* bot, Clock::time_point now)
    {
        if (!bot->IsInWorld() || !PbpIsBotPlayer(bot) || !sRandomPlayerbotMgr.IsRandomBot(bot))
            return false;
        PlayerbotAI* ai = PbpAiOf(bot);
        if (!ai || ai->GetMaster())
            return false;
        if (!bot->IsAlive() || bot->GetLevel() < g_DuelsMinLevel || bot->duel || bot->IsInCombat() ||
            bot->GetGroup() || bot->GetGroupInvite() || bot->GetTradeData() || bot->IsInFlight() ||
            bot->GetVehicle() || bot->isAFK())
            return false;
        if (bot->GetHealthPct() < 95.0f || !DuelsAllowedWhere(bot))   // playerbots turns one down under 90
            return false;
        const uint64_t guid = bot->GetGUID().GetRawValue();
        auto cd = g_cooldown.find(guid);
        if ((cd != g_cooldown.end() && now < cd->second) || Walking(guid))
            return false;
        return Settled(bot) || NothingOn(bot);
    }

    void Hold(Player* bot, uint32_t ms)
    {
        if (PlayerbotAI* ai = PbpAiOf(bot))
            ai->SetNextCheckDelay(ms);
    }

    // Off a mount and up off the ground: a duel starts on foot.
    void Ready(Player* bot)
    {
        if (bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
    }

    // The challenge, as a player's Duel button sends it. Playerbots accepts
    // it on the other's next tick and fights it out.
    bool Challenge(Player* challenger, Player* target)
    {
        Ready(challenger);
        Ready(target);
        challenger->SetFacingToObject(target);
        target->SetFacingToObject(challenger);
        challenger->CastSpell(target, kSpellDuel, true);
        Hold(challenger, 0);
        if (!challenger->duel)
            return false;
        ++g_stats.challenged;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} (level {}) challenges {} (level {}) to a duel.",
                     challenger->GetName(), challenger->GetLevel(), target->GetName(), target->GetLevel());
        return true;
    }

    // Twice a second: the walks over.
    void StepWalks(Clock::time_point now)
    {
        if (now < g_nextStep)
            return;
        g_nextStep = now + std::chrono::milliseconds(500);
        for (auto it = g_walks.begin(); it != g_walks.end();)
        {
            Player* a = ObjectAccessor::FindConnectedPlayer(ObjectGuid(it->challenger));
            Player* b = ObjectAccessor::FindConnectedPlayer(ObjectGuid(it->target));
            bool done = true;
            if (a && b && a->IsInWorld() && b->IsInWorld() && a->GetMap() == b->GetMap() && a->IsAlive() &&
                b->IsAlive() && !a->duel && !b->duel && !a->IsInCombat() && !b->IsInCombat() && !a->GetGroup() &&
                !b->GetGroup() && a->GetDistance(b) <= g_DuelsPairRange * 2.0f)
            {
                if (a->GetDistance(b) <= kCastRange)
                {
                    if (!Challenge(a, b))
                        ++g_stats.gaveUp;
                }
                else if (now - it->started > std::chrono::seconds(20))
                {
                    ++g_stats.gaveUp;
                    Hold(a, 0);
                }
                else
                {
                    done = false;
                    Hold(a, 1500);   // the walk is this, not its own idea of where to be
                    if (now >= it->nextStep)
                    {
                        it->nextStep = now + std::chrono::seconds(2);
                        float x, y, z;
                        b->GetNearPoint(a, x, y, z, a->GetCombatReach(), 3.0f, b->GetAngle(a));
                        if (a->IsSitState())
                            a->SetStandState(UNIT_STAND_STATE_STAND);
                        a->GetMotionMaster()->MovePoint(kPointId, x, y, z);
                    }
                }
            }
            else
            {
                ++g_stats.gaveUp;
                if (a)
                    Hold(a, 0);
            }
            it = done ? g_walks.erase(it) : std::next(it);
        }
        g_stats.walking = uint32_t(g_walks.size());
    }

    // Every Duels.CheckSeconds, near each real player: two free bots of a
    // side and about a level, the closest such pair, Duels.Chance of the time.
    void LookForDuels(Clock::time_point now)
    {
        if (now < g_nextCheck)
            return;
        g_nextCheck = now + std::chrono::seconds(g_DuelsCheckSeconds);

        std::unordered_set<uint64_t> taken;
        for (auto const& [guid, real] : ObjectAccessor::GetPlayers())
        {
            if (!real || !real->IsInWorld() || PbpIsBotPlayer(real))
                continue;
            Map* map = real->GetMap();
            if (!map || map->Instanceable())
                continue;

            std::list<Player*> near;
            Acore::AnyPlayerInObjectRangeCheck check(real, g_DuelsRange, true, true);
            Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(real, near, check);
            Cell::VisitObjects(real, searcher, g_DuelsRange);

            uint32_t duelling = 0, walking = 0;
            std::vector<Player*> free;
            for (Player* p : near)
            {
                if (p == real || !PbpIsBotPlayer(p))
                    continue;
                const uint64_t id = p->GetGUID().GetRawValue();
                if (p->duel)
                    ++duelling;
                else if (Walking(id))
                    ++walking;
                else if (!taken.count(id) && Free(p, now))
                    free.push_back(p);
            }
            if (duelling / 2 + walking >= g_DuelsMaxAtOnce || free.size() < 2 || urand(1, 100) > g_DuelsChance)
                continue;

            Player* first  = nullptr;
            Player* second = nullptr;
            float   best   = g_DuelsPairRange;
            for (size_t i = 0; i < free.size(); ++i)
                for (size_t j = i + 1; j < free.size(); ++j)
                {
                    Player* a = free[i];
                    Player* b = free[j];
                    if (a->GetTeamId() != b->GetTeamId())
                        continue;
                    const uint32_t gap = a->GetLevel() > b->GetLevel() ? a->GetLevel() - b->GetLevel()
                                                                       : b->GetLevel() - a->GetLevel();
                    const float dist = a->GetDistance(b);
                    if (gap > g_DuelsLevelGap || dist > best)
                        continue;
                    // Two settled at a spot only duel from where they are.
                    if (dist > kCastRange && Settled(a) && Settled(b))
                        continue;
                    best   = dist;
                    first  = a;
                    second = b;
                }
            if (!first)
                continue;

            // Whoever is not settled at a spot does the walking.
            if (Settled(first) && !Settled(second))
                std::swap(first, second);
            taken.insert(first->GetGUID().GetRawValue());
            taken.insert(second->GetGUID().GetRawValue());
            const Clock::time_point until = now + std::chrono::minutes(g_DuelsBotCooldownMinutes);
            g_cooldown[first->GetGUID().GetRawValue()]  = until;
            g_cooldown[second->GetGUID().GetRawValue()] = until;

            if (best <= kCastRange)
            {
                if (!Challenge(first, second))
                    ++g_stats.gaveUp;
                continue;
            }
            Walk w;
            w.challenger = first->GetGUID().GetRawValue();
            w.target     = second->GetGUID().GetRawValue();
            w.started    = now;
            w.nextStep   = now;
            g_walks.push_back(w);
        }

        if (now >= g_nextSweep)
        {
            g_nextSweep = now + std::chrono::minutes(10);
            for (auto it = g_cooldown.begin(); it != g_cooldown.end();)
                it = now >= it->second ? g_cooldown.erase(it) : std::next(it);
        }
    }
}

void PbpDuels_Update(uint32_t /*diff*/)
{
    const Clock::time_point now = Clock::now();
    if (!g_DuelsEnable)
    {
        for (Walk const& w : g_walks)
            if (Player* a = ObjectAccessor::FindConnectedPlayer(ObjectGuid(w.challenger)))
                Hold(a, 0);
        g_walks.clear();
        g_stats.walking = 0;
        return;
    }
    StepWalks(now);
    LookForDuels(now);
}

PbpDuelsStats PbpDuels_GetStats()
{
    return g_stats;
}
