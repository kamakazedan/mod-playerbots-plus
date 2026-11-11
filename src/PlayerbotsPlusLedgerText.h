#ifndef PLAYERBOTS_PLUS_LEDGER_TEXT_H
#define PLAYERBOTS_PLUS_LEDGER_TEXT_H

// --------------------------------------------------------------------------
// The economy ledger's words and sums, with no game types in them so they
// can be tested without a server (tests/ledger_test.cpp): what kind of thing
// an item is, by the class, subclass and quality its template gives it; the
// day a moment falls on; and the report .pbp economy prints.
//
// The ledger itself, which takes the numbers, is PlayerbotsPlusLedger.
// --------------------------------------------------------------------------

#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace PbpLedger
{
    // ItemTemplate's classes and subclasses, as numbers. PlayerbotsPlusLedger
    // checks them against the game's own enums when it is compiled.
    namespace ItemClass
    {
        constexpr uint32_t Consumable = 0;
        constexpr uint32_t Weapon     = 2;
        constexpr uint32_t Gem        = 3;
        constexpr uint32_t Armor      = 4;
        constexpr uint32_t TradeGoods = 7;
    }
    namespace TradeSub
    {
        constexpr uint32_t Parts         = 1;
        constexpr uint32_t Jewelcrafting = 4;
        constexpr uint32_t Cloth         = 5;
        constexpr uint32_t Leather       = 6;
        constexpr uint32_t MetalStone    = 7;
        constexpr uint32_t Meat          = 8;
        constexpr uint32_t Herb          = 9;
        constexpr uint32_t Elemental     = 10;
        constexpr uint32_t Enchanting    = 12;
    }
    namespace ConsumableSub
    {
        constexpr uint32_t Potion      = 1;
        constexpr uint32_t Elixir      = 2;
        constexpr uint32_t Flask       = 3;
        constexpr uint32_t Scroll      = 4;
        constexpr uint32_t Food        = 5;
        constexpr uint32_t Enhancement = 6;
        constexpr uint32_t Bandage     = 7;
    }
    namespace Quality
    {
        constexpr uint32_t Poor     = 0;
        constexpr uint32_t Uncommon = 2;
        constexpr uint32_t Rare     = 3;
        constexpr uint32_t Epic     = 4;
    }

    // Weapons and armour: the gear whose origin is followed.
    bool IsGear(uint32_t itemClass);

    // What something looted or made is, as the ledger counts it: "herb",
    // "ore" (metal and stone), "leather", "cloth", "meat", "elemental",
    // "enchanting", "gem" (raw gems and cut ones), "parts", "goods" (any other
    // trade goods), "gear.green", "gear.blue", "gear.epic" (weapons and armour,
    // uncommon and up; white and grey gear is "junk" or "other"), "junk"
    // (grey), "consumable", "other".
    std::string ThingKind(uint32_t itemClass, uint32_t subClass, uint32_t quality);

    // What a used-up thing was: "potion", "elixir", "flask", "food" (and
    // drink), "bandage", "scroll", "enhancement" (oils, stones, poisons),
    // "other". Empty when it is not a consumable at all.
    std::string UseKind(uint32_t itemClass, uint32_t subClass);

    // What an auction is of: "gear", "goods" (trade goods and gems), "other".
    std::string AuctionKind(uint32_t itemClass);

    // Level bands: 0 is 1-19, 1 is 20-39, 2 is 40-59, 3 is 60-69, 4 is
    // 70-79, 5 is 80.
    constexpr uint32_t kBands = 6;
    uint32_t Band(uint32_t level);
    std::string BandName(uint32_t band);   // "1-19" ... "80"

    // "2026-11-11", the local day `when` falls on.
    std::string DayOf(std::time_t when);

    // "11 Nov 01:30", local time.
    std::string WhenText(std::time_t when);

    // "3506, 3507" -> { 3506, 3507 }. Anything that is not a whole number is
    // skipped.
    std::vector<uint64_t> GuidList(std::string const& text);

    // Copper as people say it of big sums: "1,234,567g", "56s", "7c" (gold is
    // rounded down once there is any). Negative sums get a minus.
    std::string Gold(int64_t copper);

    // 1234567 -> "1,234,567".
    std::string Count(int64_t n);

    // What .pbp economy prints, after a `title` line of the caller's.
    // `flows`: everything taken over the days asked for, summed ("gold.in",
    // "loot.herb", "gear.looted", ...), over `withData` days that have
    // figures. `now`: the latest look at the bots ("now.bots", ...), empty if
    // there is none. One line per element.
    std::vector<std::string> Report(std::string const& title, std::map<std::string, int64_t> const& flows,
                                    std::map<std::string, int64_t> const& now, uint32_t withData);

    // Every figure a look at the bots writes ("now.bots", "now.prof.mining",
    // ...), so a look that finds nobody queued writes a 0 over an earlier one.
    std::vector<std::string> NowKeys();

    // The professions the census counts, in the order the report lists them,
    // with the skill line each is.
    struct Profession
    {
        const char* name;
        uint32_t    skill;
        bool        gathering;
    };
    inline constexpr Profession kProfessions[11] = {
        { "mining",         186, true  },
        { "herbalism",      182, true  },
        { "skinning",       393, true  },
        { "blacksmithing",  164, false },
        { "engineering",    202, false },
        { "jewelcrafting",  755, false },
        { "leatherworking", 165, false },
        { "alchemy",        171, false },
        { "inscription",    773, false },
        { "tailoring",      197, false },
        { "enchanting",     333, false },
    };
}

#endif // PLAYERBOTS_PLUS_LEDGER_TEXT_H
