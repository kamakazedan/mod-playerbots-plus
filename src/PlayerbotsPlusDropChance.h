#ifndef PLAYERBOTS_PLUS_DROP_CHANCE_H
#define PLAYERBOTS_PLUS_DROP_CHANCE_H

// --------------------------------------------------------------------------
// How likely something is to drop, worked out from the world's own loot
// tables the way the core rolls them (LootTemplate::Process,
// LootGroup::Roll), for the market to price a rare thing higher
// (PlayerbotsPlus.Market.Price.Rarity*).
//
// No game types in here, so it can be tested without a server
// (tests/drops_test.cpp). PlayerbotsPlusDrops reads the tables.
//
// How the core rolls a table:
//   - a row outside any group is rolled by itself, at its chance;
//   - each group is rolled once and gives at most one of its rows: the rows
//     with a chance of their own first, in order, then -- if none of those
//     came up -- one of the rows with no chance (0), all alike;
//   - a row that is another table (a reference) has that table rolled
//     MaxCount times, whole, when it comes up.
// --------------------------------------------------------------------------

#include <cstdint>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace PbpDrops
{
    struct Row
    {
        uint32_t item      = 0;
        uint32_t reference = 0;      // another table to roll instead, 0 for none
        float    chance    = 0.0f;   // percent
        uint8_t  group     = 0;      // 0: rolled by itself
        uint8_t  maxCount  = 1;      // a reference: how many times its table is rolled
    };

    using Table   = std::vector<Row>;
    using Tables  = std::unordered_map<uint32_t, Table>;   // by entry
    using Chances = std::unordered_map<uint32_t, double>;  // item: the chance (0..1) it comes out at least once

    // Rolls tables on paper: what comes out of one roll of a table, item by
    // item. A reference table is worked out once and kept.
    class Roller
    {
    public:
        explicit Roller(Tables const& references) : _references(references) {}

        Chances Roll(Table const& table);

    private:
        Chances const& Reference(uint32_t entry, uint32_t depth);
        Chances Roll(Table const& table, uint32_t depth);

        Tables const&                         _references;
        std::unordered_map<uint32_t, Chances> _known;
        std::unordered_set<uint32_t>          _working;   // a table that refers back to itself gives nothing more
    };

    // A map a thing is found on: none, or no one map.
    constexpr uint32_t kNoMap    = 0xFFFFFFFFu;
    constexpr uint32_t kMapsMany = 0xFFFFFFFEu;

    // Where things drop from: a creature's loot (`many`: how many of those
    // creatures the world has), or a chest's (`chest`, `many` 0).
    struct Source
    {
        Table const* table = nullptr;
        uint32_t     many  = 0;
        uint32_t     id    = 0;        // the caller's name for it (a creature's loot id), for From::top
        float        level = 0.0f;     // a creature's level (over all of them, by how many)
        uint32_t     map   = kNoMap;   // the map most of those creatures are on; kNoMap: none out in the world
        bool         chest = false;    // a chest or a fishing hole rather than a creature
    };

    // Where something drops from among the creatures, for someone asking
    // where to find it: how many kinds, how many of them the world has, what
    // level they are, and the likeliest few.
    struct From
    {
        uint32_t kinds    = 0;      // creature sources that can give it
        uint32_t many     = 0;      // ...how many such creatures the world has
        double   levelSum = 0.0;    // ...their level times how many of them, added up
        double   levelSq  = 0.0;    // ...and of its square: with levelSum, the middle and the spread
        uint32_t top[3]   = {};     // the source ids likeliest to give it, likeliest first (0: none)
        float    topChance[3] = {}; // ...the chance a kill of each gives it
        uint32_t map      = kNoMap; // the map nine in ten of them are on, by how many (a dungeon's);
                                    // kMapsMany: no one map; kNoMap: none out in the world
        bool     chests   = false;  // a chest or a fishing hole gives it too
    };

    struct Drop
    {
        float best    = 0.0f;   // the most any one source gives it, per kill or chest
        float typical = 0.0f;   // per kill of a creature that can drop it, over all the world has of them; 0: none there
        From  from;
    };

    // How likely each item is to drop from `sources`. A container one gives
    // (a lockbox, a clam: `containers`, by the item that holds them) counts
    // with what is inside, at its own chance times theirs.
    std::unordered_map<uint32_t, Drop> Drops(std::vector<Source> const& sources, Tables const& references,
                                             Tables const& containers);

    // The chance that says how rare it is: per kill, typically, else the best
    // any chest gives (a thing from chests alone).
    inline double Rarity(Drop const& d)
    {
        return d.typical > 0.0f ? d.typical : d.best;
    }

    // "1 in 2,000": how often, for a person. 0 when it never drops.
    uint64_t OneIn(double chance);

    // The levels most of the creatures that drop it are (the middle, give or
    // take the spread, by how many of them the world has), at least 1. False
    // when none of them is out in the world.
    bool Levels(From const& from, uint32_t& lo, uint32_t& hi);
}

#endif // PLAYERBOTS_PLUS_DROP_CHANCE_H
