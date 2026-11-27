// Tests for professions' sums (src/PlayerbotsPlusCraftText.cpp), which have
// no game types in them. Not part of the server build: AzerothCore only
// compiles a module's src/. Run tests/build.sh.

#include "PlayerbotsPlusCraftText.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    void Check(char const* what, bool ok)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_fails;
            std::printf("FAIL %s\n", what);
        }
    }
}

int main(int argc, char** argv)
{
    using namespace PbpCraft;
    const std::string root = argc > 1 ? argv[1] : ".";

    // ---- skill-ups: as the core works them out --------------------------------
    // Copper Bar: yellow at 25, grey at 75 (its TrivialSkillLineRank).
    Check("orange up to yellow", PointChance(1, 25, 75) == 100 && PointChance(25, 25, 75) == 100);
    Check("halfway: half", PointChance(50, 25, 75) == 50);
    Check("grey from grey", PointChance(75, 25, 75) == 0 && PointChance(140, 25, 75) == 0);
    Check("falling evenly", PointChance(70, 25, 75) == 10);
    Check("thresholds it cannot spread", PointChance(10, 30, 30) == 100 && PointChance(30, 30, 30) == 0);

    // ---- ranks ----------------------------------------------------------------------
    Check("a new character: Apprentice", RankFor(1, 1).step == 1 && RankFor(1, 1).cap == 75);
    Check("50 skill at level 9: not yet Journeyman", RankFor(9, 50).cap == 75);
    Check("50 skill at level 10: Journeyman", RankFor(10, 50).step == 2 && RankFor(10, 50).cap == 150);
    Check("level 40 with 180: Expert, not Artisan yet", RankFor(40, 180).step == 3 && RankFor(40, 180).cap == 225);
    Check("level 40 with 200: Artisan", RankFor(40, 200).cap == 300);
    Check("level 34 with 260: Expert still", RankFor(34, 260).cap == 225);
    Check("level 80 with 450: Grand Master", RankFor(80, 450).step == 6 && RankFor(80, 450).cap == 450);
    Check("level 80 with 0: Apprentice", RankFor(80, 0).cap == 75);

    // ---- pairs for a class ---------------------------------------------------------------
    Check("warrior: mining and blacksmithing fit", PairFits(Class::Warrior, Skill::Mining, Skill::Blacksmithing));
    Check("either way round", PairFits(Class::Warrior, Skill::Blacksmithing, Skill::Mining));
    Check("mage: blacksmithing does not", !PairFits(Class::Mage, Skill::Mining, Skill::Blacksmithing));
    Check("rogue: skinning and leatherworking", PairFits(Class::Rogue, Skill::Skinning, Skill::Leatherworking));
    Check("two gathering ones fit nobody", !PairFits(Class::Hunter, Skill::Mining, Skill::Skinning));
    {
        // A mage with mining and tailoring keeps tailoring: tailoring and enchanting.
        const uint32_t total = WeightOf(Class::Mage, Skill::Tailoring);
        Check("mage keeping tailoring: one pair to pick", total == 50);
        const auto p = PickPair(Class::Mage, Skill::Tailoring, 1);
        Check("...tailoring and enchanting", p.first == Skill::Tailoring && p.second == Skill::Enchanting);
    }
    {
        // A warrior with herbalism and skinning: herbalism and alchemy is the
        // only warrior pair with herbalism in it.
        const auto p = PickPair(Class::Warrior, Skill::Herbalism, WeightOf(Class::Warrior, Skill::Herbalism));
        Check("warrior keeping herbalism: herbalism and alchemy",
              p.first == Skill::Herbalism && p.second == Skill::Alchemy);
    }
    {
        // Nothing worth keeping: any of the class's, by weight.
        Check("warrior's weights add up", WeightOf(Class::Warrior, 0) == 100);
        Check("roll 1: the first", PickPair(Class::Warrior, 0, 1).second == Skill::Blacksmithing);
        Check("roll 46: the second", PickPair(Class::Warrior, 0, 46).second == Skill::Engineering);
        Check("roll 100: the last", PickPair(Class::Warrior, 0, 100).second == Skill::Alchemy);
        Check("keeping one no pair has: any", WeightOf(Class::Warrior, Skill::Tailoring) == 100);
    }
    Check("primary professions", IsPrimary(Skill::Mining) && IsPrimary(Skill::Inscription) &&
                                     !IsPrimary(Skill::Cooking) && !IsPrimary(Skill::FirstAid));
    Check("apprentice spells", ApprenticeSpell(Skill::Blacksmithing) == 2018 && ApprenticeSpell(Skill::Mining) == 2575 &&
                                   ApprenticeSpell(42) == 0);

    // ---- leanings ------------------------------------------------------------------------
    Check("professions by name, any case", PrimaryByName("mining") == Skill::Mining &&
                                               PrimaryByName("JEWELCRAFTING") == Skill::Jewelcrafting);
    Check("...primary ones only", PrimaryByName("First Aid") == 0 && PrimaryByName("Cooking") == 0 &&
                                      PrimaryByName("") == 0 && PrimaryByName("Mine") == 0);
    Check("a smith with a miner feeds itself", FeedsItself(Skill::Mining, Skill::Blacksmithing) &&
                                                   FeedsItself(Skill::Blacksmithing, Skill::Mining));
    Check("two that both want ore do not", !FeedsItself(Skill::Blacksmithing, Skill::Engineering));
    Check("alchemy without herbalism does not", !FeedsItself(Skill::Alchemy, Skill::Tailoring) &&
                                                     !FeedsItself(Skill::Alchemy, Skill::Mining));
    Check("leatherworking wants skinning", FeedsItself(Skill::Skinning, Skill::Leatherworking) &&
                                               !FeedsItself(Skill::Mining, Skill::Leatherworking));
    Check("tailoring and enchanting want only loot", FeedsItself(Skill::Tailoring, Skill::Enchanting) &&
                                                         FeedsItself(Skill::Tailoring, Skill::Herbalism));
    Check("two gathering ones feed themselves", FeedsItself(Skill::Herbalism, Skill::Mining) &&
                                                    FeedsItself(Skill::Mining, Skill::Skinning));
    Check("not one profession twice", !FeedsItself(Skill::Mining, Skill::Mining) &&
                                          !FeedsItself(Skill::Mining, Skill::Cooking));
    Check("plate is the smith's", ArmourPair(Class::Paladin).second == Skill::Blacksmithing &&
                                      ArmourPair(Class::DeathKnight).first == Skill::Mining);
    Check("mail and leather the leatherworker's", ArmourPair(Class::Shaman).second == Skill::Leatherworking &&
                                                      ArmourPair(Class::Rogue).first == Skill::Skinning);
    Check("cloth the tailor's", ArmourPair(Class::Warlock).first == Skill::Tailoring);
    Check("racial knacks", RaceKnack(Race::Gnome) == Skill::Engineering &&
                               RaceKnack(Race::Tauren) == Skill::Herbalism &&
                               RaceKnack(Race::BloodElf) == Skill::Enchanting &&
                               RaceKnack(Race::Draenei) == Skill::Jewelcrafting);
    Check("...and none for the rest", RaceKnack(Race::Human) == 0 && RaceKnack(Race::Orc) == 0 &&
                                          KnackPair(Race::Troll).first == 0);
    {
        std::string bad;
        const Leaning l = ParseLeaning("Herbalism+Mining:70  class:30 sell list:200", bad);
        Check("a leaning reads", bad.empty() && l.pairs.size() == 1 && l.pairs[0].weight == 70 &&
                                     l.classShare == 30 && l.sellFirst && l.listPct == 200);
        Check("...as written", l.pairs[0].first == Skill::Herbalism && l.pairs[0].second == Skill::Mining);
        const Leaning m = ParseLeaning("Enchanting+Tailoring:5 Blacksmithing+Mining:5", bad);
        Check("the gathering one first", m.pairs.size() == 2 && m.pairs[1].first == Skill::Mining &&
                                             m.pairs[0].first == Skill::Enchanting);
    }
    {
        std::string bad;
        const Leaning l = ParseLeaning("Blacksmithing+Alchemy:20 Mining+Tailoring:10 frob class:x never:Fishing "
                                       "Mining+Skinning:50 never:Skinning list:5000 rare:12",
                                       bad);
        Check("what does not read is left out", l.pairs.size() == 1 && l.pairs[0].second == Skill::Tailoring &&
                                                    l.classShare == 0 && l.listPct == 100 && l.rareShare == 12);
        Check("...and named", bad.find("Blacksmithing+Alchemy:20") != std::string::npos &&
                                  bad.find("frob") != std::string::npos && bad.find("class:x") != std::string::npos &&
                                  bad.find("never:Fishing") != std::string::npos &&
                                  bad.find("list:5000") != std::string::npos);
        Check("a pair with what it will never have is not taken",
              l.never.size() == 1 && bad.find("Mining+Skinning (never)") != std::string::npos);
    }
    {
        std::string bad;
        const Leaning profit = ParseLeaning("Herbalism+Mining:70 class:30", bad);
        const std::vector<Pick> picks = Picks(profit, Class::Warrior, Race::Orc);
        Check("profit warrior: its own pair and the class's", picks.size() == 5 && picks[0].from == Match::Own &&
                                                                  picks[1].from == Match::Class);
        Check("...70 to 30", PicksWeight(picks, 0) == 10000 && picks[0].weight == 7000 && picks[1].weight == 1350);
        Check("roll 7000: its own", PickOne(picks, 0, 7000).second == Skill::Mining);
        Check("roll 7001: the class's first", PickOne(picks, 0, 7001).second == Skill::Blacksmithing);
        Check("keeping alchemy: herbalism and alchemy",
              PicksWeight(picks, Skill::Alchemy) == 300 && PickOne(picks, Skill::Alchemy, 1).first == Skill::Herbalism);
        Check("herbalism and mining is its own", MatchOf(profit, Class::Warrior, 0, Skill::Mining, Skill::Herbalism) ==
                                                     Match::Own);
        Check("mining and blacksmithing its class's",
              MatchOf(profit, Class::Warrior, 0, Skill::Blacksmithing, Skill::Mining) == Match::Class);
        Check("tailoring and alchemy neither",
              MatchOf(profit, Class::Warrior, 0, Skill::Tailoring, Skill::Alchemy) == Match::None);
    }
    {
        std::string bad;
        const Leaning pets = ParseLeaning("Mining+Engineering:70 class:30 never:Skinning", bad);
        const std::vector<Pick> picks = Picks(pets, Class::Hunter, Race::Orc);
        bool skins = false;
        for (Pick const& p : picks)
            skins = skins || p.first == Skill::Skinning || p.second == Skill::Skinning;
        Check("a pet lover hunter never skins", !skins && bad.empty());
        Check("...and has the hunter's engineering once, added up",
              picks[0].from == Match::Own && picks[0].weight == 70 * 100 + 35 * 30);
        Check("skinning is never", MatchOf(pets, Class::Hunter, 0, Skill::Skinning, Skill::Leatherworking) ==
                                       Match::Never);
    }
    {
        std::string bad;
        const Leaning power = ParseLeaning("armour:70 Mining+Jewelcrafting:30", bad);
        Check("a priest's armour is tailoring", MatchOf(power, Class::Priest, 0, Skill::Tailoring, Skill::Enchanting) ==
                                                    Match::Armour);
        Check("...gems its own", MatchOf(power, Class::Priest, 0, Skill::Jewelcrafting, Skill::Mining) == Match::Own);
        Check("...and its class's pairs still suit it",
              MatchOf(power, Class::Priest, 0, Skill::Herbalism, Skill::Inscription) == Match::Class);
        const Leaning both = ParseLeaning("armour:50 Mining+Blacksmithing:50", bad);
        const std::vector<Pick> picks = Picks(both, Class::Warrior, 0);
        Check("a pair named twice is one, as it came first",
              picks.size() == 1 && picks[0].from == Match::Own && picks[0].weight == 10000);
    }
    {
        std::string bad;
        const Leaning heritage = ParseLeaning("race:100", bad);
        Check("a gnome's knack", MatchOf(heritage, Class::Mage, Race::Gnome, Skill::Mining, Skill::Engineering) ==
                                     Match::Race);
        const std::vector<Pick> human = Picks(heritage, Class::Mage, Race::Human);
        Check("a human has none: the class's pairs", human.size() == 3 && human[0].from == Match::Class &&
                                                         PicksWeight(human, 0) == 10000);
        const Leaning contrary = ParseLeaning("rare:100", bad);
        const std::vector<Pick> rogue = Picks(contrary, Class::Rogue, 0);
        Check("the rogue the fewest take: inscription", rogue.size() == 1 && rogue[0].second == Skill::Inscription &&
                                                            rogue[0].from == Match::Rare);
        const std::vector<Pick> mage = Picks(contrary, Class::Mage, 0);
        Check("a mage's two least alike: half each", mage.size() == 2 && mage[0].weight == 5000 &&
                                                         mage[1].weight == 5000);
        // A newbie looks it up: a pair that suits its class, and an odd one is none of its.
        const Leaning newbie = ParseLeaning("class:100", bad);
        Check("a newbie's odd pair is put right",
              MatchOf(newbie, Class::Mage, 0, Skill::Blacksmithing, Skill::Alchemy) == Match::None);
        Check("...and its class's suits it",
              MatchOf(newbie, Class::Mage, 0, Skill::Enchanting, Skill::Tailoring) == Match::Class);
        Check("nothing to take: the class's", Picks(Leaning{}, Class::Priest, 0).size() == 3);
        Check("the same profession twice is no pair",
              MatchOf(newbie, Class::Mage, 0, Skill::Tailoring, Skill::Tailoring) == Match::None);
        Check("...and nothing written read", bad.empty());
    }
    {
        // The leanings the module ships read, and every pair they name feeds itself.
        bool clean = true;
        for (auto const& [name, text] : DefaultLeanings())
        {
            std::string bad;
            ParseLeaning(text, bad);
            if (!bad.empty())
            {
                clean = false;
                std::printf("  %s: %s\n", name.c_str(), bad.c_str());
            }
        }
        Check("the shipped leanings read", clean && DefaultLeanings().size() == 13);
        Check("a leaning's name, looked up", LeaningKey(" Profit ") == "profit" && LeaningKey("  ").empty() &&
                                                 LeaningKey("OldWays") == "oldways");
    }
    {
        bool same = true, spread = true;
        std::vector<int> seen(11, 0);
        for (uint64_t guid = 1; guid <= 1000; ++guid)
        {
            const uint32_t r = StableRoll(guid, 10);
            same = same && r == StableRoll(guid, 10) && r >= 1 && r <= 10;
            ++seen[r];
        }
        for (int i = 1; i <= 10; ++i)
            spread = spread && seen[i] > 50;
        Check("a stable roll is the same each time", same);
        Check("...and spread", spread && StableRoll(7, 0) == 0);
    }

    // ---- what to craft --------------------------------------------------------------------
    ChoiceSettings s;
    s.maxCasts      = 10;
    s.minChance     = 25;
    s.sellMarginPct = 130;
    {
        std::vector<Option> o(3);
        o[0] = { 1, 100, 20, 500, 300, false, true };   // orange, 5s a go
        o[1] = { 2, 100, 20, 200, 150, false, true };   // orange, 2s a go: cheaper
        o[2] = { 3, 0, 5, 100, 900, false, true };      // grey, but sells well
        const Choice c = Choose(o, s);
        Check("the cheapest chance at a point", c.why == Why::Skill && c.index == 1 && c.casts == 10);
    }
    {
        std::vector<Option> o(2);
        o[0] = { 1, 100, 4, 400, 0, false, true };   // orange: 4s a point
        o[1] = { 2, 50, 9, 150, 0, false, true };    // half a chance: 3s a point
        const Choice c = Choose(o, s);
        Check("copper a point, not a go", c.why == Why::Skill && c.index == 1 && c.casts == 9);
    }
    {
        std::vector<Option> o(2);
        o[0] = { 1, 100, 3, 500, 300, false, true };
        o[1] = { 2, 0, 2, 2000, 1000, true, false };   // an upgrade for itself
        const Choice c = Choose(o, s);
        Check("an upgrade first, once", c.why == Why::Use && c.index == 1 && c.casts == 1);
    }
    {
        std::vector<Option> o(2);
        o[0] = { 1, 100, 20, 200, 150, false, true };   // orange, 2s a go, worth 1s 50c: not to sell
        o[1] = { 2, 0, 5, 100, 900, false, true };      // grey, but sells well
        ChoiceSettings greedy = s;
        greedy.sellFirst = true;
        Check("a point before a sale", Choose(o, s).why == Why::Skill);
        const Choice c = Choose(o, greedy);
        Check("selling first: the sale", c.why == Why::Sell && c.index == 1 && c.casts == 5);
        o[1].worthEach = 120;
        Check("...nothing worth selling: the point", Choose(o, greedy).why == Why::Skill);
        o.push_back({ 3, 0, 1, 2000, 1000, true, false });
        Check("...an upgrade still first", Choose(o, greedy).why == Why::Use);
    }
    {
        std::vector<Option> o(3);
        o[0] = { 1, 10, 5, 100, 120, false, true };    // a point too unlikely; not worth enough over 100
        o[1] = { 2, 0, 7, 100, 200, false, true };     // grey: 1s over its materials
        o[2] = { 3, 0, 2, 1000, 1500, false, true };   // grey: 5s over
        const Choice c = Choose(o, s);
        Check("to sell: the most over its materials", c.why == Why::Sell && c.index == 2 && c.casts == 2);
    }
    {
        std::vector<Option> o(1);
        o[0] = { 1, 0, 7, 100, 2000, false, false };   // worth a lot, but nobody could buy it
        Check("nothing to sell that cannot be sold", Choose(o, s).why == Why::None);
    }
    {
        std::vector<Option> o(2);
        o[0] = { 1, 100, 0, 10, 50, false, true };   // nothing to make it with
        o[1] = { 2, 0, 3, 100, 120, false, true };   // not worth making
        Check("nothing worth doing", Choose(o, s).why == Why::None);
    }
    Check("nothing at all", Choose({}, s).why == Why::None);
    {
        // Nothing gives a point now; smelting would get the smith its bars
        // (one step from a point), and something else sells well.
        std::vector<Option> o(3);
        o[0] = { 1, 0, 6, 100, 400, false, true };          // grey, sells well
        o[1] = { 2, 0, 4, 50, 60, false, true, 1 };         // bars: a step from a point
        o[2] = { 3, 0, 4, 20, 30, false, true, 2 };         // ...two steps from one
        const Choice c = Choose(o, s);
        Check("materials before selling, the nearest to the point", c.why == Why::Materials && c.index == 1 &&
                                                                         c.casts == 4);
        o[1].steps = 2;   // as far off as the other: the cheaper
        Check("...and the cheaper of two as near", Choose(o, s).index == 2);
        o.push_back({ 4, 100, 2, 900, 0, false, true });
        Check("a point now comes first", Choose(o, s).why == Why::Skill);
    }

    // ---- on the way to a point -----------------------------------------------------------
    {
        // A scribe: a glyph (a point) takes ink and parchment; ink is made of
        // pigment; pigment comes of milling.
        const uint32_t ink = 10, parchment = 11, pigment = 12, glyph = 13;
        std::vector<Known> known;
        known.push_back({ 100, glyph, { { ink, 1 }, { parchment, 1 } } });
        known.push_back({ 0, ink, { { pigment, 2 } } });
        std::unordered_map<uint32_t, uint32_t> have{ { parchment, 5 } };
        auto w = Wanted(known, have, 25);
        Check("ink is one step from a point", w.count(ink) && w[ink] == 1);
        Check("pigment two", w.count(pigment) && w[pigment] == 2);
        Check("parchment it has is not wanted", !w.count(parchment));
        have[ink] = 1;
        w = Wanted(known, have, 25);
        Check("with ink, nothing is wanted", w.empty());
        have.clear();
        have[pigment] = 1;   // one, and ink takes two
        w = Wanted(known, have, 25);
        Check("too few is short", w.count(pigment) && w[pigment] == 2 && w[parchment] == 1);
        known[0].pointChance = 10;   // a point too unlikely to be worth it
        Check("nothing wanted for a point not worth asking", Wanted(known, {}, 25).empty());
        Check("no further than asked", !Wanted({ { 100, glyph, { { ink, 1 } } }, { 0, ink, { { pigment, 2 } } } },
                                              {}, 25, 1).count(pigment));
    }

    // ---- breaking things down ------------------------------------------------------------
    {
        // Disenchanting a low green (disenchant_loot_template 1): dust four
        // times in five, one or two; else an essence, one or two.
        LootTable de = { { 10938, 0, 0.0f, 1, 1, 2 }, { 10940, 0, 80.0f, 1, 1, 2 } };
        const Yields y = Expected(de, {});
        Check("dust: 0.8 x 1.5", y.count(10940) && y.at(10940) > 1.19 && y.at(10940) < 1.21);
        Check("essence: the rest, 0.2 x 1.5", y.count(10938) && y.at(10938) > 0.29 && y.at(10938) < 0.31);
    }
    {
        // Milling Silverleaf: its table is another one, rolled whole
        // (reference 11900: pigment, two to four, always).
        LootTables refs;
        refs[11900] = { { 39151, 0, 100.0f, 0, 2, 4 } };
        LootTable mill = { { 11900, 11900, 100.0f, 0, 1, 1 } };
        const Yields y = Expected(mill, refs);
        Check("milling: three pigment a go", y.size() == 1 && y.at(39151) > 2.99 && y.at(39151) < 3.01);
        Check("a table that is not there gives nothing", Expected(mill, {}).empty());
    }
    {
        // Prospecting copper: malachite or tigerseye, one of the two, and a
        // shadowgem one time in ten besides.
        LootTable ore = { { 774, 0, 0.0f, 1, 1, 1 }, { 818, 0, 0.0f, 1, 1, 1 }, { 1210, 0, 10.0f, 0, 1, 1 } };
        const Yields y = Expected(ore, {});
        Check("one of two alike: half each", y.at(774) > 0.49 && y.at(774) < 0.51 && y.at(818) > 0.49);
        Check("and the odd one by itself", y.at(1210) > 0.09 && y.at(1210) < 0.11);
    }
    {
        // A table that refers to itself does not go round for ever.
        LootTables refs;
        refs[5] = { { 5, 5, 100.0f, 0, 1, 1 }, { 77, 0, 100.0f, 0, 1, 1 } };
        const Yields y = Expected(refs[5], refs);
        Check("round and round ends", y.count(77) && y.at(77) < 10.0);
    }
    Check("milling within 25: certain", GatherChance(1, 1) == 100 && GatherChance(25, 1) == 100);
    Check("...75 within 50, 25 within 100, none after",
          GatherChance(26, 1) == 75 && GatherChance(51, 1) == 25 && GatherChance(101, 1) == 0);

    {
        // What the conf ships is the code's own leanings, word for word, and
        // each is there.
        std::ifstream in(root + "/conf/mod_playerbots_plus.conf.dist");
        std::string line;
        const std::string prefix = "PlayerbotsPlus.Skills.Leaning.";
        size_t lines = 0, same = 0;
        while (std::getline(in, line))
        {
            if (line.rfind(prefix, 0) != 0)
                continue;
            ++lines;
            const std::string name = line.substr(prefix.size(), line.find(' ') - prefix.size());
            const size_t q1 = line.find('"'), q2 = line.rfind('"');
            const std::string value = q2 > q1 ? line.substr(q1 + 1, q2 - q1 - 1) : std::string();
            bool found = false;
            for (auto const& [n, text] : DefaultLeanings())
                if (n == name && text == value)
                    found = true;
            if (found)
                ++same;
            else
                std::printf("  conf leaning %s is not the code's\n", name.c_str());
        }
        Check("the conf's leanings are the code's", lines == DefaultLeanings().size() && same == lines);
    }

    std::printf("craft: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
