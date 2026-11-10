#ifndef PLAYERBOTS_PLUS_DUELS_H
#define PLAYERBOTS_PLUS_DUELS_H

#include <cstdint>
#include <string>

class Player;

// --------------------------------------------------------------------------
// Duels at the city gates.
//
// On a real realm people duel in one place: just outside the main gate of
// Orgrimmar or Stormwind, a crowd standing about, a pair or two going at it
// at a time, others watching and waiting for a go -- not out in the world.
// Playerbots only ever starts a duel through its old RPG strategy, which
// this realm does not use, so bots never did.
//
// A spot listed in Duels.Spots is a city hangout (PlayerbotsPlusHangout)
// outside a gate, and fills with bots like the others: from just inside the
// city too, and the ones fetched from elsewhere come in at the city's inn and
// walk out through the gate. Every Duels.CheckSeconds, while a real player is
// within Duels.WatchRange of it, Duels.Chance of the time two of the crowd
// there of a side and about a level (Duels.LevelGap) duel: one walks over to
// the other and challenges it as the Duel button does, playerbots accepts and
// fights it out, and the chat module, if it is in the build, has them say gg.
// Afterwards both get their breath back and go back to their places (the
// hangout keeps them through it). At most Duels.MaxAtOnce at a spot at a
// time, and a bot waits Duels.BotCooldownMinutes before another.
//
// With Duels.ChallengePlayerChance, one of the crowd now and then challenges
// a real player standing about there, as people at the gates do; each one
// at most once in Duels.ChallengePlayerMinutes.
//
// Two bots that talk themselves into a duel somewhere in the city (the chat
// module, through PlayerbotsPlus::GoDuel) walk out to the gates and fight it
// out there: once both are at the spot, one challenges the other, as soon as
// the spot has room for one more duel (Duels.MaxAtOnce). If the crowd there
// is at its room, two of it head off to make room for them.
//
// World thread only: the world tick.
// --------------------------------------------------------------------------

void PbpDuels_Update(uint32_t diff);

// Whether two bots could go out to a duel spot and duel there, now: two of
// playerbots' random bots of a side, both of Duels.MinLevel or more and
// within Duels.LevelGap of each other, on their own and free, not just out
// of a duel, on the map of a duel spot and near enough to walk out to it (as
// far as a spot outside the gates takes its crowd from). `where`: the spot's
// name ("outside the gates of Stormwind").
bool PbpDuels_CanGo(Player* a, Player* b, std::string& where);

// They go (see above). False when they cannot after all.
bool PbpDuels_Go(Player* a, Player* b);

struct PbpDuelsStats
{
    uint64_t challenged = 0;   // a challenge went out
    uint64_t asked      = 0;   // ...of those, to a real player
    uint64_t gaveUp     = 0;   // never got to it: one moved off, a fight, too far
    uint32_t walking    = 0;   // on the way to the one it challenges, now
    uint64_t pacts      = 0;   // pairs that talked themselves into a duel, sent out to the gates
    uint64_t pactsHeld  = 0;   // ...that got there and had it out
    uint64_t pactsLost  = 0;   // ...that never did: one went off, or did not get there in time
    uint32_t pactsNow   = 0;   // on their way, or waiting for room, now
};
PbpDuelsStats PbpDuels_GetStats();

#endif // PLAYERBOTS_PLUS_DUELS_H
