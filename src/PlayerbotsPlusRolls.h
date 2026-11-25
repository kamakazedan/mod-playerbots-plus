#ifndef PLAYERBOTS_PLUS_ROLLS_H
#define PLAYERBOTS_PLUS_ROLLS_H

// --------------------------------------------------------------------------
// Enchanters roll Disenchant.
//
// Playerbots has an enchanter roll Disenchant (Playerbots.LootRollDisenchant)
// only on gear that binds when picked up. For what binds when equipped --
// nearly everything the trash of a dungeon drops -- its judgement of the
// piece is "sell it", and it rolls Greed. An enchanter in a group clicks
// Disenchant on a green it will not wear.
//
// So (Rolls.Disenchant) when a roll starts that offers Disenchant, a bot
// that is an enchanter with the skill for the piece, and would only sell it
// (playerbots' own judgement), has its vote cast here: Disenchant. Only
// where playerbots would have rolled Greed for it anyway (its
// LootGreedRollLevel and LootRollDisenchant both on), and on gear that
// binds when equipped only up to Rolls.DisenchantBoEMaxQuality: a rare one
// is worth more whole.
//
// It is the game's own vote (Group::CountRollVote), cast from the world tick
// the roll starts in. Playerbots casts its own from the bot's AI a moment
// later; where it got there first its Greed stands (a vote is never
// changed), and a bot that wins the piece so breaks it down itself later
// (PlayerbotsPlusCrafting), for the same dust.
//
// What a Disenchant roll gives its winner is loot, and so its own to sell
// (PbpLooted_DisenchantWon): the game stores it with no loot hook, and it
// was taken for a hand-out, spoiling any dust of its own it joined.
//
// The packet hook runs on whatever thread sends the packet (a roll may start
// on a map thread, a bot looting): it reads the packet and who it went to,
// and notes it under a mutex. The world tick does the rest.
// --------------------------------------------------------------------------

#include "PlayerbotsPlusReport.h"
#include "ScriptMgr.h"

#include <cstdint>

class WorldPacket;
class WorldSession;

class PbpRollsPacketScript : public ServerScript
{
public:
    PbpRollsPacketScript() : ServerScript("PbpRollsPacketScript", { SERVERHOOK_ON_PACKET_SENT }) {}
    void OnPacketSent(WorldSession* session, WorldPacket const& packet) override;
};

void PbpRolls_Update();   // world tick, every tick: a vote wants casting before the bot's own

struct PbpRollsStats
{
    uint64_t offered   = 0;   // rolls offering Disenchant that went to an enchanter bot
    uint64_t voted     = 0;   // ...Disenchant cast for it
    uint64_t late      = 0;   // ...it had voted already, or the roll was over
    uint64_t wears     = 0;   // ...it would wear the piece, or keep it: left to it
    uint64_t unskilled = 0;   // ...not enough Enchanting for the piece
    uint64_t whole     = 0;   // ...worth more whole (binds when equipped, too good)
    uint64_t won       = 0;   // Disenchant rolls a bot won: the dust counted as its loot
    uint64_t dropped   = 0;   // notes dropped, too many at once
};
PbpRollsStats PbpRolls_GetStats();

// .pbp status and the report.
PbpBrief PbpRolls_Brief(bool full);

#endif // PLAYERBOTS_PLUS_ROLLS_H
