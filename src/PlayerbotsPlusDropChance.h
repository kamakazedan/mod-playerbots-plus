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

    // Where things drop from: a creature's loot (`many`: how many of those
    // creatures the world has), or a chest's (`many` 0).
    struct Source
    {
        Table const* table = nullptr;
        uint32_t     many  = 0;
    };

    struct Drop
    {
        float best    = 0.0f;   // the most any one source gives it, per kill or chest
        float typical = 0.0f;   // per kill of a creature that can drop it, over all the world has of them; 0: none there
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
}

#endif // PLAYERBOTS_PLUS_DROP_CHANCE_H
