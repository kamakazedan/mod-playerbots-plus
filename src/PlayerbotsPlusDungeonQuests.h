#ifndef PLAYERBOTS_PLUS_DUNGEON_QUESTS_H
#define PLAYERBOTS_PLUS_DUNGEON_QUESTS_H

// --------------------------------------------------------------------------
// Dungeon quests, for bot dungeons (PlayerbotsPlusDungeons).
//
// Playerbots' own questing only takes quests one can do alone, so a random
// bot never holds a quest for a dungeon, and when its quest log fills up it
// clears out first exactly the quests it cannot do alone. With bot dungeons
// on, this:
//
//   * knows every quest for a normal 5-man dungeon (from the server's own
//     data, on first use): who gives it out and who takes it back, where
//     they stand, which dungeon -- and on a split one, which wing -- it is
//     done in, and its level;
//   * walks idle random bots to the givers of those quests on the continent
//     they are on, within BotDungeons.QuestRange, for quests that suit them
//     (their level, not grey, one they may take), and takes them there:
//     playerbots' own "go camp" walk gets them to the giver;
//   * keeps a bot holding one from filling its log: BotDungeons.KeepLogFree
//     slots are kept free by letting go of what a player would first --
//     failed quests, grey ones, ones for other zones, the lowest -- never a
//     dungeon quest, one done, or the one it is out doing;
//   * hands them in: a bot with one done walks to whoever takes it back, on
//     its continent, where playerbots' questing hands it in and picks the
//     reward (if that has not happened in a minute there, it is handed in
//     here, with the reward that suits it best);
//   * lets them go: one gone grey, once the bot is out of its dungeon; one
//     that BotDungeons.QuestRuns runs of its dungeon have not finished, which
//     it does not take again.
//
// World thread only.
// --------------------------------------------------------------------------

#include <cstdint>
#include <vector>

class Player;

// A dungeon `bot` holds quests for, and how many: unfinished, not grey, not
// given up on, done in that dungeon finder dungeon (on a split dungeon, the
// wing), and one its level may queue for.
struct PbpDungeonQuestHold
{
    uint32_t lfgDungeon = 0;
    uint32_t quests     = 0;
};
std::vector<PbpDungeonQuestHold> PbpDungeonQuests_Holds(Player* bot);

// A dungeon quest done and not yet handed in, to someone on the continent the
// bot is on: it goes and hands that in before it queues again.
bool PbpDungeonQuests_ToHandIn(Player* bot);

// The dungeon quests `bot` holds for map `mapId`, done or not: what a run
// there could finish (for PbpDungeonQuests_RunEnded).
std::vector<uint32_t> PbpDungeonQuests_ForMap(Player* bot, uint32_t mapId);

// A run in dungeon finder dungeon `lfgDungeon` ended for `bot`. Each quest of
// `held` (what it held there when the run began) still not done, and done in
// that dungeon, has had one more try; one that has had BotDungeons.QuestRuns
// is let go, and not taken again.
void PbpDungeonQuests_RunEnded(Player* bot, uint32_t lfgDungeon, std::vector<uint32_t> const& held);

// The bot is walking to a quest giver or hand-in for this, or holding its
// quests: whatever else wants it leaves it be (the walk only).
bool PbpDungeonQuests_Walking(Player* bot);

// One of the dungeon quests this knows: a quest log keeper keeps it.
bool PbpDungeonQuests_IsOne(uint32_t questId);

// World tick, from PbpDungeons_Update while bot dungeons are on.
void PbpDungeonQuests_Update(uint32_t diff);

// Bot dungeons switched off: walks stop, and the quests taken stay where they
// are (playerbots may clear them out when a log fills, as it always would).
void PbpDungeonQuests_Stop();

struct PbpDungeonQuestsStats
{
    uint32_t quests     = 0;   // dungeon quests known
    uint32_t givers     = 0;   // places they are given out
    uint64_t sent       = 0;   // bots sent to take some
    uint64_t taken      = 0;   // quests taken
    uint64_t walksLost  = 0;   // walks given up (too long, or the bot was wanted elsewhere)
    uint64_t handIns    = 0;   // bots sent to hand some in
    uint64_t handedIn   = 0;   // dungeon quests handed in (by anyone's hand)
    uint64_t made       = 0;   // log slots freed for them
    uint64_t greyGone   = 0;   // let go, grey
    uint64_t triesGone  = 0;   // let go, too many runs
    uint32_t walking    = 0;   // on their way now
};
PbpDungeonQuestsStats PbpDungeonQuests_GetStats();

#endif // PLAYERBOTS_PLUS_DUNGEON_QUESTS_H
