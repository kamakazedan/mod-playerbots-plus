#ifndef PLAYERBOTS_PLUS_DUNGEONS_H
#define PLAYERBOTS_PLUS_DUNGEONS_H

// --------------------------------------------------------------------------
// Bot dungeons (PlayerbotsPlus.BotDungeons.*): random bots run dungeons with
// nobody playing among them, the way players do -- through the dungeon finder.
//
// Playerbots only ever queues a random bot for a dungeon a player has queued
// for, and mod-dungeon-clear only starts a clear when a player (or a GM) says
// so. So, every BotDungeons.CheckSeconds, while a player is online and the
// server has the room:
//
//   * a party is picked: a tank, a healer and three to do the damage, of one
//     side, of a level for the dungeon, those holding quests for it first
//     (PlayerbotsPlusDungeonQuests gets them those), and queued for it, each
//     on its own, as a player queues. Whoever the dungeon finder puts
//     together goes: a player matched in makes it a player's group, and
//     what happens there is theirs. With nobody free to heal, a priest,
//     druid, shaman or paladin who does the damage heals instead
//     (BotDungeons.HealerStepIn): from level 40 on its second spec, made
//     playerbots' premade healing one, and back on its first after;
//   * a group of bots the dungeon finder made, with no player in it -- this
//     one's, or one playerbots queued for a dungeon a player wanted -- is
//     taken on once it is in its dungeon: stragglers are brought in, the
//     wander-about strategies (grind, rpg) are taken off them, and
//     mod-dungeon-clear runs the dungeon (DcBotRun), silently: nobody in the
//     group to talk to;
//   * a member that dies lies where it fell until it is rezzed, the tank
//     too: playerbots releases a dead bot with no player in its group at
//     once, and a tank out of its dungeon ends the clear. Its AI is held
//     while it lies there, and a rez offered to it is taken, as a player
//     clicks Accept. mod-dungeon-clear sends a healer over once the fight is
//     done, and calls the run off if nobody left can rez;
//   * when the clear is done (and the last boss looted), or it fails (the
//     party wiped, nothing moved for BotDungeons.NoProgressMinutes, the
//     BotDungeons.MaxRunMinutes cap), everyone is brought back to life,
//     leaves the group -- which takes them back where they queued from --
//     and is unbound from the dungeon, with its strategies back. They queue
//     again after a cooldown, a longer one after a failure;
//   * bots that log in inside a dungeon they are no longer grouped for (the
//     server went down mid-run) are sent back out.
//
// Built only with mod-dungeon-clear (DcBotRun.h, DC_BOT_RUN_API); without it
// everything here is a stub and the setting says so. World thread only.
// --------------------------------------------------------------------------

#if __has_include("DcBotRun.h")
#include "DcBotRun.h"
#endif

#if defined(DC_BOT_RUN_API) && DC_BOT_RUN_API >= 1
#define PBP_BOT_DUNGEONS 1
#else
#define PBP_BOT_DUNGEONS 0
#endif

#include "PlayerbotsPlusReport.h"
#include "ScriptMgr.h"

#include <cstdint>
#include <string>
#include <vector>

class Player;

#if PBP_BOT_DUNGEONS
// The dungeon finder's offer of a group ("your dungeon is ready"), as it goes to
// each member. Playerbots says no for a bot that is fighting or dead, and one
// no ends the offer for all five -- out questing, one of them nearly always is.
// So this notes each offer (who, which), and the world tick says yes for the
// bots this queued in the same world tick the offer is made, before their own
// AI sees it. OnPacketSent fires for a bot's session too (CanPacketSend does
// not). Runs on whatever thread sends the packet: it only notes, under a mutex.
class PbpDungeonsPacketScript : public ServerScript
{
public:
    PbpDungeonsPacketScript() : ServerScript("PbpDungeonsPacketScript", { SERVERHOOK_ON_PACKET_SENT }) {}
    void OnPacketSent(WorldSession* session, WorldPacket const& packet) override;
};

// A living member of a run is not taken out of its dungeon while the run is
// gathering or clearing. Playerbots teleports a random bot that levels up to
// somewhere for its new level (Playerbots.AutoTeleportForLevel), a grouped
// one only if it leads the group -- in a bot dungeon, the tank -- and a tank
// out of its dungeon ends the clear: every early "Left the dungeon" was that.
// Moving about inside the dungeon, and the dead's way to a graveyard, are
// left alone. Runs on whatever thread teleports the bot: it reads only a list
// the world tick publishes, under a mutex.
class PbpDungeonsTeleportScript : public PlayerScript
{
public:
    PbpDungeonsTeleportScript() : PlayerScript("PbpDungeonsTeleportScript", { PLAYERHOOK_ON_BEFORE_TELEPORT }) {}
    bool OnPlayerBeforeTeleport(Player* player, uint32 mapid, float x, float y, float z, float orientation,
                                uint32 options, Unit* target) override;
};
#endif

// The dungeon finder's normal dungeons (not heroics, raids or holiday ones):
// what the planner queues for and what dungeon quests are done in.
struct PbpLfgDungeon
{
    uint32_t    id = 0;
    uint32_t    map = 0;
    std::string name;            // "Scarlet Monastery - Library", as the dungeon finder says it
    uint32_t    minLevel = 0;
    uint32_t    maxLevel = 0;
    uint32_t    targetLevel = 0;
};
std::vector<PbpLfgDungeon> const& PbpDungeons_LfgDungeons();
PbpLfgDungeon const* PbpDungeons_LfgDungeon(uint32_t id);

// World tick. Call it with the module or the feature off too: then nobody new
// is queued and every queue it made is left, while the runs going on finish.
void PbpDungeons_Update(uint32_t diff);

// In the dungeon finder's queue because of this, or in a run it looks after:
// whatever else wants the bot leaves it be.
bool PbpDungeons_Busy(Player* bot);

// Every queue, run and count, for the full report (PlayerbotsPlusReport).
std::vector<std::string> PbpDungeons_Status();

// .pbp dungeons, and the line in .pbp status: how many are running and in
// the queue, how they have gone, and what looks wrong -- a run with no
// progress for ten minutes, one still getting everyone in, a party queued
// half an hour, more runs failed than cleared, most quest walks given up.
// `full`: by level band, the longest runs, the last results and the
// dungeon quests too.
PbpBrief PbpDungeons_Brief(bool full);

// The memory this machine has available for new work, in MB: /proc/meminfo's
// MemAvailable, which BotDungeons.MinFreeMemoryMB is held to. In a container
// that is the host's (or the VM's) memory, not a limit set on the container.
// -1 where there is no /proc/meminfo to read (not Linux).
int64_t PbpDungeons_FreeMemoryMb();

// .pbp dungeons bands: each level band's dungeons (BotDungeons.LevelBands),
// with the runs it has now and its share. The lines to print.
std::vector<std::string> PbpDungeons_Bands();

// .pbp dungeons send <dungeon>: a party for that dungeon (its dungeon finder
// id, or part of its name), queued now, whatever the checks that space runs
// out would say, quest or no quest. The line to print.
std::string PbpDungeons_Send(std::string const& which);

struct PbpDungeonsStats
{
    uint64_t queued    = 0;   // parties put in the queue
    uint64_t gaveUp    = 0;   // ...that waited too long and left it
    uint64_t together  = 0;   // ...that queued again as a party, after waiting alone
    uint64_t cameApart = 0;   // ...that could not, someone gone (with a player, in a fight elsewhere)
    uint64_t leftNobody = 0;  // ...that left it when the last player logged off
    uint64_t runs      = 0;   // groups of bots taken on in their dungeon
    uint64_t cleared   = 0;   // ...that cleared it
    uint64_t failed    = 0;   // ...that did not
    uint64_t handedOver = 0;  // ...that a player joined
    uint64_t sentOut   = 0;   // bots found in a dungeon with no group for it, and sent out
    uint64_t keptIn    = 0;   // members of a run kept in its dungeon when something teleported them out
    uint64_t keptDown  = 0;   // members of a run that died, left where they fell for a rez
    uint64_t rezzed    = 0;   // ...rezzes taken for them, as a player clicks Accept
    uint64_t offers    = 0;   // the dungeon finder's offers said yes to for a bot it queued
    uint64_t rejoined  = 0;   // bots that dropped out of the queue, put back in
    uint64_t stepIns   = 0;   // parties queued with one who does the damage to heal (HealerStepIn)
    uint64_t respecs   = 0;   // ...that healed on a second spec
    uint64_t dualSpecs = 0;   // ...that learned dual spec for it
    uint32_t queuedNow = 0;   // parties in the queue now
    uint32_t runsNow   = 0;   // runs going on now
};
PbpDungeonsStats PbpDungeons_GetStats();

#endif // PLAYERBOTS_PLUS_DUNGEONS_H
