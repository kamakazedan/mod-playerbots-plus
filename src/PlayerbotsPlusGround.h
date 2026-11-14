#ifndef PLAYERBOTS_PLUS_GROUND_H
#define PLAYERBOTS_PLUS_GROUND_H

#include <cstdint>

// --------------------------------------------------------------------------
// Feet on the ground.
//
// Playerbots sends bots to places that are not on the ground, and a bot has
// no game client to bring it down again: it stays wherever it was put, and
// its next walk starts from up there -- a glide through the air.
//
// Most of it is playerbots' grind spots. They are a grid 50 yards a side,
// built from the creatures' spawns, with the height rounded to 50 as well:
// half of them are more than 12 yards above or below the ground under them.
// A bot walking to one comes to a stop under it (or over it), never within
// the 10 yards that count as there, and a minute and a half later playerbots
// takes it for stuck and teleports it there -- up into the air, or into the
// ground.
//
// Every second, for playerbots' random bots in the open world that no real
// player plays or leads:
// - a grind spot on that grid is moved onto the spawn point of a creature in
//   its square, the one nearest its middle: where what the bot is going to
//   grind stands, on the ground;
// - a bot standing more than Ground.AirHeight yards above the floor (or the
//   water) under it, or walking more than twice that above it, on two looks a
//   second apart, falls to the ground as anyone would (onto water: put on
//   it). Not one that may be up there: flying, swimming, on a flight path, a
//   boat or a vehicle, jumping, falling, knocked back, floating down. No
//   fall damage: nothing but a mistake put it up there. One brought down
//   three times in two minutes is also sent off to do something else -- but
//   for one this module has somewhere to go (a hangout, a group quest, a bot
//   dungeon or a walk for one), which keeps its own;
// - a bot inside the ground -- under the land, with nothing to stand on --
//   on two looks is put back on top of it.
//
// With PlayerbotsPlus.Debug on, each one is logged.
//
// World thread only.
// --------------------------------------------------------------------------

// The creatures' spawns, by playerbots' grid square: once, at startup (the
// world's spawns are loaded by WORLDHOOK_ON_STARTUP).
void PbpGround_Load();

// World tick. Call it with the module switched off too: it then forgets
// whoever it was watching.
void PbpGround_Update(uint32_t diff);

struct PbpGroundStats
{
    uint64_t spots   = 0;   // grind spots off the ground, moved onto a spawn in their square
    uint64_t noSpawn = 0;   // ...with no spawn known in the square: left as they were
    uint64_t dropped = 0;   // bots brought down out of the air
    uint64_t gliding = 0;   // ...of them walking through it
    uint64_t water   = 0;   // ...of them over water, put on it
    uint64_t sentOn  = 0;   // ...brought down three times in two minutes, sent off to do something else
    uint64_t raised  = 0;   // bots put back on top of the ground they were inside
    uint32_t squares = 0;   // grid squares with a spawn known
    uint32_t loadMs  = 0;   // how long working them out took
    bool     loaded  = false;
};
PbpGroundStats PbpGround_GetStats();

#endif // PLAYERBOTS_PLUS_GROUND_H
