#include "PlayerbotsPlusFire.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusUtil.h"

#include "CellImpl.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "PathGenerator.h"
#include "Player.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include "NewRpgInfo.h"
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <list>
#include <mutex>
#include <random>
#include <unordered_map>
#include <vector>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr uint32_t kPointId   = 0x464952;   // MovePoint id for the step out of the fire
    constexpr float    kTwoPi     = 6.2831853f;
    constexpr float    kSearch    = 20.0f;      // how far round a place fires are looked for
    constexpr float    kMaxRadius = 10.0f;      // wider than this is a boss's floor, not a campfire
    constexpr auto     kOnItsWay  = std::chrono::seconds(3);    // burned once more as it leaves: let it
    constexpr auto     kAgain     = std::chrono::seconds(60);   // burned again this soon: sent on its way

    // Written by the hook on map threads, taken on the world tick.
    std::mutex            g_noteMutex;
    std::vector<uint64_t> g_noted;
    constexpr size_t      kMaxNoted = 1024;

    // World thread from here on.
    std::unordered_map<uint64_t, Clock::time_point> g_lastStep;   // bot -> walked out of a fire at
    PbpFireStats g_stats;

    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    float Frand(float lo, float hi)
    {
        return std::uniform_real_distribution<float>(lo, hi)(Rng());
    }

    // Its spell hurts: at once, over time, or through the spell it sets off.
    bool Hurts(SpellInfo const* spell, bool nested = false)
    {
        if (!spell)
            return false;
        for (SpellEffectInfo const& e : spell->GetEffects())
        {
            switch (e.Effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_ENVIRONMENTAL_DAMAGE:
                case SPELL_EFFECT_HEALTH_LEECH:
                    return true;
                case SPELL_EFFECT_APPLY_AURA:
                case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                case SPELL_EFFECT_APPLY_AREA_AURA_ENEMY:
                    if (e.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE || e.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH ||
                        e.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT)
                        return true;
                    if (!nested && e.ApplyAuraName == SPELL_AURA_PERIODIC_TRIGGER_SPELL &&
                        Hurts(sSpellMgr->GetSpellInfo(e.TriggerSpell), true))
                        return true;
                    break;
                case SPELL_EFFECT_TRIGGER_SPELL:
                    if (!nested && Hurts(sSpellMgr->GetSpellInfo(e.TriggerSpell), true))
                        return true;
                    break;
                default:
                    break;
            }
        }
        return false;
    }

    // How near its middle someone of `size` gets burned, measured the way
    // the game does (GameObject::Update: the trap's radius, plus both
    // sizes), or its spell's area if that is wider. 0 for anything that is
    // not a fire of the world's own: not a trap, someone's trap (a hunter's),
    // one that goes off once, one that only something else sets off, one
    // that does no harm.
    float Reach(GameObject* go, float size)
    {
        if (!go || !go->isSpawned() || go->GetGoType() != GAMEOBJECT_TYPE_TRAP || !go->GetOwnerGUID().IsEmpty())
            return 0.0f;
        GameObjectTemplate const* info = go->GetGOInfo();
        if (!info || !info->trap.spellId || info->trap.type != 0)
            return 0.0f;
        float radius = 0.0f;
        if (info->trap.diameter)
            radius = float(info->trap.diameter) * 0.5f;
        else if (info->trap.cooldown == 3)
            radius = 3.0f;
        else
            return 0.0f;
        SpellInfo const* spell = sSpellMgr->GetSpellInfo(info->trap.spellId);
        if (!spell || spell->IsPositive() || !Hurts(spell))
            return 0.0f;
        for (SpellEffectInfo const& e : spell->GetEffects())
            if (e.Effect && e.HasRadius())
                radius = std::max(radius, e.CalcRadius());
        if (radius > kMaxRadius)
            return 0.0f;
        return radius + go->GetObjectSize() + size;
    }

    struct Fire
    {
        GameObject* go    = nullptr;
        float       reach = 0.0f;   // for whoever it was looked for
        float       dist  = 0.0f;   // from its middle to the place
    };

    void FiresAround(WorldObject const* who, float x, float y, float z, std::vector<Fire>& out)
    {
        std::list<GameObject*> near;
        Acore::GameObjectInRangeCheck check(x, y, z, kSearch);
        Acore::GameObjectListSearcher<Acore::GameObjectInRangeCheck> searcher(who, near, check);
        Cell::VisitObjects(x, y, who->GetMap(), searcher, kSearch);
        const float size = who->GetObjectSize();
        for (GameObject* go : near)
            if (const float reach = Reach(go, size); reach > 0.0f)
                out.push_back({ go, reach, go->GetExactDist(x, y, z) });
    }

    // A random bot nothing else is steering: not in a fight or a duel, not
    // following a group or a real player, on the ground, not casting, held
    // by nothing.
    bool OursToMove(Player* bot, PlayerbotAI* ai)
    {
        if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsBeingTeleported() || !ai->IsBotAI() ||
            !sRandomPlayerbotMgr.IsRandomBot(bot))
            return false;
        if (bot->IsInCombat() || bot->duel || bot->GetGroup() || bot->IsInFlight() || bot->GetVehicle() ||
            bot->GetTransport() || bot->IsNonMeleeSpellCast(false) ||
            bot->HasUnitState(UNIT_STATE_ROOT | UNIT_STATE_STUNNED | UNIT_STATE_CONFUSED | UNIT_STATE_FLEEING))
            return false;
        Player* master = ai->GetMaster();
        return !master || master == bot || !PbpIsRealPlayer(master);
    }

    // Somewhere out of it: away from its middle, Fire.Margin yards or so past
    // its edge -- turning further round when that way is a wall, a drop or
    // the next fire -- on ground it can walk to, in sight of where it stands.
    bool PlaceOut(Player* bot, Fire const& in, float& outX, float& outY, float& outZ)
    {
        Map* map = bot->GetMap();
        const uint32 phase = bot->GetPhaseMask();
        const float bx = bot->GetPositionX(), by = bot->GetPositionY(), bz = bot->GetPositionZ();
        const float fx = in.go->GetPositionX(), fy = in.go->GetPositionY();
        const float away = std::hypot(bx - fx, by - fy) > 0.2f ? std::atan2(by - fy, bx - fx) : Frand(0.0f, kTwoPi);
        static const float kTurns[] = { 0.0f, 0.5f, -0.5f, 1.0f, -1.0f, 1.6f, -1.6f, 2.2f, -2.2f, 2.8f, -2.8f };
        for (float turn : kTurns)
        {
            const float angle = away + turn;
            const float dist  = in.reach + g_FireMargin + Frand(0.0f, 1.5f);   // from the fire's middle
            const float x = fx + dist * std::cos(angle);
            const float y = fy + dist * std::sin(angle);
            const float z = map->GetHeight(phase, x, y, bz + 3.0f, true, 8.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - bz) > 4.0f)
                continue;
            if (map->IsInWater(phase, x, y, z, 2.0f))
                continue;
            if (!map->isInLineOfSight(bx, by, bz + 2.0f, x, y, z + 2.0f, phase, LINEOFSIGHT_ALL_CHECKS,
                                      VMAP::ModelIgnoreFlags::Nothing))
                continue;
            if (PbpFire_Burns(bot, x, y, z, 0.5f))
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

void PbpFireScript::OnDamage(Unit* attacker, Unit* victim, uint32& /*damage*/)
{
    // Damage nobody dealt: a trap of the world's own. Map threads: noted, nothing more.
    if (attacker || !victim || !victim->IsPlayer() || !g_FireEnable)
        return;
    std::lock_guard<std::mutex> lock(g_noteMutex);
    if (g_noted.size() < kMaxNoted)
        g_noted.push_back(victim->GetGUID().GetRawValue());
}

bool PbpFire_Burns(WorldObject const* who, float x, float y, float z, float extra)
{
    if (!who || !who->IsInWorld() || !who->GetMap())
        return false;
    std::vector<Fire> fires;
    FiresAround(who, x, y, z, fires);
    for (Fire const& f : fires)
        if (f.dist < f.reach + extra)
            return true;
    return false;
}

void PbpFire_Update(uint32_t /*diff*/)
{
    std::vector<uint64_t> burned;
    {
        std::lock_guard<std::mutex> lock(g_noteMutex);
        burned.swap(g_noted);
    }

    const auto now = Clock::now();
    static Clock::time_point nextSweep{};
    if (now >= nextSweep)
    {
        nextSweep = now + std::chrono::minutes(1);
        for (auto it = g_lastStep.begin(); it != g_lastStep.end();)
            it = now - it->second > kAgain ? g_lastStep.erase(it) : std::next(it);
    }
    if (burned.empty() || !g_FireEnable || !g_Pbp.enable)
        return;   // (taken all the same: nothing piles up while it is off)
    std::sort(burned.begin(), burned.end());
    burned.erase(std::unique(burned.begin(), burned.end()), burned.end());

    for (uint64_t guid : burned)
    {
        Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(guid));
        PlayerbotAI* ai = PbpAiOf(bot);
        if (!ai || !OursToMove(bot, ai))
            continue;

        auto last = g_lastStep.find(guid);
        if (last != g_lastStep.end() && now - last->second < kOnItsWay && bot->isMoving())
            continue;   // already on its way out: the fire went off once more as it left

        // The fire it is in: the one it stands nearest the middle of.
        std::vector<Fire> fires;
        FiresAround(bot, bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), fires);
        Fire const* in = nullptr;
        for (Fire const& f : fires)
            if (f.dist < f.reach && (!in || f.dist / f.reach < in->dist / in->reach))
                in = &f;
        if (!in)
            continue;   // burned by something else, or out of it already
        ++g_stats.burns;

        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!PlaceOut(bot, *in, x, y, z))
        {
            ++g_stats.nowhere;
            if (g_PbpDebug)
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} is in {} ({}) at {} {:.1f} {:.1f} {:.1f}, "
                         "with nowhere to step out to.", bot->GetName(), in->go->GetName(), in->go->GetEntry(),
                         bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
            continue;
        }

        const PbpHangoutFire hangout = PbpHangout_OutOfFire(bot, x, y, z);
        if (hangout == PbpHangoutFire::OnItsWay)
            continue;   // passing by on its way to a spot: it is not stopping here
        const bool again = last != g_lastStep.end() && now - last->second < kAgain;
        g_lastStep[guid] = now;
        ++g_stats.stepped;
        if (hangout == PbpHangoutFire::Moved)
            ++g_stats.moved;
        else
        {
            // Burned again this soon: whatever it is doing keeps it there (the
            // guard it stops to talk to stands by the brazier). Something else.
            if (again)
            {
                ai->rpgInfo.ChangeToIdle();
                ++g_stats.sentOn;
            }
            if (bot->IsSitState())
                bot->SetStandState(UNIT_STAND_STATE_STAND);
            bot->GetMotionMaster()->MovePoint(kPointId, x, y, z);
            ai->SetNextCheckDelay(2500);   // its own AI lets it get there first
        }
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} steps out of {} ({}) at {} {:.1f} {:.1f} {:.1f}{}.",
                     bot->GetName(), in->go->GetName(), in->go->GetEntry(), bot->GetMapId(), bot->GetPositionX(),
                     bot->GetPositionY(), bot->GetPositionZ(),
                     hangout == PbpHangoutFire::Moved ? ", to a new place at its hangout"
                                                       : (again ? ", and goes and does something else" : ""));
    }
}

PbpFireStats PbpFire_GetStats()
{
    return g_stats;
}
