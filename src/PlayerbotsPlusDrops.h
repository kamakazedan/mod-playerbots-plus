#ifndef PLAYERBOTS_PLUS_DROPS_H
#define PLAYERBOTS_PLUS_DROPS_H

// --------------------------------------------------------------------------
// How likely each thing is to drop, worked out once at startup from the world
// database's own loot tables -- creatures', chests' and the containers they
// give, with every reference table they use -- for the market to price a
// rare thing higher (PlayerbotsPlus.Market.Price.Rarity*). The sums are
// PlayerbotsPlusDropChance's.
//
// World thread.
// --------------------------------------------------------------------------

#include <cstdint>

// Once, at startup: the tables are read and the sums done (a second or so).
void PbpDrops_Load();

// How rare it is: its chance to drop from a kill, percent -- per kill of a
// creature that can drop it, over all the world has of them -- or, for a
// thing only chests give, the best one's. 0 when nothing drops it.
double PbpDrops_ChancePct(uint32_t itemId);

// The most any one creature or chest gives it, percent.
double PbpDrops_BestPct(uint32_t itemId);

// Where it drops from (PlayerbotsPlus::DropSourcesOf).
struct PbpDropsFrom
{
    uint32_t kinds    = 0;        // creature loot tables that give it
    uint32_t spawns   = 0;        // how many of those creatures the world has
    uint32_t minLevel = 0;        // the levels most of them are
    uint32_t maxLevel = 0;
    uint32_t creature[3]  = {};   // the likeliest, commonest of each loot table first (0: none)
    double   chancePct[3] = {};   // ...the chance a kill of it gives it, percent
    double   bestPct  = 0.0;      // the most any creature or chest gives it, percent
    int32_t  onlyMap  = -1;       // the map nine in ten of those creatures are on (a dungeon's); -1: none is
    bool     chests   = false;    // a chest or a fishing hole gives it too
};
bool PbpDrops_Sources(uint32_t itemId, PbpDropsFrom& out);

struct PbpDropsStats
{
    bool     loaded  = false;
    uint32_t items   = 0;   // things anything drops
    uint32_t sources = 0;   // creatures' and chests' loot tables looked through
    uint32_t ms      = 0;   // how long it took
};
PbpDropsStats PbpDrops_GetStats();

#endif // PLAYERBOTS_PLUS_DROPS_H
