#ifndef PLAYERBOTS_PLUS_HANGOUT_H
#define PLAYERBOTS_PLUS_HANGOUT_H

#include <cstdint>
#include <string>

class Player;

// --------------------------------------------------------------------------
// City hangouts: idle bots in a capital gather where players do.
//
// On a real server everyone parks in the same places -- the Trade District,
// the Commons, the Valley of Strength, outside the bank in Dalaran -- mounted
// on their best mount, sat on a bench, or stood there AFK. Playerbots never
// sends a bot anywhere in particular: one with nothing to do in a city walks
// from NPC to NPC, and a "rest" is 30 seconds sat wherever it happens to be.
//
// Every Hangout.CheckSeconds, a spot (PlayerbotsPlus.Hangout.Spot.*) with
// room may take one more of the idle random bots in its zone (and on its
// level of it) -- not grouped, not questing, flying or fighting, not one a
// real player has targeted. It walks over (playerbots' own "go camp"
// movement) to a free place within the radius (on the ground, in sight of
// the middle, out of the water, one you can walk to), and settles there for
// StayMinutesMin..Max:
//
//   * on a bench within Hangout.BenchRange that is free;
//   * mounted, if it can ride and it is outdoors (playerbots puts an idle
//     random bot on its best mount);
//   * sat on the ground, or just standing about;
//   * and now and then AFK (the flag, so it shows <AFK>): it stays quiet,
//     and when someone talks to it, it is back.
//
// The idle bots already in a city run out half an hour or so into a session
// (playerbots sends few bots to the capitals), so a spot short of
// Hangout.BringInFill of its room also gets bots fetched from elsewhere:
// random bots of the city's side and level, out in the world on their own,
// with no real player near enough to see them go. While someone is in the
// city one arrives at an inn near the spot (the nearest friendly one, within
// Hangout.ArrivalRange) and walks over; while nobody is, it is put down in
// the middle of the spot, and when it is done it goes back where it came from
// if nobody would see. Hangout.BringInPerMinute caps them, all spots
// together.
//
// From the moment it gets there its playerbots AI is held (renewed every
// world tick), the way a parked player does nothing: otherwise playerbots
// stands a sitting bot up and takes its AFK off on its very next tick. An
// invite (group, guild, arena team), a trade, a duel, a queue or a fight lets
// it go at once. When its time is up it stands and goes about its business,
// and does not hang out again for Hangout.CooldownMinutes. Switching it off
// lets everyone go at once; if this stopped running altogether, the hold
// would run out within five seconds.
//
// World thread only: PbpHangout_Update runs in the world update, while the
// map threads (and every bot's AI) are idle.
// --------------------------------------------------------------------------

// World tick. Call it with the module or the feature switched off too: then
// everyone hanging out is let go at once.
void PbpHangout_Update(uint32_t diff);

// Hanging out AFK (the flag this set). Not for a bot that has since been
// invited, traded with or attacked: that one is let go here and now.
bool PbpHangout_IsAway(Player* bot);

// Someone talked to it, or it is about to talk: back at the keyboard.
void PbpHangout_ComeBack(Player* bot);

// True while a hanging-out bot is sitting down on a bench here, so a module
// that reacts to objects being used does not take it for news. Any thread
// (it is thread_local: only the world thread ever sets it).
bool PbpHangout_UsingBench();

// Settled at a spot: where ("the Trade District in Stormwind"), how ("sitting
// on a bench"), whether it is AFK right now, and whether it came back from
// being AFK in the last few minutes. False when it is not settled at one.
bool PbpHangout_Get(Player* bot, std::string& spot, std::string& doing, bool& away, bool& justBack);

struct PbpHangoutStats
{
    uint64_t sent      = 0;   // set off for a spot
    uint64_t settled   = 0;   // got there and settled
    uint64_t gaveUp    = 0;   // never got there
    uint64_t movedOn   = 0;   // time up, off again
    uint64_t cutShort  = 0;   // a group, a fight, moved away...
    uint64_t cameBack  = 0;   // AFK, then talked to
    uint64_t broughtIn = 0;   // fetched from elsewhere (Hangout.BringIn)
    uint64_t sentBack  = 0;   // ...and gone back there when done
    uint32_t hanging   = 0;   // there now
    uint32_t away      = 0;   // ...of whom AFK
    uint32_t onTheWay  = 0;
    uint32_t spots     = 0;
    uint32_t withInn   = 0;   // spots with an inn their fetched bots arrive at (of those looked at so far)
};
PbpHangoutStats PbpHangout_GetStats();

#endif // PLAYERBOTS_PLUS_HANGOUT_H
