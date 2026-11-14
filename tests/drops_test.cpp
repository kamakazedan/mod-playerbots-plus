// Tests for working out drop chances from loot tables
// (src/PlayerbotsPlusDropChance.cpp), which has no game types in it. Not part
// of the server build: AzerothCore only compiles a module's src/. Run
// tests/build.sh.

#include "PlayerbotsPlusDropChance.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    void Near(char const* what, double got, double want)
    {
        ++g_checks;
        if (std::fabs(got - want) > 1e-7 * std::max(1.0, std::fabs(want)))
        {
            ++g_fails;
            std::printf("FAIL %s: got %.12g, want %.12g\n", what, got, want);
        }
    }

    void Eq(char const* what, uint64_t got, uint64_t want)
    {
        ++g_checks;
        if (got != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got %llu, want %llu\n", what, (unsigned long long)got, (unsigned long long)want);
        }
    }

    using PbpDrops::Row;
    using PbpDrops::Table;
    using PbpDrops::Tables;

    Row Item(uint32_t item, float chance, uint8_t group = 0)
    {
        Row r;
        r.item   = item;
        r.chance = chance;
        r.group  = group;
        return r;
    }

    Row Ref(uint32_t reference, float chance, uint8_t group = 0, uint8_t times = 1)
    {
        Row r;
        r.reference = reference;
        r.chance    = chance;
        r.group     = group;
        r.maxCount  = times;
        return r;
    }

    double Of(PbpDrops::Chances const& c, uint32_t item)
    {
        auto it = c.find(item);
        return it == c.end() ? 0.0 : it->second;
    }
}

int main()
{
    const Tables none;

    // ---- rows by themselves -------------------------------------------------------
    {
        PbpDrops::Roller roll(none);
        const auto c = roll.Roll({ Item(1, 25.0f), Item(2, 100.0f), Item(3, 150.0f), Item(4, 0.0f) });
        Near("a row at 25%", Of(c, 1), 0.25);
        Near("a row at 100%", Of(c, 2), 1.0);
        Near("over 100% is 100%", Of(c, 3), 1.0);
        Near("0% outside a group never", Of(c, 4), 0.0);
        Near("two rows of one thing: either", Of(roll.Roll({ Item(1, 50.0f), Item(1, 50.0f) }), 1), 0.75);
    }

    // ---- a group: one roll, one row --------------------------------------------------
    {
        PbpDrops::Roller roll(none);
        const auto c = roll.Roll({ Item(1, 30.0f, 1), Item(2, 20.0f, 1), Item(3, 0.0f, 1), Item(4, 0.0f, 1) });
        Near("chanced in a group: its own", Of(c, 1), 0.30);
        Near("chanced in a group: its own (2)", Of(c, 2), 0.20);
        Near("the rest alike: what is left, shared", Of(c, 3), 0.25);
        Near("the rest alike (2)", Of(c, 4), 0.25);

        const auto over = roll.Roll({ Item(1, 80.0f, 1), Item(2, 50.0f, 1), Item(3, 0.0f, 1) });
        Near("over 100 in a group: the first in full", Of(over, 1), 0.80);
        Near("...the next only what is left", Of(over, 2), 0.20);
        Near("...and nothing for the rest", Of(over, 3), 0.0);

        const auto two = roll.Roll({ Item(1, 0.0f, 1), Item(2, 0.0f, 1), Item(1, 0.0f, 2), Item(3, 0.0f, 2) });
        Near("in two groups: either gives it", Of(two, 1), 1.0 - 0.5 * 0.5);
    }

    // ---- references ---------------------------------------------------------------------
    {
        Tables refs;
        refs[100] = { Item(10, 0.0f, 1), Item(11, 0.0f, 1), Item(12, 0.0f, 1), Item(13, 0.0f, 1) };
        refs[200] = { Item(20, 50.0f) };
        refs[300] = { Ref(100, 100.0f), Item(30, 10.0f) };
        refs[400] = { Ref(400, 100.0f), Item(40, 10.0f) };   // refers to itself
        PbpDrops::Roller roll(refs);

        Near("a reference at 10%: a quarter of that", Of(roll.Roll({ Ref(100, 10.0f) }), 10), 0.025);
        Near("rolled twice when it comes up", Of(roll.Roll({ Ref(100, 10.0f, 0, 2) }), 10), 0.1 * (1.0 - 0.75 * 0.75));
        Near("a reference in a group: when picked", Of(roll.Roll({ Ref(100, 0.0f, 1), Ref(200, 0.0f, 1) }), 20),
             0.5 * 0.5);
        Near("...a chanced one in a group", Of(roll.Roll({ Ref(200, 20.0f, 1), Item(1, 0.0f, 1) }), 20), 0.2 * 0.5);
        const auto deep = roll.Roll({ Ref(300, 50.0f) });
        Near("a reference in a reference", Of(deep, 10), 0.5 * 0.25);
        Near("...and its own row", Of(deep, 30), 0.5 * 0.1);
        Near("a table that refers to itself ends", Of(roll.Roll({ Ref(400, 100.0f) }), 40), 0.1);
        Near("a reference that is not there: nothing", Of(roll.Roll({ Ref(999, 100.0f) }), 10), 0.0);
    }

    // ---- every source, containers inside ----------------------------------------------------
    {
        Tables refs;
        refs[100] = { Item(10, 0.0f, 1), Item(11, 0.0f, 1) };
        Tables boxes;
        boxes[50] = { Item(60, 10.0f), Ref(100, 100.0f) };
        const Table mob   = { Item(1, 2.0f), Ref(100, 1.0f) };
        const Table boss  = { Item(1, 20.0f) };
        const Table chest = { Item(50, 50.0f), Item(60, 10.0f), Item(70, 5.0f) };
        const auto drops = PbpDrops::Drops({ { &mob, 300 }, { &boss, 1 }, { &chest, 0 }, { nullptr, 5 } }, refs, boxes);
        auto at = [&drops](uint32_t item)
        {
            auto it = drops.find(item);
            return it == drops.end() ? PbpDrops::Drop{} : it->second;
        };
        Near("best: the boss", at(1).best, 0.2f);
        Near("typical: per kill over every one there is", at(1).typical, float((0.02 * 300 + 0.2 * 1) / 301.0));
        Near("best: by a reference, or in a box", at(10).best, 0.25f);
        Near("typical: the creatures only", at(10).typical, 0.005f);
        Near("in a box and by itself", at(60).best, float(1.0 - 0.9 * (1.0 - 0.5 * 0.1)));
        Near("from chests alone: no typical", at(70).typical, 0.0f);
        Near("...so its rarity is the chest's", PbpDrops::Rarity(at(70)), 0.05f);
        Near("...and a creature's is the typical", PbpDrops::Rarity(at(1)), at(1).typical);
        Near("the box itself", at(50).best, 0.5f);
        Near("never dropped: nothing", at(999).best, 0.0);
    }

    // ---- how often, for a person ----------------------------------------------------------
    Eq("1 in 2,000", PbpDrops::OneIn(0.0005), 2000);
    Eq("two figures: 1 in 1,700", PbpDrops::OneIn(0.00058), 1700);
    Eq("small numbers as they are", PbpDrops::OneIn(0.03), 33);
    Eq("every other one", PbpDrops::OneIn(0.5), 2);
    Eq("never", PbpDrops::OneIn(0.0), 0);

    std::printf("%d/%d passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
