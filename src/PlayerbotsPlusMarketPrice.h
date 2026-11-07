#ifndef PLAYERBOTS_PLUS_MARKET_PRICE_H
#define PLAYERBOTS_PLUS_MARKET_PRICE_H

// --------------------------------------------------------------------------
// What a bot asks for something on the auction house, and what it would pay.
//
// Pure arithmetic on numbers taken from the item template, so it can be
// tested without a server. All money is in copper.
//
// The idea is the one a real player goes by:
//   - if it is already listed, ask a little under the cheapest one;
//   - if it is not, ask what it is worth: a multiple of what a vendor pays,
//     more for better quality;
//   - never ask more than someone of that level could sensibly pay (their
//     "wealth"), and never so little that a vendor would have paid more;
//   - if it came back unsold, ask less next time.
// --------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>

namespace PbpPrice
{
    constexpr uint32_t kQualities = 7;   // poor .. heirloom

    struct Settings
    {
        // What something is worth, as a percentage of its vendor price: gear
        // (weapons, armour) by quality, everything else in one figure.
        std::array<uint32_t, kQualities> gearValuePct  { 100, 250, 500, 1000, 2500, 4000, 4000 };
        uint32_t                         goodsValuePct = 300;

        // Never listed for less than this percentage of the vendor price: the
        // house keeps 5% of a sale, so below about 106% a vendor pays more.
        uint32_t floorPct = 110;

        // How much gold someone of a level has to spend: base * e^(level / growth).
        double wealthBaseCopper = 10000.0;   // 1g
        double wealthGrowth     = 10.5;      // about 2g at 10, 300g at 60, 2000g at 80

        // A single piece of gear never costs more than this share of the
        // wealth at its level; a whole stack of anything else, this share.
        std::array<uint32_t, kQualities> gearCapPct  { 2, 5, 20, 50, 120, 300, 300 };
        uint32_t                         stackCapPct = 20;

        // How far under the cheapest listing it goes, in percent.
        uint32_t undercutMinPct = 1;
        uint32_t undercutMaxPct = 5;

        // Asked this much less (percent) for every time it came back unsold.
        uint32_t relistDiscountPct = 15;
    };

    struct ItemFacts
    {
        uint32_t vendor        = 0;   // SellPrice, per one
        uint32_t quality       = 1;
        uint32_t itemLevel     = 1;
        uint32_t requiredLevel = 0;
        bool     gear          = false;   // weapon or armour
        uint32_t maxStack      = 1;
    };

    // The level an item is for: its required level, else its item level, 1..80.
    uint32_t LevelOf(ItemFacts const& item);

    // How much gold someone of `level` has to spend.
    uint64_t Wealth(Settings const& s, uint32_t level);

    // What one of it is worth by itself, with nothing on the market to go by.
    uint64_t Value(Settings const& s, ItemFacts const& item);

    // The most one of it should cost anyone of `level`.
    uint64_t Cap(Settings const& s, ItemFacts const& item, uint32_t level);

    // Below this a vendor pays more.
    uint64_t Floor(Settings const& s, ItemFacts const& item);

    // What a bot asks for one of it. `marketUnit`: the cheapest price per one
    // somebody else is asking right now, 0 if nobody is. `expiries`: how often
    // the bot's own listing of it came back unsold. `roll1`, `roll2`: random
    // in [0,1), for how far under it goes and how it rounds a guess. 0 means
    // do not list it: the market is already below what a vendor pays.
    uint64_t ListUnit(Settings const& s, ItemFacts const& item, uint64_t marketUnit, uint32_t expiries,
                      double roll1, double roll2);

    // The most a bot of `botLevel` with `spendable` copper to spare pays for a
    // piece of gear that is an upgrade for it.
    uint64_t GearBudget(Settings const& s, ItemFacts const& item, uint32_t botLevel, uint64_t spendable);

    // The most it pays per one for something its profession uses: up to
    // `overValuePct` percent of what it is worth.
    uint64_t GoodsBudgetUnit(Settings const& s, ItemFacts const& item, uint32_t overValuePct);

    // "12g 30s 5c", "45s", "7c".
    std::string MoneyText(uint64_t copper);
}

#endif // PLAYERBOTS_PLUS_MARKET_PRICE_H
