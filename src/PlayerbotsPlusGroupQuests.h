#ifndef PLAYERBOTS_PLUS_GROUP_QUESTS_H
#define PLAYERBOTS_PLUS_GROUP_QUESTS_H

// --------------------------------------------------------------------------
// Group quests (PlayerbotsPlus.GroupQuests.*): random bots take the quests
// marked Group, or that suggest a party, and get one together to do them,
// the way players do.
//
// Playerbots' own questing takes only quests one can do alone: it never
// picks up a group quest, empties a full quest log starting with them, and a
// bot on its own never attacks an elite (in a party that follows its leader,
// it does). So, with this on:
//
//   * it knows every group quest done out in the world (from the server's own
//     data, on first use): who gives it out and takes it back, where they
//     stand, its zone, its level and the party it suggests. One with an
//     objective a bot cannot do -- an escort, a spell to cast, an object to
//     use, somewhere to find, someone to talk to, a timer, a player to kill --
//     or with no map marker to go to, is left out;
//   * idle random bots walk (playerbots' own "go camp") to the givers of
//     those quests for the zone they are in, within GroupQuests.QuestRange,
//     and take the ones that suit them: not grey, at most three levels up,
//     one the game lets them take. They keep them while the quest log fills
//     (GroupQuests.KeepLogFree), and playerbots' questing does not go off to
//     do them alone;
//   * a bot holding one in the zone asks for help with it in General -- one
//     quest at a time, the one the most others there hold. Others in the zone
//     who hold it, not done, answer and come along: the asker leads, up to
//     the party the quest suggests (GroupQuests.PartySize where it suggests
//     none). Short, it asks again every GroupQuests.AskMinutes for
//     GroupQuests.AskForMinutes, questing as ever in between, and anyone
//     asking about it then joins it; then it goes with whoever came;
//   * the party meets where its leader is (GroupQuests.MeetMinutes), and
//     does the quest. The leader goes for what it is for (what it asks to be
//     killed, or what drops what it asks for, from the server's quest item
//     lists): walks up to it in sight and pulls it, the others following
//     and fighting with it, elites too -- playerbots' own choice of a target
//     skips an elite unless everyone's bags have room; waits where it
//     stands when it is not about; leaves it to playerbots' questing when
//     nothing is known of where they stand. Then every group quest of the
//     zone two of them hold, the rest leaving as theirs run out; then it
//     breaks up. A quest done is never gone back to;
//   * a try fails when the party wipes, when nothing moves forward for
//     GroupQuests.NoProgressMinutes once it is at the quest's place, or when
//     it is not there after GroupQuests.WalkMinutes on its way.
//     GroupQuests.Attempts failed tries and it is let go, and not taken
//     again;
//   * nobody came at all: it tries it alone, as many tries, and still
//     answers anyone who asks about it;
//   * done, it is handed in where it is taken back (playerbots' questing
//     first; after a minute there, here, with the reward that suits it).
//     One gone grey is let go.
//
// What the bots say about it is mod-ollama-chat's: the asks and answers are
// raised as BotEvents (PlayerbotsPlus.h). World thread only.
// --------------------------------------------------------------------------

#include "PlayerbotsPlusReport.h"
#include "ScriptMgr.h"

#include <cstdint>
#include <string>
#include <vector>

class Player;

// A living member of a party getting together for a group quest, or at one
// (alone too), is not teleported away from it. Playerbots teleports a random
// bot that levels up to somewhere for its new level
// (Playerbots.AutoTeleportForLevel), a grouped one only if it leads its
// group: the party's leader, who levels as often as the rest. A step aside on
// its own map (under 300 yards: a stuck bot put back on its feet) and the
// dead's way to a graveyard are left alone. Runs on whatever thread teleports
// the bot: it reads only a list the world tick publishes, under a mutex.
class PbpGroupQuestsTeleportScript : public PlayerScript
{
public:
    PbpGroupQuestsTeleportScript() : PlayerScript("PbpGroupQuestsTeleportScript", { PLAYERHOOK_ON_BEFORE_TELEPORT }) {}
    bool OnPlayerBeforeTeleport(Player* player, uint32 mapid, float x, float y, float z, float orientation,
                                uint32 options, Unit* target) override;
};

// World tick. Call it with the module or the feature off too: then the
// parties it made let their members go and the walks stop.
void PbpGroupQuests_Update(uint32_t diff);

// In a party for a group quest, getting one together, or walking for one:
// whatever else wants the bot (a dungeon queue, a city hangout, a walk for a
// dungeon quest) leaves it be.
bool PbpGroupQuests_Busy(Player* bot);

// One of the group quests this knows: a quest log keeper keeps it.
bool PbpGroupQuests_IsOne(uint32_t questId);

// Every party and count, for the full report (PlayerbotsPlusReport).
std::vector<std::string> PbpGroupQuests_Status();

// .pbp groupquests, and the line in .pbp status: the parties and how they
// stand, how they have gone, and what looks wrong -- parties going and no
// quest done, most walks given up. `full`: the quests and walks too.
PbpBrief PbpGroupQuests_Brief(bool full);

struct PbpGroupQuestsStats
{
    uint32_t quests     = 0;   // group quests known
    uint32_t givers     = 0;   // places they are given out
    uint32_t partiesNow = 0;   // parties now, getting together or doing one
    uint64_t sent       = 0;   // bots sent to take some
    uint64_t taken      = 0;   // quests taken
    uint64_t handIns    = 0;   // bots sent to hand one in
    uint64_t handedIn   = 0;   // group quests handed in, by anyone's hand
    uint64_t walksLost  = 0;   // walks given up
    uint64_t asks       = 0;   // asks for help in General
    uint64_t answers    = 0;   // ...answered by one who came along
    uint64_t parties    = 0;   // parties that got together and went
    uint64_t solos      = 0;   // ...and bots that went alone, nobody having come
    uint64_t done       = 0;   // quests a party (or one alone) did
    uint64_t failedTries = 0;  // tries that failed
    uint64_t gaveUp     = 0;   // quests let go after their tries
    uint64_t greyGone   = 0;   // ...or gone grey
    uint64_t made       = 0;   // log slots freed for them
    uint64_t keptAt     = 0;   // teleports away from one turned down (a level-up, mostly)
};
PbpGroupQuestsStats PbpGroupQuests_GetStats();

#endif // PLAYERBOTS_PLUS_GROUP_QUESTS_H
