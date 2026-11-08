#ifndef PLAYERBOTS_PLUS_SPACE_H
#define PLAYERBOTS_PLUS_SPACE_H

#include <cstdint>

class Map;
class WorldObject;

// --------------------------------------------------------------------------
// Room to stand.
//
// Players who stop somewhere stand a step apart: nobody waits for the
// innkeeper inside the next person, or inside the innkeeper. Playerbots never
// minds where it stops. A bot sent to an innkeeper walks to the innkeeper's
// own spot, a bot that is doing nothing stands wherever it last stopped, and
// a handful of them end up in one place, on top of each other.
//
// Every Space.CheckSeconds, a random bot that has stood still for
// Space.StillSeconds -- out of any fight, not in a group, with no real
// player as its master, not casting, not on a chair -- and is closer than
// Space.Distance yards to someone else standing still (a player, or an NPC,
// whose own size counts) steps aside: Space.StepMin..StepMax yards away
// from them, onto ground it can walk to, on the same floor, in sight, out of
// any fire and out of anyone else's way, and turns back to face them. Of two
// bots on one spot the newcomer moves. A bot hanging out at a city spot
// (PlayerbotsPlusHangout.h) takes the new place as its own. One that has
// just stepped aside does not again for Space.CooldownSeconds, so nothing
// pulling it back makes it shuffle on the spot. Only bots within
// Space.Range yards of a real player are looked at: crowding nobody sees
// is left alone.
//
// World thread only.
// --------------------------------------------------------------------------

// World tick. Call it with the module switched off too: then it forgets
// what it was keeping track of.
void PbpSpace_Update(uint32_t diff);

// Whether someone is in the way at x, y, z on `map`: a player, or an NPC
// (not a pet, a totem or a hidden trigger), not on the move, whose middle is
// within `room` yards of it -- a big one's own size added. `phaseRef` gives
// the phase; `except` is left out (the one asking).
bool PbpSpace_Crowded(Map* map, WorldObject const* phaseRef, float x, float y, float z, float room,
                      WorldObject const* except = nullptr);

struct PbpSpaceStats
{
    uint64_t crowded = 0;   // bots found standing in someone
    uint64_t stepped = 0;   // ...that stepped aside
    uint64_t moved   = 0;   // ...of whom hanging out, given a new place at their spot
    uint64_t nowhere = 0;   // ...with nowhere to step to, just then
};
PbpSpaceStats PbpSpace_GetStats();

#endif // PLAYERBOTS_PLUS_SPACE_H
