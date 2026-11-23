#include "PlayerbotsPlusGround.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusCrafting.h"
#include "PlayerbotsPlusDungeonQuests.h"
#include "PlayerbotsPlusDungeons.h"
#include "PlayerbotsPlusGroundGrid.h"
#include "PlayerbotsPlusGroupQuests.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusUtil.h"

#include "CreatureData.h"
#include "DBCStores.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "MoveSpline.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SpellAuraDefines.h"

#include "NewRpgInfo.h"
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"

#include <chrono>
#include <cmath>
#include <deque>
#include <unordered_map>
#include <unordered_set>
#include <variant>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr auto     kSecondLook   = std::chrono::milliseconds(900);   // the second of two looks: no sooner
    constexpr auto     kDropWindow   = std::chrono::minutes(2);
    constexpr size_t   kDropsTooMany = 3;      // brought down this often in kDropWindow: sent off elsewhere
    constexpr float    kWalkFactor   = 2.0f;   // walking, this much further up before it counts as in the air
    constexpr float    kUnder        = 3.0f;   // further below the land than this, with nothing under it: inside it
    constexpr uint32_t kSettleMs     = 300;    // its AI held this long past a fall, to land first

    // A creature's spawn standing for its square of playerbots' grid.
    struct Spawn
    {
        float x = 0.0f, y = 0.0f, z = 0.0f;
        float fromMiddle2 = 0.0f;
    };
    std::unordered_map<uint64_t, Spawn> g_squares;   // by PbpGround::Key

    // Seen up in the air (or inside the ground), since: brought down on the
    // next look if it still is.
    std::unordered_map<uint64_t, Clock::time_point> g_seen;
    std::unordered_map<uint64_t, std::deque<Clock::time_point>> g_drops;   // when each was brought down lately
    std::unordered_map<uint64_t, uint64_t> g_noSpawnFor;   // bot -> the square it was last counted for, with none
    PbpGroundStats g_stats;

    // One of playerbots' own random bots out in the open world: none a real
    // player plays (a self-bot) or leads (its master), none in a dungeon or a
    // battleground (mod-dungeon-clear's, and playerbots').
    bool OursToMind(Player* bot, PlayerbotAI* ai)
    {
        if (!ai || !ai->IsBotAI() || !bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported())
            return false;
        Player* master = ai->GetMaster();
        if (master && (master == bot || PbpIsRealPlayer(master)))
            return false;
        Map* map = bot->GetMap();
        return map && !map->Instanceable();
    }

    // Up off the ground for a reason: flying, swimming, on a flight path, a
    // boat or a vehicle, jumping, falling, knocked back, floating down.
    // Flying is its movement's say, not a flying mount's: one ridden where
    // nobody flies walks. (Nor the falling flag alone: a bot has no client
    // to clear it on landing, so one can be left with it on the ground.)
    bool MayBeUp(Player* bot)
    {
        if (bot->IsInFlight() || bot->GetTransport() || bot->GetVehicle())
            return true;
        if (bot->IsFlying() || bot->CanFly() ||
            bot->HasUnitMovementFlag(MOVEMENTFLAG_CAN_FLY | MOVEMENTFLAG_SWIMMING | MOVEMENTFLAG_HOVER))
            return true;
        if (bot->HasAuraType(SPELL_AURA_FEATHER_FALL) || bot->HasAuraType(SPELL_AURA_HOVER))
            return true;
        if (bot->GetMotionMaster()->GetCurrentMovementGeneratorType() == EFFECT_MOTION_TYPE)
            return true;   // a jump, a knockback, a fall: the game brings it down
        if (bot->movespline && !bot->movespline->Finalized() && bot->movespline->isFalling())
            return true;
        return (bot->GetLiquidData().Status & MAP_LIQUID_STATUS_SWIMMING) != 0;
    }

    // Playerbots' grind spot, if it is a square of its grid: onto the spawn
    // standing for that square.
    void Ground(Player* bot, uint64_t key, NewRpgInfo::GoGrind& grind)
    {
        WorldPosition& pos = grind.pos;
        PbpGround::Cell cell;
        if (!PbpGround::OnGrid(pos.GetPositionX(), pos.GetPositionY(), pos.GetPositionZ(), cell))
            return;
        uint64_t const square = PbpGround::Key(pos.GetMapId(), cell);
        auto const it = g_squares.find(square);
        if (it == g_squares.end())
        {
            if (auto [at, added] = g_noSpawnFor.try_emplace(key, square); added || at->second != square)
            {
                at->second = square;
                ++g_stats.noSpawn;
            }
            return;
        }
        float const off = pos.GetPositionZ() - it->second.z;
        pos.setX(it->second.x);
        pos.setY(it->second.y);
        pos.setZ(it->second.z);
        ++g_stats.spots;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {}'s grind spot in {} was {:.0f} yards {} the ground: "
                     "moved onto a creature's spawn in its square.",
                     bot->GetName(), PbpZoneName(bot), std::fabs(off), off > 0.0f ? "above" : "below");
    }

    // How a bot stands. Up: further above what it would land on than it
    // should be (standing, AirHeight; walking, twice that -- a walk down a
    // slope or off a ledge cuts through the air for a moment). `to`: where it
    // comes down to, the floor or the water on it. Inside: under the land,
    // with nothing at all under it (a cave has a floor); `to`: the land.
    enum class Stand : uint8_t
    {
        Fine,
        Up,
        Inside
    };

    Stand HowItStands(Player* bot, float& to, bool& water)
    {
        float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
        bool const walking = bot->movespline && !bot->movespline->Finalized();
        float const limit = g_GroundAirHeight * (walking ? kWalkFactor : 1.0f);
        water = false;
        // Cheapest first: the floor the game keeps for where it is (the land,
        // a building's, an object's). On it, it is fine -- nearly everyone.
        float const kept = bot->GetFloorZ();
        if (kept > INVALID_HEIGHT && z - kept <= limit)
            return Stand::Fine;
        // Above that, or none: what it would really land on, as a fall finds
        // it (a tree, a rock or a plank bridge counts: it may stand on one).
        float const land = bot->GetMapHeight(x, y, z, true, MAX_FALL_DISTANCE);
        if (land > INVALID_HEIGHT)
        {
            // Water over that is what it comes down to (and what one walking
            // on water stands on).
            LiquidData const& liquid = bot->GetLiquidData();
            water = liquid.Status != LIQUID_MAP_NO_WATER && liquid.Level > land;
            to = water ? liquid.Level : land;
            return z - to > limit ? Stand::Up : Stand::Fine;
        }
        float const top = bot->GetMap()->GetGridHeight(x, y);
        if (top > INVALID_HEIGHT && z < top - kUnder)
        {
            to = top;
            return Stand::Inside;
        }
        return Stand::Fine;
    }

    bool OneOfOurs(Player* bot)
    {
        return PbpHangout_Has(bot) || PbpGroupQuests_Busy(bot) || PbpDungeons_Busy(bot) ||
               PbpDungeonQuests_Walking(bot) || PbpCrafting_Busy(bot);
    }

    // Down, as anyone falls: from where it is to `to`. Its AI waits for the
    // landing, so nothing walks it on from up there.
    void BringDown(Player* bot, PlayerbotAI* ai, uint64_t key, float to, bool water, Clock::time_point now)
    {
        float const x = bot->GetPositionX(), y = bot->GetPositionY(), z = bot->GetPositionZ();
        bool const walking = bot->movespline && !bot->movespline->Finalized();
        bot->StopMoving();
        bot->GetMotionMaster()->Clear();
        if (water)
        {
            // MoveFall comes down onto the bottom, under the water.
            bot->NearTeleportTo(x, y, to, bot->GetOrientation());
            ++g_stats.water;
        }
        else
        {
            bot->GetMotionMaster()->MoveFall();
            // A bot has no client to land it (MSG_MOVE_FALL_LAND): settled
            // here, as playerbots does after a dismount -- with no fall
            // damage, since nothing but a mistake put it up there.
            bot->SetFallInformation(0, to);
            bot->RemoveUnitMovementFlag(MOVEMENTFLAG_FALLING | MOVEMENTFLAG_FALLING_FAR);
        }
        ai->SetNextCheckDelay(PbpGround::FallMs(z - to) + kSettleMs);
        ++g_stats.dropped;
        if (walking)
            ++g_stats.gliding;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} was {} {:.0f} yards up in the air in {} "
                     "({:.1f}, {:.1f}, {:.1f}): brought down.",
                     bot->GetName(), walking ? "walking" : "standing", z - to, PbpZoneName(bot), x, y, z);

        // Again and again: the walk it is on goes nowhere but up. Off to do
        // something else -- but for one of this module's, which keeps its own.
        std::deque<Clock::time_point>& drops = g_drops[key];
        while (!drops.empty() && now - drops.front() >= kDropWindow)
            drops.pop_front();
        drops.push_back(now);
        if (drops.size() >= kDropsTooMany && !OneOfOurs(bot))
        {
            drops.clear();
            ai->rpgInfo.ChangeToIdle();
            ++g_stats.sentOn;
            if (g_PbpDebug)
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} was brought down {} times in {} minutes: sent "
                         "off to do something else.",
                         bot->GetName(), kDropsTooMany,
                         std::chrono::duration_cast<std::chrono::minutes>(kDropWindow).count());
        }
    }

    void BringUp(Player* bot, PlayerbotAI* ai, float top)
    {
        float const z = bot->GetPositionZ();
        bot->StopMoving();
        bot->GetMotionMaster()->Clear();
        bot->NearTeleportTo(bot->GetPositionX(), bot->GetPositionY(), top + 0.5f, bot->GetOrientation());
        ai->SetNextCheckDelay(kSettleMs);
        ++g_stats.raised;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} was {:.0f} yards inside the ground in {}: put back "
                     "on top of it.",
                     bot->GetName(), top - z, PbpZoneName(bot));
    }

    void Forget()
    {
        g_seen.clear();
        g_drops.clear();
        g_noSpawnFor.clear();
    }
}

void PbpGround_Load()
{
    auto const start = Clock::now();
    g_squares.clear();
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        MapEntry const* map = sMapStore.LookupEntry(data.mapid);
        if (!map || map->Instanceable())
            continue;
        // One that walks: a flier's or a swimmer's spawn may be up in the air
        // or under the water.
        CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(data.id);
        if (!t || !t->Movement.IsGroundAllowed() || t->Movement.IsFlightAllowed())
            continue;
        PbpGround::Cell const cell = PbpGround::CellOf(data.posX, data.posY, data.posZ);
        Spawn const spawn{ data.posX, data.posY, data.posZ,
                           PbpGround::FromMiddle2(cell, data.posX, data.posY, data.posZ) };
        auto [it, added] = g_squares.try_emplace(PbpGround::Key(data.mapid, cell), spawn);
        if (!added && spawn.fromMiddle2 < it->second.fromMiddle2)
            it->second = spawn;
    }
    g_stats.squares = uint32_t(g_squares.size());
    g_stats.loadMs  = uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    g_stats.loaded  = true;
    LOG_INFO("server.loading", ">> Playerbots Plus: {} grind squares put on the ground, from the creatures' spawns, "
             "in {} ms", g_stats.squares, g_stats.loadMs);
}

void PbpGround_Update(uint32_t diff)
{
    static uint32_t timer = 0;
    timer += diff;
    if (timer < IN_MILLISECONDS)
        return;
    timer = 0;
    if (!g_Pbp.enable || !g_GroundEnable)
    {
        Forget();
        return;
    }

    auto const now = Clock::now();
    std::unordered_set<uint64_t> up;   // up in the air (or inside the ground) on this look
    for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* bot = it->second;
        PlayerbotAI* ai = bot ? PbpAiOf(bot) : nullptr;
        if (!bot || !OursToMind(bot, ai))
            continue;
        uint64_t const key = bot->GetGUID().GetRawValue();

        if (g_stats.loaded)
            if (auto* grind = std::get_if<NewRpgInfo::GoGrind>(&ai->rpgInfo.data))
                Ground(bot, key, *grind);

        if (MayBeUp(bot))
            continue;
        float to = 0.0f;
        bool water = false;
        Stand const stand = HowItStands(bot, to, water);
        if (stand == Stand::Fine)
            continue;
        up.insert(key);
        auto const [seen, first] = g_seen.try_emplace(key, now);
        if (first || now - seen->second < kSecondLook)
            continue;
        g_seen.erase(seen);
        up.erase(key);
        if (stand == Stand::Up)
            BringDown(bot, ai, key, to, water, now);
        else
            BringUp(bot, ai, to);
    }
    // Back on the ground, or gone: forgotten.
    for (auto it = g_seen.begin(); it != g_seen.end();)
        it = up.contains(it->first) ? std::next(it) : g_seen.erase(it);
    for (auto it = g_drops.begin(); it != g_drops.end();)
        it = !it->second.empty() && now - it->second.back() < kDropWindow ? std::next(it) : g_drops.erase(it);
    if (g_noSpawnFor.size() > 4096)
        g_noSpawnFor.clear();
}

PbpGroundStats PbpGround_GetStats()
{
    return g_stats;
}
