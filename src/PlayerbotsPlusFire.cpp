#include "PlayerbotsPlusFire.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusUtil.h"

#include "CellImpl.h"
#include "DBCStores.h"
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
#include <fmt/format.h>
#include <cctype>
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

    constexpr uint32_t kPointId     = 0x464952;   // MovePoint id for the step out of the fire
    constexpr float    kTwoPi       = 6.2831853f;
    constexpr float    kSearch      = 20.0f;      // how far round a place fires are looked for
    constexpr float    kMaxRadius   = 10.0f;      // wider than this is a boss's floor, not a campfire
    constexpr float    kMaxSight    = 6.0f;       // a fire to look at wider than this is a model's odd bounds
    constexpr uint32_t kCookingFire = 4;          // the spell focus of campfires, bonfires, braziers, stoves
    constexpr auto     kOnItsWay    = std::chrono::seconds(3);    // burned once more as it leaves: let it
    constexpr auto     kAgain       = std::chrono::seconds(60);   // burned again this soon: sent on its way
    constexpr auto     kSweepEvery  = std::chrono::seconds(2);    // a look for bots stood in a fire that does no harm
    constexpr float    kComfort     = 0.5f;   // nobody stands right up against a fire, burning or not
    constexpr float    kMounted     = 2.5f;   // a mount's head or tail, past its rider in the middle
    constexpr auto     kNowhereWait = std::chrono::seconds(10);   // nowhere to step out to: looked at again after this

    // Written by the hook on map threads, taken on the world tick.
    std::mutex            g_noteMutex;
    std::vector<uint64_t> g_noted;
    constexpr size_t      kMaxNoted = 1024;

    // World thread from here on.
    std::unordered_map<uint64_t, Clock::time_point> g_lastStep;   // bot -> walked out of a fire at
    std::unordered_map<uint64_t, Clock::time_point> g_nowhere;    // bot -> nowhere to step to, until
    std::unordered_map<uint32_t, bool> g_looksLikeFire;           // object entry -> a fire to look at
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

    // A fire anyone can see is one: a cooking fire (a campfire, a bonfire, a
    // brazier, a stove: spell focus 4), or something named like a fire. Most
    // of them do no harm -- Orgrimmar's bonfires burn nobody -- so nobody is
    // ever noted standing in one, and bots stood in them all the same. By
    // entry, worked out once.
    bool LooksLikeFire(GameObject* go)
    {
        const uint32 entry = go->GetEntry();
        if (auto it = g_looksLikeFire.find(entry); it != g_looksLikeFire.end())
            return it->second;
        bool fire = false;
        if (GameObjectTemplate const* t = go->GetGOInfo())
        {
            if (t->type == GAMEOBJECT_TYPE_SPELL_FOCUS && t->spellFocus.focusId == kCookingFire)
                fire = true;
            else if (t->type == GAMEOBJECT_TYPE_SPELL_FOCUS || t->type == GAMEOBJECT_TYPE_GENERIC ||
                     t->type == GAMEOBJECT_TYPE_QUESTGIVER || t->type == GAMEOBJECT_TYPE_GOOBER)
            {
                // Its name, word by word: "Bonfire", "Dwarven Fire", "Burning
                // Embers" -- not "Firework Launcher", not "Pyrewood Village".
                std::string word;
                for (size_t i = 0; i <= t->name.size() && !fire; ++i)
                {
                    const char c = i < t->name.size() ? t->name[i] : ' ';
                    if (std::isalpha(static_cast<unsigned char>(c)))
                        word += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
                    else
                    {
                        for (const char* w : { "bonfire", "campfire", "brazier", "firepit", "pyre", "fire", "blaze",
                                               "embers", "flames" })
                            fire = fire || word == w;
                        word.clear();
                    }
                }
            }
        }
        g_looksLikeFire.emplace(entry, fire);
        return fire;
    }

    // How near its middle someone of `size` stands in a fire anyone can see:
    // its model's footprint at the size it is shown, and theirs. 0 for
    // anything else, or one with nothing to show.
    float SightReach(GameObject* go, float size)
    {
        if (!go || !go->isSpawned() || !LooksLikeFire(go))
            return 0.0f;
        GameObjectDisplayInfoEntry const* shown = sGameObjectDisplayInfoStore.LookupEntry(go->GetDisplayId());
        if (!shown)
            return 0.0f;
        float half = std::max({ std::fabs(shown->minX), std::fabs(shown->maxX), std::fabs(shown->minY),
                                std::fabs(shown->maxY) });
        if (half <= 0.0f)
            half = 1.0f;   // a model with no bounds given: about a campfire's
        return std::clamp(half * go->GetObjectScale(), 0.4f, kMaxSight) + size;
    }

    struct Fire
    {
        GameObject* go       = nullptr;
        float       reach    = 0.0f;    // for whoever it was looked for
        float       dist     = 0.0f;    // from its middle to the place
        bool        harmless = false;   // one to look at only: it burns nobody
    };

    // How far past a fire's edge someone keeps clear of it as it looks:
    // a little room, and a mount's length when they ride one -- a bot sat on
    // its mammoth beside a bonfire had the beast's head in the flames.
    float SightExtra(WorldObject const* who)
    {
        Unit const* unit = who ? who->ToUnit() : nullptr;
        return kComfort + (unit && unit->IsMounted() ? kMounted : 0.0f);
    }

    // The fires about a place: the ones that burn (Reach, for someone of
    // `size`) and the ones that only look like it (SightReach, with
    // `sightExtra` more), whichever reaches further.
    void FiresAround(Map* map, WorldObject const* phaseRef, float size, float sightExtra, float x, float y,
                     float z, std::vector<Fire>& out)
    {
        std::list<GameObject*> near;
        Acore::GameObjectInRangeCheck check(x, y, z, kSearch);
        Acore::GameObjectListSearcher<Acore::GameObjectInRangeCheck> searcher(phaseRef, near, check);
        Cell::VisitObjects(x, y, map, searcher, kSearch);
        for (GameObject* go : near)
        {
            const float burns = Reach(go, size);
            const float looks = SightReach(go, size + sightExtra);
            if (burns > 0.0f || looks > 0.0f)
                out.push_back({ go, std::max(burns, looks), go->GetExactDist(x, y, z), burns <= 0.0f });
        }
    }

    // The fire `bot` stands in, if any: the one it is nearest the middle of.
    bool InFire(Player* bot, Fire& in)
    {
        std::vector<Fire> fires;
        FiresAround(bot->GetMap(), bot, bot->GetObjectSize(), SightExtra(bot), bot->GetPositionX(),
                    bot->GetPositionY(), bot->GetPositionZ(), fires);
        Fire const* best = nullptr;
        for (Fire const& f : fires)
            if (f.dist < f.reach && std::fabs(f.go->GetPositionZ() - bot->GetPositionZ()) < 4.0f &&
                (!best || f.dist / f.reach < best->dist / best->reach))
                best = &f;
        if (!best)
            return false;
        in = *best;
        return true;
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
        for (int i = 0; i < 2 * int(std::size(kTurns)); ++i)
        {
            // Every way round at the margin first, then every way a step or
            // two further out.
            const bool wider  = i >= int(std::size(kTurns));
            const float angle = away + kTurns[i % std::size(kTurns)];
            const float dist  = in.reach + g_FireMargin + (wider ? 2.5f : 0.0f) + Frand(0.0f, 1.5f);
            float x = fx + dist * std::cos(angle);
            float y = fy + dist * std::sin(angle);
            const float z = map->GetHeight(phase, x, y, bz + 3.0f, true, 8.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - bz) > 4.0f)
                continue;
            if (map->IsInWater(phase, x, y, z, 2.0f))
                continue;
            // In sight past walls and the like -- not past the fire's own
            // model, which a bot stood inside it may well be in the way of.
            if (!map->isInLineOfSight(bx, by, bz + 2.0f, x, y, z + 2.0f, phase, LINEOFSIGHT_CHECK_VMAP,
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
            // (The ground about a bonfire is lumpy: the path's end and the
            // ground's height can be a yard or so apart.)
            G3D::Vector3 const& end = path.GetActualEndPosition();
            if (std::hypot(end.x - x, end.y - y) > 2.0f || std::fabs(end.z - z) > 2.0f)
                continue;
            x = end.x;
            y = end.y;
            if (PbpFire_Burns(bot, x, y, end.z, 0.5f))
                continue;
            outX = x;
            outY = y;
            outZ = end.z;
            return true;
        }
        return false;
    }
}

namespace
{
    // Out of `in`, now: to a new place at its hangout, or a step of its own.
    void StepOut(Player* bot, PlayerbotAI* ai, Fire const& in, const Clock::time_point now)
    {
        const uint64_t guid = bot->GetGUID().GetRawValue();
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (!PlaceOut(bot, in, x, y, z))
        {
            ++g_stats.nowhere;
            g_nowhere[guid] = now + kNowhereWait;
            if (g_PbpDebug)
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} is in {} ({}) at {} {:.1f} {:.1f} {:.1f}, "
                         "with nowhere to step out to.", bot->GetName(), in.go->GetName(), in.go->GetEntry(),
                         bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ());
            return;
        }

        const PbpHangoutMove hangout = PbpHangout_MoveTo(bot, x, y, z);
        if (hangout == PbpHangoutMove::OnItsWay)
            return;   // passing by on its way to a spot: it is not stopping here
        auto last = g_lastStep.find(guid);
        const bool again = last != g_lastStep.end() && now - last->second < kAgain;
        g_lastStep[guid] = now;
        ++g_stats.stepped;
        if (hangout == PbpHangoutMove::Moved)
            ++g_stats.moved;
        else
        {
            // In it again this soon: whatever it is doing keeps it there (the
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
                     bot->GetName(), in.go->GetName(), in.go->GetEntry(), bot->GetMapId(), bot->GetPositionX(),
                     bot->GetPositionY(), bot->GetPositionZ(),
                     hangout == PbpHangoutMove::Moved ? ", to a new place at its hangout"
                                                       : (again ? ", and goes and does something else" : ""));
    }

    // Every couple of seconds: bots standing still in a fire anyone can see
    // is one, where someone could see them (Space.Range of a real player).
    // Most fires burn nobody, so nothing notes it otherwise.
    void Sweep(const Clock::time_point now)
    {
        std::vector<Player*> real;
        for (auto const& [guid, p] : ObjectAccessor::GetPlayers())
            if (p && p->IsInWorld() && PbpIsRealPlayer(p))
                real.push_back(p);
        if (real.empty())
            return;
        const float rangeSq = g_SpaceRange * g_SpaceRange;
        for (auto const& [guid, bot] : ObjectAccessor::GetPlayers())
        {
            if (!bot || !bot->IsInWorld() || bot->isMoving() || PbpIsRealPlayer(bot))
                continue;
            bool seen = g_SpaceRange <= 0.0f;
            for (Player* r : real)
                if (!seen && r->GetMapId() == bot->GetMapId() &&
                    r->GetExactDist2dSq(bot->GetPositionX(), bot->GetPositionY()) < rangeSq)
                    seen = true;
            if (!seen)
                continue;
            const uint64_t key = guid.GetRawValue();
            if (auto last = g_lastStep.find(key); last != g_lastStep.end() && now - last->second < kOnItsWay)
                continue;   // on its way out already
            if (auto wait = g_nowhere.find(key); wait != g_nowhere.end() && now < wait->second)
                continue;
            PlayerbotAI* ai = PbpAiOf(bot);
            if (!ai || !OursToMove(bot, ai))
                continue;
            Fire in;
            if (!InFire(bot, in))
                continue;
            if (in.harmless)
                ++g_stats.seen;
            else
                ++g_stats.burns;
            StepOut(bot, ai, in, now);
        }
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

namespace
{
    bool InAny(Map* map, WorldObject const* phaseRef, float size, float sightExtra, float x, float y, float z,
               float extra)
    {
        if (!map || !phaseRef)
            return false;
        std::vector<Fire> fires;
        FiresAround(map, phaseRef, size, sightExtra, x, y, z, fires);
        for (Fire const& f : fires)
            if (f.dist < f.reach + extra)
                return true;
        return false;
    }
}

bool PbpFire_BurnsAt(Map* map, WorldObject const* phaseRef, float size, float x, float y, float z, float extra)
{
    return InAny(map, phaseRef, size, kComfort, x, y, z, extra);
}

bool PbpFire_Burns(WorldObject const* who, float x, float y, float z, float extra)
{
    if (!who || !who->IsInWorld())
        return false;
    return InAny(who->GetMap(), who, who->GetObjectSize(), SightExtra(who), x, y, z, extra);
}

float PbpFire_MountRoom()
{
    return kMounted;
}

std::vector<std::string> PbpFire_Describe(Player* where)
{
    std::vector<std::string> out;
    if (!where || !where->IsInWorld())
        return out;
    // The fires about them, burning or not, and how near anyone may stand.
    std::list<GameObject*> near;
    Acore::GameObjectInRangeCheck check(where->GetPositionX(), where->GetPositionY(), where->GetPositionZ(), 40.0f);
    Acore::GameObjectListSearcher<Acore::GameObjectInRangeCheck> searcher(where, near, check);
    Cell::VisitObjects(where, searcher, 40.0f);
    std::list<Player*> people;
    Acore::AnyPlayerInObjectRangeCheck nearPeople(where, 50.0f);
    Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> peopleSearcher(where, people, nearPeople);
    Cell::VisitObjects(where, peopleSearcher, 50.0f);
    const auto now = Clock::now();
    for (GameObject* go : near)
    {
        const float burns = Reach(go, 1.5f);
        const float looks = SightReach(go, 1.5f + kComfort);
        if (burns <= 0.0f && looks <= 0.0f)
            continue;
        GameObjectDisplayInfoEntry const* shown = sGameObjectDisplayInfoStore.LookupEntry(go->GetDisplayId());
        out.push_back(fmt::format("{} ({}, type {}{}) {:.1f} yards off: anyone on foot keeps {:.1f} from its "
                                  "middle, mounted {:.1f}{}{}",
                                  go->GetName(), go->GetEntry(), uint32(go->GetGoType()),
                                  burns > 0.0f ? ", burns" : "", where->GetExactDist(go),
                                  std::max(burns, looks), std::max(burns, looks + kMounted),
                                  shown ? fmt::format(" (model {:.1f}x{:.1f} at size {:.2f})",
                                                      shown->maxX - shown->minX, shown->maxY - shown->minY,
                                                      go->GetObjectScale())
                                        : std::string(" (no model)"),
                                  ""));
        for (Player* p : people)
        {
            const float d = p->GetExactDist2d(go);
            const float reach = std::max(Reach(go, p->GetObjectSize()),
                                         SightReach(go, p->GetObjectSize() + SightExtra(p)));
            if (d > reach + 3.0f || std::fabs(p->GetPositionZ() - go->GetPositionZ()) > 4.0f)
                continue;
            PlayerbotAI* ai = PbpAiOf(p);
            const uint64_t key = p->GetGUID().GetRawValue();
            std::string why;
            if (PbpIsRealPlayer(p))
                why = "a real player";
            else if (!ai || !OursToMove(p, ai))
                why = "not one to move (busy, grouped, a real player's, casting...)";
            else if (p->isMoving())
                why = "moving";
            else if (auto w = g_nowhere.find(key); w != g_nowhere.end() && now < w->second)
                why = "nowhere to step out to just now";
            out.push_back(fmt::format("   {}{}: {:.1f} yards from the middle -- {}{}", p->GetName(),
                                      p->IsMounted() ? " (mounted)" : "", d, d < reach ? "IN IT" : "clear",
                                      d < reach && !why.empty() ? " (" + why + ")" : ""));
        }
    }
    if (out.empty())
        out.push_back("No fire within 40 yards.");
    return out;
}

void PbpFire_Update(uint32_t /*diff*/)
{
    std::vector<uint64_t> burned;
    {
        std::lock_guard<std::mutex> lock(g_noteMutex);
        burned.swap(g_noted);
    }

    const auto now = Clock::now();
    static Clock::time_point nextTidy{};
    if (now >= nextTidy)
    {
        nextTidy = now + std::chrono::minutes(1);
        for (auto it = g_lastStep.begin(); it != g_lastStep.end();)
            it = now - it->second > kAgain ? g_lastStep.erase(it) : std::next(it);
        for (auto it = g_nowhere.begin(); it != g_nowhere.end();)
            it = now >= it->second ? g_nowhere.erase(it) : std::next(it);
    }
    if (!g_FireEnable || !g_Pbp.enable)
        return;   // (taken all the same: nothing piles up while it is off)
    static Clock::time_point nextSweep{};
    if (now >= nextSweep)
    {
        nextSweep = now + kSweepEvery;
        Sweep(now);
    }
    if (burned.empty())
        return;
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
        Fire in;
        if (!InFire(bot, in))
            continue;   // burned by something else, or out of it already
        ++g_stats.burns;
        StepOut(bot, ai, in, now);
    }
}

PbpFireStats PbpFire_GetStats()
{
    return g_stats;
}
