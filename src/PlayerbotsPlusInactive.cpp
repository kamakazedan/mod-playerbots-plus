#include "PlayerbotsPlusInactive.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusFire.h"
#include "PlayerbotsPlusSpace.h"
#include "PlayerbotsPlusUtil.h"

#include "Map.h"
#include "MotionMaster.h"
#include "Player.h"
#include "SpellAuraDefines.h"

#include "PlayerbotAI.h"

#include <algorithm>
#include <cmath>
#include <random>

using namespace pbp;

namespace
{
    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    float Frand(float lo, float hi)
    {
        return std::uniform_real_distribution<float>(lo, hi)(Rng());
    }

    // Where someone who had walked there would stand: a few steps from the
    // place, on the side it came from, on the floor in sight of it, out of
    // the water, any fire and anyone else.
    bool StepsFrom(Player* bot, Map* map, float x, float y, float z, float within, float& outX, float& outY,
                   float& outZ)
    {
        uint32_t const phase = bot->GetPhaseMask();
        float const from = std::atan2(bot->GetPositionY() - y, bot->GetPositionX() - x);
        for (int tries = 0; tries < 8; ++tries)
        {
            float const angle = from + Frand(-1.2f, 1.2f);
            float const dist  = Frand(std::min(1.5f, within), within);
            float const px = x + dist * std::cos(angle);
            float const py = y + dist * std::sin(angle);
            float const pz = map->GetHeight(phase, px, py, z + 2.0f, true, 6.0f);
            if (pz <= INVALID_HEIGHT || std::fabs(pz - z) > 2.0f || map->IsInWater(phase, px, py, pz, 2.0f))
                continue;
            if (!map->isInLineOfSight(x, y, z + 1.5f, px, py, pz + 1.5f, phase, LINEOFSIGHT_ALL_CHECKS,
                                      VMAP::ModelIgnoreFlags::Nothing))
                continue;
            if (PbpFire_BurnsAt(map, bot, bot->GetObjectSize(), px, py, pz, 0.5f) ||
                PbpSpace_Crowded(map, bot, px, py, pz, 1.5f, bot))
                continue;
            outX = px;
            outY = py;
            outZ = pz;
            return true;
        }
        return false;
    }
}

bool PbpInactive(PlayerbotAI* ai)
{
    return ai && !ai->IsActivityAllowedCached();
}

bool PbpUnseen(Player* bot, float x, float y, float z)
{
    uint32_t const map = bot->GetMapId();
    return !PbpRealPlayerNear(map, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(),
                              g_InactiveUnseenYards) &&
           !PbpRealPlayerNear(map, x, y, z, g_InactiveUnseenYards);
}

bool PbpPutWhereGoing(Player* bot, uint32_t map, float x, float y, float z, float within)
{
    if (!g_InactiveEnable || !bot || !bot->IsInWorld() || bot->IsBeingTeleported() || !bot->IsAlive() ||
        bot->IsInCombat() || bot->GetMapId() != map || bot->IsInFlight() || bot->GetTransport() ||
        bot->GetVehicle() || bot->HasUnitState(UNIT_STATE_LOST_CONTROL) ||
        bot->GetExactDist(x, y, z) <= std::max(within, 2.0f))
        return false;
    PlayerbotAI* ai = PbpAiOf(bot);
    if (!PbpInactive(ai) || !PbpUnseen(bot, x, y, z))
        return false;

    Map* m = bot->GetMap();
    float px = x, py = y, pz = z;
    if (within <= 0.0f || !StepsFrom(bot, m, x, y, z, within, px, py, pz))
    {
        // Right there, on the floor if there is one close under it.
        float const ground = m->GetHeight(bot->GetPhaseMask(), x, y, z + 2.0f, true, 6.0f);
        if (ground > INVALID_HEIGHT && std::fabs(ground - z) < 3.0f)
            pz = ground;
    }
    // Facing the place, as one who has just walked up to it.
    float const o = std::hypot(x - px, y - py) > 0.5f ? std::atan2(y - py, x - px) : bot->GetOrientation();

    // As playerbots puts a bot whose walk gets nowhere where it was going.
    bot->GetMotionMaster()->Clear();
    bot->StopMoving();
    bot->RemoveAurasWithInterruptFlags(AURA_INTERRUPT_FLAG_TELEPORTED | AURA_INTERRUPT_FLAG_CHANGE_MAP);
    if (bot->IsSitState())
        bot->SetStandState(UNIT_STAND_STATE_STAND);
    return bot->TeleportTo(map, px, py, pz + 0.05f, o);
}
