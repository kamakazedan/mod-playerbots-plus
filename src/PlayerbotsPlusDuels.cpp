#include "PlayerbotsPlusDuels.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusUtil.h"

#include "DBCStores.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SpellAuraDefines.h"

#include "PlayerbotAI.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr uint32_t kSpellDuel = 7266;       // what a player's Duel does
    constexpr float    kCastRange = 6.0f;       // the duel spell's reach, with room to spare
    constexpr uint32_t kPointId   = 0x44554C;   // MovePoint id of the walk over

    // One of the crowd on its way over to the one it is about to challenge.
    struct Walk
    {
        uint64_t          challenger = 0;
        uint64_t          target     = 0;
        bool              toPlayer   = false;   // the one it is going to is a real player
        std::string       spotKey;
        Clock::time_point started{};
        Clock::time_point nextStep{};
    };
    std::vector<Walk> g_walks;

    // Two that talked themselves into a duel (PlayerbotsPlus::GoDuel), on
    // their way out to the spot, or waiting there for room.
    struct Pact
    {
        uint64_t          a = 0;
        uint64_t          b = 0;
        std::string       spotKey;
        Clock::time_point until{};
    };
    std::vector<Pact> g_pacts;
    std::unordered_map<uint64_t, Clock::time_point> g_cooldown;   // a bot duels again from then
    std::unordered_map<uint64_t, Clock::time_point> g_asked;      // a real player is challenged again from then
    std::unordered_set<std::string> g_noDuels;                    // spots the game allows no duels at: said once
    PbpDuelsStats     g_stats;
    Clock::time_point g_nextCheck{};
    Clock::time_point g_nextStep{};
    Clock::time_point g_nextSweep{};

    bool Roll(uint32_t percent)
    {
        return percent > 0 && urand(1, 100) <= percent;
    }

    size_t Pick(size_t count)
    {
        return count <= 1 ? 0 : size_t(urand(0, uint32_t(count - 1)));
    }

    bool DuelsAllowedIn(uint32_t areaId)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
        return area && (area->flags & AREA_FLAG_ALLOW_DUELS);
    }

    bool Walking(uint64_t guid)
    {
        for (Walk const& w : g_walks)
            if (w.challenger == guid || w.target == guid)
                return true;
        return false;
    }

    bool Pledged(uint64_t guid)
    {
        for (Pact const& p : g_pacts)
            if (p.a == guid || p.b == guid)
                return true;
        return false;
    }

    uint32_t WalksAt(std::string const& spotKey)
    {
        uint32_t n = 0;
        for (Walk const& w : g_walks)
            n += w.spotKey == spotKey;
        return n;
    }

    uint32_t Gap(Player* a, Player* b)
    {
        return a->GetLevel() > b->GetLevel() ? a->GetLevel() - b->GetLevel() : b->GetLevel() - a->GetLevel();
    }

    // On its feet in the world and in nothing else: no duel, fight, trade or
    // invite to answer.
    bool Unbusy(Player* p)
    {
        return p->IsInWorld() && p->IsAlive() && !p->duel && !p->IsInCombat() && !p->GetTradeData() &&
               !p->GetGroupInvite() && !p->IsInFlight() && !p->GetVehicle();
    }

    // Ready to fight: not AFK, its health and mana up, where the game allows
    // a duel.
    bool Fit(Player* bot)
    {
        if (!Unbusy(bot) || bot->GetGroup() || bot->isAFK() || bot->GetLevel() < g_DuelsMinLevel ||
            !sRandomPlayerbotMgr.IsRandomBot(bot))
            return false;
        if (bot->GetHealthPct() < 95.0f)   // playerbots turns a challenge down under 90
            return false;
        if (bot->getPowerType() == POWER_MANA && bot->GetPowerPct(POWER_MANA) < 80.0f)
            return false;
        return DuelsAllowedIn(bot->GetAreaId());
    }

    // One of the crowd at the gates that could duel now: fit, not just out
    // of one, and not come out to fight someone in particular.
    bool Free(Player* bot, Clock::time_point now)
    {
        if (!Fit(bot))
            return false;
        const uint64_t guid = bot->GetGUID().GetRawValue();
        auto cd = g_cooldown.find(guid);
        return (cd == g_cooldown.end() || now >= cd->second) && !Walking(guid) && !Pledged(guid);
    }

    // Off a mount and up off the ground: a duel starts on foot.
    void Ready(Player* bot)
    {
        if (bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
    }

    // The challenge, as the Duel button sends it. Playerbots accepts it on
    // the other's next tick and fights it out; a real player gets the usual
    // window to accept or decline. The hangout sees the duel and lets their
    // own AIs at it.
    bool Challenge(Player* challenger, Player* target, bool toPlayer)
    {
        Ready(challenger);
        challenger->SetFacingToObject(target);
        if (!toPlayer)
        {
            Ready(target);
            target->SetFacingToObject(challenger);
        }
        challenger->CastSpell(target, kSpellDuel, true);
        if (!challenger->duel)
            return false;
        ++g_stats.challenged;
        if (toPlayer)
            ++g_stats.asked;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} (level {}) challenges {} (level {}) to a duel.",
                     challenger->GetName(), challenger->GetLevel(), target->GetName(), target->GetLevel());
        return true;
    }

    // Straight to it when they are close, else a walk over first.
    void Start(Player* challenger, Player* target, bool toPlayer, std::string const& spotKey, Clock::time_point now)
    {
        const Clock::time_point until = now + std::chrono::minutes(g_DuelsBotCooldownMinutes);
        g_cooldown[challenger->GetGUID().GetRawValue()] = until;
        if (!toPlayer)
            g_cooldown[target->GetGUID().GetRawValue()] = until;
        if (challenger->GetDistance(target) <= kCastRange)
        {
            if (!Challenge(challenger, target, toPlayer))
                ++g_stats.gaveUp;
            return;
        }
        Walk w;
        w.challenger = challenger->GetGUID().GetRawValue();
        w.target     = target->GetGUID().GetRawValue();
        w.toPlayer   = toPlayer;
        w.spotKey    = spotKey;
        w.started    = now;
        w.nextStep   = now;
        g_walks.push_back(std::move(w));
    }

    // Twice a second: the walks over. The one walking is held by the
    // hangout, so its own AI does not take it anywhere else.
    void StepWalks(Clock::time_point now)
    {
        if (now < g_nextStep)
            return;
        g_nextStep = now + std::chrono::milliseconds(500);
        for (auto it = g_walks.begin(); it != g_walks.end();)
        {
            Player* a = ObjectAccessor::FindConnectedPlayer(ObjectGuid(it->challenger));
            Player* b = ObjectAccessor::FindConnectedPlayer(ObjectGuid(it->target));
            const bool up = a && b && Unbusy(a) && Unbusy(b) && !a->GetGroup() && (it->toPlayer || !b->GetGroup()) &&
                            a->GetMap() == b->GetMap() && a->GetDistance(b) <= 60.0f;
            bool done = true;
            if (up && a->GetDistance(b) <= kCastRange)
            {
                if (!Challenge(a, b, it->toPlayer))
                    ++g_stats.gaveUp;
            }
            else if (up && now - it->started <= std::chrono::seconds(20))
            {
                done = false;
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
            else
                ++g_stats.gaveUp;
            it = done ? g_walks.erase(it) : std::next(it);
        }
        g_stats.walking = uint32_t(g_walks.size());
    }

    // Twice a second: the pairs that talked themselves into a duel. Once
    // both are standing at the spot, and it has room for one more duel, one
    // goes over to the other and challenges it.
    void KeepPacts(Clock::time_point now)
    {
        for (auto it = g_pacts.begin(); it != g_pacts.end();)
        {
            Player* a = ObjectAccessor::FindConnectedPlayer(ObjectGuid(it->a));
            Player* b = ObjectAccessor::FindConnectedPlayer(ObjectGuid(it->b));
            bool done = false;
            if (!a || !b || now >= it->until)
            {
                ++g_stats.pactsLost;
                done = true;
                if (g_PbpDebug)
                    LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Duels: a pair never got to fight at {}.",
                             it->spotKey);
            }
            else if (PbpHangout_SettledAt(a, it->spotKey) && PbpHangout_SettledAt(b, it->spotKey) && Fit(a) &&
                     Fit(b))
            {
                std::vector<Player*> crowd;
                uint32_t duelling = 0;
                PbpHangout_At(it->spotKey, crowd, duelling);
                if ((duelling + 1) / 2 + WalksAt(it->spotKey) < g_DuelsMaxAtOnce)
                {
                    if (Roll(50))
                        std::swap(a, b);   // either one goes over
                    Start(a, b, false, it->spotKey, now);
                    ++g_stats.pactsHeld;
                    done = true;
                }
            }
            it = done ? g_pacts.erase(it) : std::next(it);
        }
        g_stats.pactsNow = uint32_t(g_pacts.size());
    }

    // A duel spot on `p`'s map near enough to walk out to: as far as one
    // outside the gates takes its crowd from (Hangout.ArrivalRange). The
    // nearest; null when there is none, or the game allows no duels there.
    PbpHangoutSpot const* DuelSpotNear(Player* p)
    {
        const float reach = std::max(g_HangoutArrivalRange, 150.0f) + 50.0f;
        PbpHangoutSpot const* best = nullptr;
        float bestDist = reach;
        for (PbpHangoutSpot const& s : g_HangoutSpots)
        {
            // (Its ground has to be loaded to find them a place there: the
            // crowd at the gates keeps it so.)
            if (!s.capacity || s.map != p->GetMapId() || !DuelSpot(s.key) || g_noDuels.count(s.key) ||
                !p->GetMap()->IsGridLoaded(s.x, s.y))
                continue;
            const float d = p->GetExactDist2d(s.x, s.y);
            if (d <= bestDist)
            {
                bestDist = d;
                best = &s;
            }
        }
        return best;
    }

    // Someone standing about at the gates, not busy, and not asked lately:
    // one of the crowd of their side and about their level challenges them.
    bool AskPlayer(PbpHangoutSpot const& s, std::vector<Player*> const& watching, std::vector<Player*> const& free,
                   Clock::time_point now)
    {
        for (Player* p : watching)
        {
            if (!Unbusy(p) || p->isAFK() || p->IsGameMaster() || p->isMoving() || p->IsMounted() ||
                p->GetExactDist2d(s.x, s.y) > s.radius + 10.0f || p->GetHealthPct() < 90.0f ||
                !DuelsAllowedIn(p->GetAreaId()))
                continue;
            const uint64_t guid = p->GetGUID().GetRawValue();
            auto asked = g_asked.find(guid);
            if (asked != g_asked.end() && now < asked->second)
                continue;
            std::vector<Player*> fit;
            for (Player* bot : free)
                if (bot->GetTeamId() == p->GetTeamId() && Gap(bot, p) <= g_DuelsLevelGap)
                    fit.push_back(bot);
            if (fit.empty())
                continue;
            g_asked[guid] = now + std::chrono::minutes(g_DuelsChallengePlayerMinutes);
            Start(fit[Pick(fit.size())], p, true, s.key, now);
            return true;
        }
        return false;
    }

    // Every Duels.CheckSeconds, at each spot in Duels.Spots someone can see.
    void LookForDuels(Clock::time_point now)
    {
        if (now < g_nextCheck)
            return;
        g_nextCheck = now + std::chrono::seconds(g_DuelsCheckSeconds);

        std::vector<Player*> real;
        for (auto const& [guid, p] : ObjectAccessor::GetPlayers())
            if (p && p->IsInWorld() && PbpIsRealPlayer(p))
                real.push_back(p);

        std::vector<Player*> crowd;
        for (PbpHangoutSpot const& s : g_HangoutSpots)
        {
            if (real.empty())
                break;
            if (!s.capacity || !DuelSpot(s.key))
                continue;
            std::vector<Player*> watching;
            for (Player* p : real)
                if (p->GetMapId() == s.map && p->GetExactDist2d(s.x, s.y) <= g_DuelsWatchRange)
                    watching.push_back(p);
            if (watching.empty())
                continue;   // nobody to see it

            // A spot inside a city: the game allows no duels there.
            Player* someone = watching.front();
            if (!DuelsAllowedIn(someone->GetMap()->GetAreaId(someone->GetPhaseMask(), s.x, s.y, s.z)))
            {
                if (g_noDuels.insert(s.key).second)
                    LOG_ERROR("module.playerbotsplus", "[Playerbots Plus] Duels: the game allows no duels at {} ({}); "
                              "a duel spot goes just outside the city.", s.key, s.name);
                continue;
            }

            uint32_t duelling = 0;
            PbpHangout_At(s.key, crowd, duelling);
            if ((duelling + 1) / 2 + WalksAt(s.key) >= g_DuelsMaxAtOnce || !Roll(g_DuelsChance))
                continue;

            std::vector<Player*> free;
            for (Player* bot : crowd)
                if (Free(bot, now))
                    free.push_back(bot);
            if (free.empty())
                continue;

            if (Roll(g_DuelsChallengePlayerChance) && AskPlayer(s, watching, free, now))
                continue;

            // Any two of a side and about a level: not the same neighbours
            // every time.
            std::vector<std::pair<Player*, Player*>> pairs;
            for (size_t i = 0; i < free.size(); ++i)
                for (size_t j = i + 1; j < free.size(); ++j)
                    if (free[i]->GetTeamId() == free[j]->GetTeamId() && Gap(free[i], free[j]) <= g_DuelsLevelGap)
                        pairs.emplace_back(free[i], free[j]);
            if (pairs.empty())
                continue;
            auto [a, b] = pairs[Pick(pairs.size())];
            if (Roll(50))
                std::swap(a, b);   // either one goes over
            Start(a, b, false, s.key, now);
        }

        if (now >= g_nextSweep)
        {
            g_nextSweep = now + std::chrono::minutes(10);
            for (auto it = g_cooldown.begin(); it != g_cooldown.end();)
                it = now >= it->second ? g_cooldown.erase(it) : std::next(it);
            for (auto it = g_asked.begin(); it != g_asked.end();)
                it = now >= it->second ? g_asked.erase(it) : std::next(it);
        }
    }
}

void PbpDuels_Update(uint32_t /*diff*/)
{
    if (!g_Pbp.enable || !g_DuelsEnable || !g_HangoutEnable)
    {
        g_walks.clear();   // whoever was walking over stays where it got to
        g_pacts.clear();   // ...and whoever was going out to the gates hangs out there
        g_stats.walking  = 0;
        g_stats.pactsNow = 0;
        return;
    }
    const Clock::time_point now = Clock::now();
    if (now >= g_nextStep)
        KeepPacts(now);   // (before the walks: one it starts is walked from the next step)
    StepWalks(now);
    LookForDuels(now);
}

bool PbpDuels_CanGo(Player* a, Player* b, std::string& where)
{
    if (!g_Pbp.enable || !g_DuelsEnable || !g_HangoutEnable || !a || !b || a == b)
        return false;
    const Clock::time_point now = Clock::now();
    for (Player* p : { a, b })
    {
        PlayerbotAI* ai = PlayerbotsMgr::instance().GetPlayerbotAI(p);
        if (!ai || !ai->IsBotAI() || ai->GetMaster() || !sRandomPlayerbotMgr.IsRandomBot(p) ||
            !ai->HasStrategy("new rpg", BOT_STATE_NON_COMBAT))
            return false;
        if (!Unbusy(p) || p->GetGroup() || p->isAFK() || p->GetLevel() < g_DuelsMinLevel || p->IsBeingTeleported())
            return false;
        const uint64_t guid = p->GetGUID().GetRawValue();
        auto cd = g_cooldown.find(guid);
        if ((cd != g_cooldown.end() && now < cd->second) || Walking(guid) || Pledged(guid))
            return false;
    }
    if (a->GetTeamId() != b->GetTeamId() || a->GetMapId() != b->GetMapId() || Gap(a, b) > g_DuelsLevelGap)
        return false;
    PbpHangoutSpot const* spot = DuelSpotNear(a);
    if (!spot || DuelSpotNear(b) != spot)
        return false;
    where = spot->name.empty() ? spot->key : spot->name;
    return true;
}

bool PbpDuels_Go(Player* a, Player* b)
{
    std::string where;
    if (!PbpDuels_CanGo(a, b, where))
        return false;
    PbpHangoutSpot const* spot = DuelSpotNear(a);
    if (!spot)
        return false;
    const std::string key = spot->key;   // (the spot list is the conf's: kept by value past the sends)
    const uint64_t ga = a->GetGUID().GetRawValue(), gb = b->GetGUID().GetRawValue();
    if (!PbpHangout_SendToDuel(a, key) || !PbpHangout_SendToDuel(b, key))
        return false;   // (one sent goes and hangs out there all the same)
    PbpHangout_MakeRoom(key, 2, { ga, gb });
    Pact pact;
    pact.a       = ga;
    pact.b       = gb;
    pact.spotKey = key;
    pact.until   = Clock::now() + std::chrono::seconds(g_HangoutTravelSeconds + 120);
    g_pacts.push_back(std::move(pact));
    ++g_stats.pacts;
    g_stats.pactsNow = uint32_t(g_pacts.size());
    LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Duels: {} (level {}) and {} (level {}) head out to {} to "
             "duel.", a->GetName(), a->GetLevel(), b->GetName(), b->GetLevel(), where);
    return true;
}

PbpDuelsStats PbpDuels_GetStats()
{
    return g_stats;
}
