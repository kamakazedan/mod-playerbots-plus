#ifndef PLAYERBOTS_PLUS_DUELS_H
#define PLAYERBOTS_PLUS_DUELS_H

#include <cstdint>

// --------------------------------------------------------------------------
// Duels outside the cities.
//
// Players with nothing on stand outside the gates and duel each other to
// practise. Playerbots only ever starts one through its old RPG strategy,
// which this realm does not use, so bots never did.
//
// Every Duels.CheckSeconds, near each real player who is somewhere duels are
// allowed (outside the capitals, the dungeons and the battlegrounds),
// Duels.Chance of the time two idle random bots of the same side and about
// the same level (Duels.LevelGap) duel each other: ones hanging out, or with
// nothing on in playerbots' own terms -- not questing, grinding, travelling,
// grouped, fighting or hurt. One walks over to the other if they are not
// close (within Duels.PairRange), and challenges it; playerbots accepts and
// fights it out, and the chat module says gg if it is in the build. At most
// Duels.MaxAtOnce near anybody, and a bot does not duel again for
// Duels.BotCooldownMinutes.
//
// World thread only: the world tick.
// --------------------------------------------------------------------------

void PbpDuels_Update(uint32_t diff);

struct PbpDuelsStats
{
    uint64_t challenged = 0;   // a challenge went out
    uint64_t gaveUp     = 0;   // never got to it: one moved off, a fight, too far
    uint32_t walking    = 0;   // on the way to the one it challenges, now
};
PbpDuelsStats PbpDuels_GetStats();

#endif // PLAYERBOTS_PLUS_DUELS_H
