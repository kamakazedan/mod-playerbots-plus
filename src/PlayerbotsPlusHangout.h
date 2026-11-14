#ifndef PLAYERBOTS_PLUS_HANGOUT_H
#define PLAYERBOTS_PLUS_HANGOUT_H

#include <cstdint>
#include <string>
#include <vector>

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
//   * mounted, if it can ride and its place is out in the open (playerbots
//     puts an idle random bot on its best mount); one whose place is under
//     a roof gets off there;
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
// it go at once -- except a duel at the gates of a city (Duels.Spots, see
// PlayerbotsPlusDuels.h), which is what the crowd there is for: its AI fights
// it out, gets its breath back, and it goes back to its place. A spot like
// that also takes in people from just inside the city (bots fetched from
// elsewhere arrive at the city's inn and walk out through the gate). When
// its time is up it stands and goes about its business, and does not hang
// out again for Hangout.CooldownMinutes. Switching it off lets everyone go at
// once; if this stopped running altogether, the hold would run out within
// five seconds.
//
// City life (PlayerbotsPlus.CityLife.*): near a real player the crowd is not
// a painting. Each settled bot gets a temper (stays put, shuffles, can't keep
// still) and, free and near someone real, now and then takes a step, hops,
// looks round, walks off to a banker, an auctioneer or a mailbox, or goes
// fishing at the water's edge; a few wander over to look at someone's rare
// mount; someone starts a dance and others join in (a real player dancing
// too); some of those fetched fly in to the city's flight master, and some
// of those leaving fly out. What a chat module does (talking, emoting, two
// bots chatting) it says through PbpHangout_Busy, _Spoke and _Gather.
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
// on a bench", "standing about, watching the duels"), whether it is AFK right
// now, and whether it came back from being AFK in the last few minutes. False
// when it is not settled at one (on its way, or in a duel).
bool PbpHangout_Get(Player* bot, std::string& spot, std::string& doing, bool& away, bool& justBack);

// Who is at a spot right now: the bots settled there, and how many of those
// that came from it are in a duel.
void PbpHangout_At(const std::string& spotKey, std::vector<Player*>& settled, uint32_t& duelling);

// A bot that is to step to x, y, z: out of a fire (PlayerbotsPlusFire.h), or
// out of someone's way (PlayerbotsPlusSpace.h).
enum class PbpHangoutMove : uint8_t
{
    NotHere,   // not hanging out (or in a duel at the gates, its own AI running): the caller moves it
    Moved,     // at its place, or on the last steps to it: x, y, z is its place from now on, and it is
               // walking there, to stand, sit or ride the way it did, for the rest of its time
    OnItsWay   // on its way to a spot, only passing by: left to get there
};
PbpHangoutMove PbpHangout_MoveTo(Player* bot, float x, float y, float z);

// Sends `bot` out to the duel spot `spotKey` to fight someone there
// (PlayerbotsPlusDuels): whatever it was doing -- a visit to another spot,
// or to this one -- ends, and it walks over now and stands there, not sat,
// mounted or AFK. False when it cannot: not a bot that may hang out, wanted,
// not on the spot's map, or nowhere to stand there just now.
bool PbpHangout_SendToDuel(Player* bot, const std::string& spotKey);

// Room at a spot for `count` more: if the crowd there (none of `keep`
// counted) would be over its room with them, that many of those settled
// there -- not in a duel, not on their way to one -- go about their
// business.
void PbpHangout_MakeRoom(const std::string& spotKey, uint32_t count, const std::vector<uint64_t>& keep);

// Settled at `spotKey` right now: standing about there, not on its way, not
// in a duel.
bool PbpHangout_SettledAt(Player* bot, const std::string& spotKey);

// Hanging out, or on its way to: whatever else wants the bot leaves it be.
bool PbpHangout_Has(Player* bot);

// City life, as a chat module tells it (PlayerbotsPlus.h's CityBusy,
// CitySpoke, CityGather): talking or emoting for `seconds` (0: done), it is
// left where it is; someone spoke in /say, so some near them look round;
// these are about to talk, so they stand together first.
void PbpHangout_Busy(Player* bot, uint32_t seconds);
void PbpHangout_Spoke(Player* speaker);
void PbpHangout_Gather(std::vector<Player*> const& bots, uint32_t seconds);

// How full each city is: its spots taken together, by the city their names
// end with ("outside the bank in Thunder Bluff" -> "Thunder Bluff").
struct PbpHangoutCity
{
    std::string name;
    uint32_t    there    = 0;   // settled at one of its spots, or in a duel at its gates
    uint32_t    onTheWay = 0;
    uint32_t    room     = 0;   // its spots' room together, as given out now
    uint32_t    share    = 0;   // ...its share of the places before a full spot's went elsewhere
    uint32_t    hot      = 0;   // of `there`, at its hot spots
    uint32_t    full     = 0;   // its spots found full just now
};
std::vector<PbpHangoutCity> PbpHangout_Cities();

// One line per spot for .pbp hangouts: who is there and on the way, its room
// (and whether it was found full), and how the places looked at for it went
// -- what was turned down, and why.
std::vector<std::string> PbpHangout_SpotReport();

struct PbpHangoutStats
{
    uint64_t sent      = 0;   // set off for a spot
    uint64_t settled   = 0;   // got there and settled
    uint64_t gaveUp    = 0;   // never got there:
    uint64_t lostNoRoom    = 0;   // ...fetched, and no place found for it at the spot
    uint64_t lostSlow      = 0;   // ...on the way longer than Hangout.TravelSeconds
    uint64_t lostBusy      = 0;   // ...playerbots gave it something else to do on the way
    uint64_t lostElsewhere = 0;   // ...wanted, grouped, off the map or out of the city on the way
    uint64_t lostLanding   = 0;   // ...fetched, and never landed
    uint64_t movedOn   = 0;   // time up, off again
    uint64_t cutShort  = 0;   // a group, a fight, moved away...
    uint64_t cameBack  = 0;   // AFK, then talked to
    uint64_t broughtIn = 0;   // fetched from elsewhere (Hangout.BringIn)
    uint64_t sentBack  = 0;   // ...and gone back there when done
    uint64_t duels     = 0;   // duels fought at the gates without leaving the crowd
    uint64_t foundFull = 0;   // times a spot was found full (its room went elsewhere for a while)
    uint64_t redirected = 0;  // fetched, no room at the spot: on to another spot in the city
    // City life (CityLife.*)
    uint64_t steps     = 0;   // stepped to another place at their spot
    uint64_t turns     = 0;   // looked round (someone near, someone speaking)
    uint64_t hops      = 0;   // little bouts of hopping
    uint64_t gawks     = 0;   // rare mounts looked at
    uint64_t gawkers   = 0;   // ...by this many, all told
    uint64_t dances    = 0;   // dance parties started
    uint64_t danced    = 0;   // ...joined, someone's own dance too
    uint64_t gathered  = 0;   // walked over to who they were about to talk to
    uint64_t errands   = 0;   // off to a banker, an auctioneer or a mailbox
    uint64_t errandsDone = 0; // ...and got there
    uint64_t flewIn    = 0;   // fetched by flight path
    uint64_t landed    = 0;   // ...and landed in the city
    uint64_t flewOut   = 0;   // left by flight path
    uint64_t fished    = 0;   // went fishing at the water's edge
    uint64_t casts     = 0;   // ...lines cast
    uint32_t fishPlaces = 0;  // places to fish from, found so far
    uint32_t services  = 0;   // bankers, auctioneers and mailboxes found so far
    uint32_t hanging   = 0;   // there now
    uint32_t away      = 0;   // ...of whom AFK
    uint32_t duelling  = 0;   // ...of whom in a duel or just out of one
    uint32_t onTheWay  = 0;
    uint32_t spots     = 0;
    uint32_t withInn   = 0;   // spots with an inn their fetched bots arrive at (of those looked at so far)
};
PbpHangoutStats PbpHangout_GetStats();

#endif // PLAYERBOTS_PLUS_HANGOUT_H
