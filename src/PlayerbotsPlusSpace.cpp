#include "PlayerbotsPlusSpace.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusFire.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusUtil.h"

#include "CellImpl.h"
#include "Creature.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"

#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <list>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr uint32_t kPointId = 0x535043;   // MovePoint id for a step aside
    constexpr float    kTwoPi   = 6.2831853f;

    struct Still
    {
        uint32_t          map = 0;
        float             x = 0.0f, y = 0.0f, z = 0.0f;
        Clock::time_point since{};      // standing here since
        Clock::time_point nextMove{};   // may step aside again from then
    };
    std::unordered_map<uint64_t, Still> g_still;   // bots near a real player, by guid
    PbpSpaceStats g_stats;

    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    float Frand(float lo, float hi)
    {
        return std::uniform_real_distribution<float>(lo, hi)(Rng());
    }

    // Everyone alive within `range` of a point: players and creatures.
    struct NearPoint
    {
        float x, y, z, range;
        bool operator()(Unit* u) const { return u->IsInWorld() && u->IsAlive() && u->GetExactDist(x, y, z) < range; }
    };

    void Around(Map* map, WorldObject const* phaseRef, float x, float y, float z, float range, std::list<Unit*>& out)
    {
        NearPoint check{ x, y, z, range };
        Acore::UnitListSearcher<NearPoint> searcher(phaseRef, out, check);
        Cell::VisitObjects(x, y, map, searcher, range);
    }

    // How close to `u` is in its way: `room`, or a big NPC's own size and a
    // bit. 0 for anyone whose space does not count: someone passing by, a
    // pet, a totem, a summon, a hidden trigger, a critter, a GM nobody sees,
    // someone on a flight or a boat.
    float RoomFor(Unit* u, float room)
    {
        if (u->isMoving())
            return 0.0f;
        if (Creature* c = u->ToCreature())
        {
            if (c->IsPet() || c->IsTotem() || c->IsGuardian() || c->IsSummon() || !c->GetOwnerGUID().IsEmpty() ||
                c->IsTrigger() || c->GetCreatureType() == CREATURE_TYPE_CRITTER ||
                c->HasUnitFlag(UNIT_FLAG_NOT_SELECTABLE) || !c->IsVisible())
                return 0.0f;
            return std::max(room, c->GetCombatReach() + 0.5f);
        }
        if (Player* p = u->ToPlayer())
        {
            if ((p->IsGameMaster() && !p->isGMVisible()) || p->IsInFlight() || p->GetTransport())
                return 0.0f;
            return room;
        }
        return 0.0f;
    }

    // A random bot nothing else is steering, and nothing would mind moving.
    bool OursToMove(Player* bot, PlayerbotAI* ai)
    {
        if (!bot->IsAlive() || bot->IsBeingTeleported() || !ai->IsBotAI() || !sRandomPlayerbotMgr.IsRandomBot(bot))
            return false;
        if (bot->IsInCombat() || bot->duel || bot->GetGroup() || bot->GetTradeData() || bot->IsInFlight() ||
            bot->GetVehicle() || bot->GetTransport() || bot->isMoving() || bot->IsNonMeleeSpellCast(false) ||
            bot->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING))
            return false;
        switch (bot->getStandState())
        {
            case UNIT_STAND_STATE_SIT_CHAIR:
            case UNIT_STAND_STATE_SIT_LOW_CHAIR:
            case UNIT_STAND_STATE_SIT_MEDIUM_CHAIR:
            case UNIT_STAND_STATE_SIT_HIGH_CHAIR:
                return false;   // on a bench or a chair: that seat is its place
            default:
                break;
        }
        Player* master = ai->GetMaster();
        return !master || master == bot || !PbpIsRealPlayer(master);
    }

    struct Point
    {
        float x = 0.0f, y = 0.0f;
    };

    // A step or two away from whoever it stands in (away from `fromX`,
    // `fromY`, turning further round when that way is a wall, a drop or
    // someone else): on the same floor, indoors or out the way it is now, in
    // sight, walkable, out of any fire, and out of everyone's way -- the
    // places others are stepping to this tick (`claimed`) included.
    bool StepAside(Player* bot, float fromX, float fromY, std::vector<Point> const& claimed, float& outX, float& outY,
                   float& outZ)
    {
        Map* map = bot->GetMap();
        const uint32 phase = bot->GetPhaseMask();
        const float bx = bot->GetPositionX(), by = bot->GetPositionY(), bz = bot->GetPositionZ();
        PositionFullTerrainStatus here;
        map->GetFullTerrainStatusForPosition(phase, bx, by, bz, bot->GetCollisionHeight(), here);
        const float away = std::hypot(bx - fromX, by - fromY) > 0.2f ? std::atan2(by - fromY, bx - fromX)
                                                                       : Frand(0.0f, kTwoPi);
        static const float kTurns[] = { 0.0f, 0.6f, -0.6f, 1.2f, -1.2f, 1.8f, -1.8f, 2.5f, -2.5f, 3.1f };
        for (float turn : kTurns)
        {
            const float angle = away + turn + Frand(-0.2f, 0.2f);
            const float dist  = Frand(g_SpaceStepMin, g_SpaceStepMax);
            const float x = bx + dist * std::cos(angle);
            const float y = by + dist * std::sin(angle);
            const float z = map->GetHeight(phase, x, y, bz + 2.0f, true, 6.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - bz) > 1.5f)
                continue;
            bool taken = false;
            for (Point const& p : claimed)
                if (std::hypot(p.x - x, p.y - y) < g_SpaceDistance + 0.5f)
                    taken = true;
            if (taken || map->IsInWater(phase, x, y, z, 2.0f))
                continue;
            if (!map->isInLineOfSight(bx, by, bz + 1.5f, x, y, z + 1.5f, phase, LINEOFSIGHT_ALL_CHECKS,
                                      VMAP::ModelIgnoreFlags::Nothing))
                continue;
            PositionFullTerrainStatus there;
            map->GetFullTerrainStatusForPosition(phase, x, y, z, bot->GetCollisionHeight(), there);
            if (there.outdoors != here.outdoors)
                continue;   // not into a building, nor out of one
            if (PbpSpace_Crowded(map, bot, x, y, z, g_SpaceDistance + 0.5f, bot) || PbpFire_Burns(bot, x, y, z, 0.5f))
                continue;
            PathGenerator path(bot);
            path.CalculatePath(x, y, z, false);
            const uint32 type = path.GetPathType();
            if (!(type & PATHFIND_NORMAL) ||
                (type & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE | PATHFIND_SHORTCUT | PATHFIND_NOT_USING_PATH)))
                continue;
            G3D::Vector3 const& end = path.GetActualEndPosition();
            if (std::hypot(end.x - x, end.y - y) > 1.0f || std::fabs(end.z - z) > 0.5f)
                continue;
            outX = x;
            outY = y;
            outZ = z;
            return true;
        }
        return false;
    }
}

bool PbpSpace_Crowded(Map* map, WorldObject const* phaseRef, float x, float y, float z, float room,
                      WorldObject const* except)
{
    if (!map || !phaseRef)
        return false;
    std::list<Unit*> near;
    Around(map, phaseRef, x, y, z, room + 4.0f, near);
    for (Unit* u : near)
    {
        if (u == except)
            continue;
        const float need = RoomFor(u, room);
        if (need > 0.0f && u->GetExactDist(x, y, z) < need)
            return true;
    }
    return false;
}

void PbpSpace_Update(uint32_t diff)
{
    static uint32_t timer = 0;
    timer += diff;
    if (timer < g_SpaceCheckSeconds * IN_MILLISECONDS)
        return;
    timer = 0;
    if (!g_SpaceEnable || !g_Pbp.enable)
    {
        g_still.clear();
        return;
    }

    // Only what someone could see: bots near a real player.
    std::vector<Player*> real;
    for (auto const& [guid, p] : ObjectAccessor::GetPlayers())
        if (p && p->IsInWorld() && PbpIsRealPlayer(p))
            real.push_back(p);
    if (real.empty())
    {
        g_still.clear();
        return;
    }

    const auto now = Clock::now();
    const float rangeSq = g_SpaceRange * g_SpaceRange;
    struct Candidate
    {
        Player*         bot;
        PlayerbotAI*    ai;
        Clock::duration still;
    };
    std::vector<Candidate> candidates;
    std::unordered_set<uint64_t> looked;
    for (auto const& [guid, bot] : ObjectAccessor::GetPlayers())
    {
        if (!bot || !bot->IsInWorld() || PbpIsRealPlayer(bot))
            continue;
        bool seen = g_SpaceRange <= 0.0f;
        for (Player* r : real)
            if (!seen && r->GetMapId() == bot->GetMapId() &&
                r->GetExactDist2dSq(bot->GetPositionX(), bot->GetPositionY()) < rangeSq)
                seen = true;
        if (!seen)
            continue;
        const uint64_t key = guid.GetRawValue();
        looked.insert(key);
        Still& s = g_still[key];
        const float moved = std::hypot(bot->GetPositionX() - s.x, bot->GetPositionY() - s.y) +
                            std::fabs(bot->GetPositionZ() - s.z);
        if (s.since == Clock::time_point{} || s.map != bot->GetMapId() || moved > 0.4f)
        {
            s.map   = bot->GetMapId();
            s.x     = bot->GetPositionX();
            s.y     = bot->GetPositionY();
            s.z     = bot->GetPositionZ();
            s.since = now;
            continue;
        }
        if (now - s.since < std::chrono::seconds(g_SpaceStillSeconds) || now < s.nextMove)
            continue;
        PlayerbotAI* ai = PbpAiOf(bot);
        if (ai && OursToMove(bot, ai))
            candidates.push_back({ bot, ai, now - s.since });
    }
    for (auto it = g_still.begin(); it != g_still.end();)
        it = looked.count(it->first) ? std::next(it) : g_still.erase(it);

    // Of two on one spot, the one that got there last steps aside: newcomers first.
    std::sort(candidates.begin(), candidates.end(),
              [](Candidate const& a, Candidate const& b) { return a.still < b.still; });
    std::unordered_set<WorldObject const*> leaving;
    std::vector<Point> claimed;
    for (Candidate const& c : candidates)
    {
        Player* bot = c.bot;
        std::list<Unit*> near;
        Around(bot->GetMap(), bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
               g_SpaceDistance + 4.0f, near);
        float sx = 0.0f, sy = 0.0f;
        uint32_t in = 0;
        for (Unit* u : near)
        {
            if (u == bot || leaving.count(u))
                continue;
            const float room = RoomFor(u, g_SpaceDistance);
            if (room > 0.0f && bot->GetExactDist(u) < room)
            {
                sx += u->GetPositionX();
                sy += u->GetPositionY();
                ++in;
            }
        }
        if (!in)
            continue;
        sx /= float(in);
        sy /= float(in);
        ++g_stats.crowded;

        Still& s = g_still[bot->GetGUID().GetRawValue()];
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!StepAside(bot, sx, sy, claimed, x, y, z))
        {
            ++g_stats.nowhere;
            s.nextMove = now + std::chrono::seconds(10);
            continue;
        }

        const PbpHangoutMove hangout = PbpHangout_MoveTo(bot, x, y, z);
        if (hangout == PbpHangoutMove::OnItsWay)
            continue;
        if (hangout == PbpHangoutMove::Moved)
            ++g_stats.moved;
        else
        {
            // Turned back to whoever it was standing in, once there, the way
            // people make room and carry on facing each other. (MovePoint
            // turns it only for an angle above 0.)
            float face = std::atan2(sy - y, sx - x);
            if (face <= 0.0f)
                face += kTwoPi;
            if (bot->IsSitState())
                bot->SetStandState(UNIT_STAND_STATE_STAND);
            bot->GetMotionMaster()->MovePoint(kPointId, x, y, z, FORCED_MOVEMENT_NONE, 0.0f, face);
            c.ai->SetNextCheckDelay(2500);   // its own AI lets it get there first
        }
        leaving.insert(bot);
        claimed.push_back({ x, y });
        s.nextMove = now + std::chrono::seconds(g_SpaceCooldownSeconds);
        ++g_stats.stepped;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} steps aside from {} standing in its way at {} "
                     "{:.1f} {:.1f}{}.", bot->GetName(), in, bot->GetMapId(), bot->GetPositionX(),
                     bot->GetPositionY(), hangout == PbpHangoutMove::Moved ? ", to a new place at its hangout" : "");
    }
}

PbpSpaceStats PbpSpace_GetStats()
{
    return g_stats;
}
