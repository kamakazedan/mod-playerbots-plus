// Tests for professions' sums (src/PlayerbotsPlusCraftText.cpp), which have
// no game types in them. Not part of the server build: AzerothCore only
// compiles a module's src/. Run tests/build.sh.

#include "PlayerbotsPlusCraftText.h"

#include <cstdio>

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

int main()
{
    using namespace PbpCraft;

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

    std::printf("craft: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
