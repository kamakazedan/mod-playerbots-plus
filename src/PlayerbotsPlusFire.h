#ifndef PLAYERBOTS_PLUS_FIRE_H
#define PLAYERBOTS_PLUS_FIRE_H

#include "ScriptMgr.h"

#include <cstdint>

class Map;
class Unit;
class WorldObject;

// --------------------------------------------------------------------------
// Bots standing in a fire.
//
// A campfire, a bonfire or a brazier by a city's guards is a trap the game
// fires at whoever stands in it: a few points of fire damage every few
// seconds. Playerbots only steps out of one in a fight, and only for a bot
// with a real player as its master ("avoid aoe"), so a random bot that stops
// by a guard to talk, or one that sits down to rest by a campfire, burns on
// the spot for as long as it stays there.
//
// Damage nobody dealt -- a trap of the world's own, not a mob or a player --
// is noted as it happens (a UnitScript hook, on map threads: record only).
// On the world tick a random bot that was burned -- alive, out of any fight,
// not in a group, with no real player as its master, on the ground, not
// casting -- and that stands within reach of a fire (a trap of the world's
// own that does damage: its diameter's reach, or its spell's area if that is
// wider, the way the game measures it) walks out of it: away from the middle,
// Fire.Margin yards or so past its edge, onto ground it can walk to, in
// sight, and not into the next fire. A bot hanging out at a city spot
// (PlayerbotsPlusHangout.h) takes that as its new place there. One burned
// again within a minute of the last time is also sent about its business
// (playerbots' own "idle", which picks something else to do), so it does not
// walk straight back to the guard it was talking to.
//
// PbpFire_Burns says whether a place is in a fire, so a hangout never puts
// anyone in one.
//
// World thread only, except the hook.
// --------------------------------------------------------------------------

// World tick: the bots burned since the last one, walked out of the fire.
void PbpFire_Update(uint32_t diff);

// Whether someone the size of `who`, standing at x, y, z on its map, would be
// in a fire (a damaging trap of the world's own), with `extra` yards to
// spare. False for a place whose grid is not loaded: nothing is known there.
bool PbpFire_Burns(WorldObject const* who, float x, float y, float z, float extra = 0.0f);

// The same on `map`, for someone of `size` who may not be on it yet
// (`phaseRef` gives the phase).
bool PbpFire_BurnsAt(Map* map, WorldObject const* phaseRef, float size, float x, float y, float z, float extra = 0.0f);

struct PbpFireStats
{
    uint64_t burns   = 0;   // bots burned by a trap of the world's own
    uint64_t stepped = 0;   // ...walked out of the fire
    uint64_t moved   = 0;   // ...of whom hanging out, given a new place at their spot
    uint64_t sentOn  = 0;   // ...of whom burned again within a minute, sent about their business
    uint64_t nowhere = 0;   // in a fire, with nowhere to step to
};
PbpFireStats PbpFire_GetStats();

class PbpFireScript : public UnitScript
{
public:
    PbpFireScript() : UnitScript("PbpFireScript", true, { UNITHOOK_ON_DAMAGE }) {}
    void OnDamage(Unit* attacker, Unit* victim, uint32& damage) override;
};

#endif // PLAYERBOTS_PLUS_FIRE_H
