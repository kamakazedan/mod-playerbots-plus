#include "PlayerbotsPlusIndoors.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusUtil.h"

#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "PlayerbotAI.h"

#include <chrono>
#include <unordered_map>
#include <unordered_set>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr float kAloft      = 3.0f;     // further above the floor than this is in the air
    constexpr float kRidingWith = 100.0f;   // a player this close on a mount is ridden along with

    std::unordered_map<uint64_t, Clock::time_point> g_since;   // bot -> riding indoors since
    PbpIndoorsStats g_stats;

    bool InTravelForm(Player* bot)
    {
        switch (bot->GetShapeshiftForm())
        {
            case FORM_TRAVEL:
            case FORM_FLIGHT:
            case FORM_FLIGHT_EPIC:
                return true;
            default:
                return false;
        }
    }

    // A bot on a mount under a roof that should be on foot there: on the
    // ground in the open world, nothing else going on, nobody's own
    // character, and not riding along with its player.
    bool ShouldWalk(Player* bot)
    {
        if (!bot->IsAlive() || bot->IsInCombat() || bot->IsBeingTeleported() || bot->IsInFlight() ||
            bot->GetVehicle() || bot->GetTransport() || bot->GetMap()->Instanceable())
            return false;
        PlayerbotAI* ai = PbpAiOf(bot);
        if (!ai || !ai->IsBotAI())
            return false;
        Player* master = ai->GetMaster();
        if (master == bot)
            return false;   // a self-bot: someone's own character
        if (master && PbpIsRealPlayer(master) && (master->IsMounted() || InTravelForm(master)) &&
            bot->IsWithinDistInMap(master, kRidingWith))
            return false;   // its player rode in too
        if (bot->IsOutdoors())
            return false;
        return bot->GetPositionZ() - bot->GetFloorZ() < kAloft;
    }

    void GetOff(Player* bot)
    {
        if (bot->IsMounted())
            bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        else
            bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
        // A bot has no client to answer the "can't fly now": what playerbots'
        // own dismount does, so it is not left flagged as flying.
        if (!bot->HasIncreaseMountedFlightSpeedAura() && !bot->HasFlyAura() &&
            bot->HasUnitMovementFlag(MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY))
        {
            bot->RemoveUnitMovementFlag(MOVEMENTFLAG_FLYING | MOVEMENTFLAG_DISABLE_GRAVITY | MOVEMENTFLAG_CAN_FLY);
            if (!bot->IsRooted())
                bot->SendMovementFlagUpdate();
        }
    }
}

void PbpIndoors_Update(uint32_t diff)
{
    static uint32_t timer = 0;
    timer += diff;
    if (timer < IN_MILLISECONDS)
        return;
    timer = 0;
    if (!g_Pbp.enable || !g_IndoorsEnable)
    {
        g_since.clear();
        return;
    }

    const auto now = Clock::now();
    std::unordered_set<uint64_t> still;   // riding indoors and left on, this time round
    for (auto const& [guid, bot] : ObjectAccessor::GetPlayers())
    {
        // Cheapest first: riding at all, then a bot, then under a roof.
        if (!bot || !bot->IsInWorld() || (!bot->IsMounted() && !InTravelForm(bot)) || PbpIsRealPlayer(bot) ||
            !ShouldWalk(bot))
            continue;
        const uint64_t key = guid.GetRawValue();
        auto const it = g_since.emplace(key, now).first;
        if (now - it->second < std::chrono::seconds(g_IndoorsSeconds) || bot->isMoving())
        {
            still.insert(key);
            continue;
        }
        const char* what = bot->IsMounted() ? "gets off its mount" : "comes out of travel form";
        GetOff(bot);
        ++g_stats.dismounted;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} {} indoors in {}.", bot->GetName(), what,
                     PbpZoneName(bot));
    }
    // Got off, outside again, or gone: forgotten.
    for (auto it = g_since.begin(); it != g_since.end();)
        it = still.contains(it->first) ? std::next(it) : g_since.erase(it);
}

PbpIndoorsStats PbpIndoors_GetStats()
{
    PbpIndoorsStats out = g_stats;
    out.watching = uint32_t(g_since.size());
    return out;
}
