// Tests for the economy ledger's words and sums (src/PlayerbotsPlusLedgerText.cpp),
// which have no game types in them: what kind of thing an item is, level
// bands, days, the AH bot's guid list, money and counts as people write them,
// and the report .pbp economy prints. Not part of the server build: run
// tests/build.sh.

#include "PlayerbotsPlusLedgerText.h"

#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    void True(std::string const& what, bool ok)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_fails;
            std::printf("FAIL %s\n", what.c_str());
        }
    }

    void EqS(std::string const& what, std::string const& got, std::string const& want)
    {
        ++g_checks;
        if (got != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got '%s', want '%s'\n", what.c_str(), got.c_str(), want.c_str());
        }
    }

    bool Has(std::vector<std::string> const& lines, std::string const& text)
    {
        for (std::string const& line : lines)
            if (line.find(text) != std::string::npos)
                return true;
        return false;
    }
}

int main()
{
    using namespace PbpLedger;

    // ---- what a thing is -------------------------------------------------------------
    EqS("herb", ThingKind(7, 9, 1), "herb");
    EqS("ore", ThingKind(7, 7, 1), "ore");
    EqS("leather", ThingKind(7, 6, 1), "leather");
    EqS("cloth", ThingKind(7, 5, 1), "cloth");
    EqS("meat", ThingKind(7, 8, 1), "meat");
    EqS("elemental", ThingKind(7, 10, 1), "elemental");
    EqS("enchanting", ThingKind(7, 12, 2), "enchanting");
    EqS("a raw gem, trade goods", ThingKind(7, 4, 2), "gem");
    EqS("a cut gem", ThingKind(3, 0, 3), "gem");
    EqS("parts", ThingKind(7, 1, 1), "parts");
    EqS("other goods", ThingKind(7, 11, 1), "goods");
    EqS("a green sword", ThingKind(2, 7, 2), "gear.green");
    EqS("a blue helm", ThingKind(4, 4, 3), "gear.blue");
    EqS("an epic ring", ThingKind(4, 0, 4), "gear.epic");
    EqS("a legendary counts as epic", ThingKind(2, 7, 5), "gear.epic");
    EqS("a white shirt", ThingKind(4, 0, 1), "other");
    EqS("grey junk", ThingKind(4, 2, 0), "junk");
    EqS("grey trade goods are junk too", ThingKind(7, 11, 0), "junk");
    EqS("a potion", ThingKind(0, 1, 1), "consumable");
    EqS("a quest item", ThingKind(12, 0, 1), "other");
    True("gear", IsGear(2) && IsGear(4) && !IsGear(7) && !IsGear(0) && !IsGear(3));

    EqS("used potion", UseKind(0, 1), "potion");
    EqS("used elixir", UseKind(0, 2), "elixir");
    EqS("used flask", UseKind(0, 3), "flask");
    EqS("used scroll", UseKind(0, 4), "scroll");
    EqS("used food", UseKind(0, 5), "food");
    EqS("used oil", UseKind(0, 6), "enhancement");
    EqS("used bandage", UseKind(0, 7), "bandage");
    EqS("used other", UseKind(0, 8), "other");
    EqS("used something else", UseKind(0, 0), "other");
    EqS("not a consumable", UseKind(7, 9), "");

    EqS("auction of gear", AuctionKind(2), "gear");
    EqS("auction of armour", AuctionKind(4), "gear");
    EqS("auction of goods", AuctionKind(7), "goods");
    EqS("auction of a gem", AuctionKind(3), "goods");
    EqS("auction of a recipe", AuctionKind(9), "other");
    EqS("auction of nothing known", AuctionKind(99), "other");

    // ---- bands, days, guids ---------------------------------------------------------
    True("bands", Band(1) == 0 && Band(19) == 0 && Band(20) == 1 && Band(39) == 1 && Band(40) == 2 &&
                      Band(59) == 2 && Band(60) == 3 && Band(69) == 3 && Band(70) == 4 && Band(79) == 4 &&
                      Band(80) == 5 && Band(255) == 5);
    EqS("band name", BandName(0), "1-19");
    EqS("band name 80", BandName(5), "80");
    EqS("band name out of range", BandName(9), "?");

    setenv("TZ", "UTC", 1);
    tzset();
    EqS("day", DayOf(std::time_t(1794355200)), "2026-11-11");     // 2026-11-11 00:00 UTC
    EqS("day, a second before", DayOf(std::time_t(1794355199)), "2026-11-10");
    EqS("when", WhenText(std::time_t(1794355200 + 5400)), "11 Nov 01:30");
    EqS("when, a single-figure day", WhenText(std::time_t(1794355200 - 6 * 86400)), "5 Nov 00:00");

    True("guid list", GuidList("3506") == std::vector<uint64_t>{ 3506 });
    True("guid list, two", GuidList("3506, 3507") == std::vector<uint64_t>{ 3506, 3507 });
    True("guid list, odd spacing", GuidList(" 1,2 ;3\t4 ") == std::vector<uint64_t>{ 1, 2, 3, 4 });
    True("guid list, words skipped", GuidList("0x12, 5, abc") == std::vector<uint64_t>{ 5 });
    True("guid list, empty", GuidList("").empty());
    True("guid list, zero kept", GuidList("0") == std::vector<uint64_t>{ 0 });

    // ---- money and counts ---------------------------------------------------------
    EqS("count", Count(0), "0");
    EqS("count 999", Count(999), "999");
    EqS("count 1000", Count(1000), "1,000");
    EqS("count big", Count(1234567), "1,234,567");
    EqS("count negative", Count(-1234), "-1,234");
    EqS("gold", Gold(12345678), "1,234g");
    EqS("gold big", Gold(123456780000LL), "12,345,678g");
    EqS("silver", Gold(5600), "56s");
    EqS("copper", Gold(7), "7c");
    EqS("nothing", Gold(0), "0c");
    EqS("negative gold", Gold(-25000), "-2g");

    // ---- what a look writes ------------------------------------------------------------
    const std::vector<std::string> keys = NowKeys();
    const std::set<std::string> unique(keys.begin(), keys.end());
    True("look keys are unique", unique.size() == keys.size());
    True("look keys", unique.count("now.bots") && unique.count("now.prof.mining") && unique.count("now.ilvl.b5") &&
                          unique.count("now.geared.b0") && unique.count("now.ah.ahbot.value") &&
                          unique.count("now.time") && unique.count("now.lfg"));
    True("look keys all start now.", [&]() {
        for (std::string const& k : keys)
            if (k.rfind("now.", 0) != 0)
                return false;
        return true;
    }());
    True("professions", sizeof(kProfessions) / sizeof(kProfessions[0]) == 11 && kProfessions[0].gathering &&
                            kProfessions[1].gathering && kProfessions[2].gathering && !kProfessions[3].gathering);
    for (std::string const& k : keys)
        True("a figure name fits the table (64): " + k, k.size() <= 64);

    // ---- the report -----------------------------------------------------------------
    {
        const std::vector<std::string> empty = Report("Title:", {}, {}, 0);
        True("empty report: the title and one line", empty.size() == 2 && empty[0] == "Title:" &&
                                                          Has(empty, "No figures yet"));
    }
    {
        std::map<std::string, int64_t> flows = {
            { "gold.in", 1000000000 },  { "gold.loot", 600000000 }, { "gold.quests", 200000000 },
            { "gold.mail", 50000000 },  { "gold.out", 300000000 },  { "ah.bought.value", 10000000 },
            { "ah.deposits", 2000000 }, { "gold.free", 9000000 },   { "gold.gone", 10000 },
            { "ah.listed", 1234 },      { "ah.listed.gear", 300 },  { "ah.listed.goods", 900 },
            { "ah.listed.other", 34 },  { "ah.listed.value", 50000000 }, { "ah.sold", 800 },
            { "ah.sold.value", 41000000 }, { "ah.sold.cut", 2050000 }, { "ah.sold.ahbot", 760 },
            { "ah.sold.ahbot.value", 40000000 }, { "ah.sold.player", 25 }, { "ah.sold.bot", 15 },
            { "ah.expired", 300 },      { "ah.bought", 40 },        { "ah.bought.ahbot", 30 },
            { "loot.herb", 5432 },      { "loot.ore", 4321 },       { "loot.gear.green", 3000 },
            { "loot.junk", 20000 },     { "loot.nodes", 9000 },     { "made.consumable", 12 },
            { "vendor.consumable", 40 }, { "gear.handout", 2000 },  { "gear.handout.ilvl", 240000 },
            { "gear.looted", 300 },     { "gear.looted.ilvl", 34500 }, { "used.potion", 900 },
            { "used.food", 2000 },
        };
        std::map<std::string, int64_t> now;
        for (std::string const& k : NowKeys())
            now[k] = 0;
        now["now.time"] = 1794355200 + 5400;
        now["now.bots"] = 3098;
        now["now.gold"] = 3098LL * 6400000;
        now["now.bots.b5"] = 300;
        now["now.gold.b5"] = 300LL * 21000000;
        now["now.ilvl.b5"] = 300 * 200;
        now["now.geared.b5"] = 300;
        now["now.prof.mining"] = 1100;
        now["now.prof.gatherers"] = 2400;
        now["now.lfg"] = 120;
        now["now.lfg.ilvl"] = 120 * 150;
        now["now.lfg.geared"] = 120;
        now["now.ah.ahbot"] = 20000;
        now["now.ah.bot"] = 1500;
        now["now.ah.player"] = 12;

        const std::vector<std::string> lines = Report("Seven days:", flows, now, 7);
        True("report has every part", lines.size() == 15);
        EqS("report title", lines.empty() ? "" : lines[0], "Seven days:");
        True("gold in, a day", Has(lines, "Gold in: 100,000g, about 14,285g a day"));
        True("gold in, its parts", Has(lines, "looted 60,000g, quest rewards 20,000g, auction house mail 5,000g, "
                                              "the rest 15,000g"));
        True("gold out, its parts", Has(lines, "auction house buys 1,000g, deposits 200g, the rest 28,800g"));
        True("gold from playerbots", Has(lines, "+900g, -1g"));
        True("auction selling", Has(lines, "1,234 listed (300 gear, 900 goods, 34 other) asking 5,000g; 800 sold "
                                           "for 4,100g (205g of it to the house) -- to the AH bot 760 (4,000g)"));
        True("auction buying", Has(lines, "40 bought for 1,000g -- from the AH bot 30"));
        True("looted", Has(lines, "Looted: herbs 5,432, ore and stone 4,321") &&
                           Has(lines, "gear: green 3,000") && Has(lines, "grey junk 20,000. 9,000 things"));
        True("made and vendors", Has(lines, "Made (crafted or conjured): 12 -- consumables 12") &&
                                     Has(lines, "Bought from vendors: 40 -- consumables 40"));
        True("gear put on", Has(lines, "Gear put on: 2,300 -- handed out by playerbots 2,000 (item level 120), "
                                       "looted 300 (115)") &&
                                Has(lines, "quest rewards 0 (-)"));
        True("used up", Has(lines, "Used up: food and drink 2,000, potions 900"));
        True("the look", Has(lines, "Last look at the bots (11 Nov 01:30): 3,098 online, holding 1,982,720g -- "
                                    "640g each on average, 2,100g at level 80."));
        True("item levels", Has(lines, "80 200 (300 bots)") && Has(lines, "1-19 - (0 bots)"));
        True("professions", Has(lines, "Professions: mining 1,100, herbalism 0") &&
                                Has(lines, "2,400 have a gathering one"));
        True("queue", Has(lines, "In the dungeon queue: 120 (item level 150)"));
        True("auction houses", Has(lines, "Up on the auction houses: 21,512 -- the AH bot 20,000"));
        for (std::string const& line : lines)
            True("no placeholders left: " + line.substr(0, 40),
                 line.find('{') == std::string::npos && line.find('}') == std::string::npos);

        const std::vector<std::string> noLook = Report("One day:", flows, {}, 1);
        True("no look yet", noLook.size() == 11 && Has(noLook, "No look at the bots yet"));
        True("one day: no 'a day'", !Has(noLook, " a day"));

        std::map<std::string, int64_t> under = { { "gold.in", 100 }, { "gold.loot", 500 } };
        True("the rest never below nothing", Has(Report("t", under, {}, 1), "the rest 0c"));
    }

    std::printf("%d/%d passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
