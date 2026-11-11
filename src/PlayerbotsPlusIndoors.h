#ifndef PLAYERBOTS_PLUS_INDOORS_H
#define PLAYERBOTS_PLUS_INDOORS_H

#include <cstdint>

// --------------------------------------------------------------------------
// On foot indoors.
//
// The game lets nobody get on a mount under a roof, but it does not take
// anyone off one who rides in, and playerbots' mount check does nothing at
// all indoors, up or down. So a bot that rides in -- or is teleported to a
// banker on its mount -- stands at the counter on it for as long as it is
// there.
//
// Once a second, a bot that has been on a mount (or in a druid's travel or
// flight form) indoors for Indoors.Seconds, and is standing still, gets off
// it. Riding through a gateway or a tunnel without stopping is left alone,
// as a player would ride through. Only a bot on the ground in the open
// world, out of any fight, and not on a flight path, a vehicle or a boat;
// never a self-bot. A player's own bots stay up while their player is on a
// mount close by, and get off once the player has. Outside again, playerbots
// puts them back on their mounts as it always does. City hangouts
// (PlayerbotsPlusHangout.h) never have a bot ride at a place under a roof in
// the first place.
//
// World thread only.
// --------------------------------------------------------------------------

// World tick. Call it with the module switched off too: it then forgets
// whoever it was watching.
void PbpIndoors_Update(uint32_t diff);

struct PbpIndoorsStats
{
    uint64_t dismounted = 0;   // bots taken off their mount (or out of travel form) indoors
    uint32_t watching   = 0;   // bots on a mount indoors right now, riding on or not long enough yet
};
PbpIndoorsStats PbpIndoors_GetStats();

#endif // PLAYERBOTS_PLUS_INDOORS_H
