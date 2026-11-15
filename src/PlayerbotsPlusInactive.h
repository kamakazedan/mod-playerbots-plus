#ifndef PLAYERBOTS_PLUS_INACTIVE_H
#define PLAYERBOTS_PLUS_INACTIVE_H

#include <cstdint>

class Player;
class PlayerbotAI;

// --------------------------------------------------------------------------
// Bots playerbots is not running just now.
//
// Playerbots runs the AI of only some of its random bots at a time
// (Playerbots.BotActiveAlone: a share of them, taken in turns a minute at a
// time, and less of it the slower the world tick -- its SmartScale). One
// near a real player or in one's zone, fighting, in a dungeon or a queue, or
// grouped with a real player always runs. One it has not picked just now
// does nothing at all: it stands where it is, <AFK>, whatever it was on its
// way to do -- and in a group of bots everyone does while the leader does.
// Out in the world, away from the few real players, that is nearly every bot
// nearly all the time: a walk of a few hundred yards for this module (to a
// dungeon quest's giver, a group quest's, the party getting together, its way
// to what the quest is for, a hangout spot) took an hour or never got there,
// and a group quest's leader stood three tries running where it was.
//
// So a walk of this module's that playerbots is not running is made at once
// while nobody would see it: the bot is put where it was going -- the way
// playerbots itself puts a bot whose walk gets nowhere where it was going
// (its MoveFarTo), and hangouts fetch a bot into a city nobody is in. With a
// real player near either end, it walks when its turn comes, as before.
// (Inactive.Enable, Inactive.UnseenYards.)
//
// World thread.
// --------------------------------------------------------------------------

// Playerbots is not running this bot just now (it was not picked, and is not
// near anyone, fighting, ...): it will not move. As playerbots last worked
// it out, at most five seconds ago.
bool PbpInactive(PlayerbotAI* ai);

// Nobody would see `bot` go from where it is, nor turn up at (x, y, z) on its
// map: no real player within Inactive.UnseenYards of either.
bool PbpUnseen(Player* bot, float x, float y, float z);

// Puts `bot` where it was walking to: on the floor a few steps from (x, y, z)
// on `map` -- within `within` yards, in sight of it and out of the water --
// or, within 0 (or nowhere like that), right there. Only with
// Inactive.Enable on, for a bot playerbots is not running (PbpInactive),
// alive, out of a fight, on that map, on its own feet (not on a flight, a
// boat or a vehicle) and further than `within` from it, while nobody would
// see (PbpUnseen). True: put there (a teleport; it lands on its next update).
bool PbpPutWhereGoing(Player* bot, uint32_t map, float x, float y, float z, float within);

#endif // PLAYERBOTS_PLUS_INACTIVE_H
