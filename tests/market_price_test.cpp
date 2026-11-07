// Tests for the market's pricing (src/PlayerbotsPlusMarketPrice.cpp), which
// has no game types in it. Not part of the server build: AzerothCore only
// compiles a module's src/. Run tests/build.sh.

#include "PlayerbotsPlusMarketPrice.h"

#include <cstdio>
#include <string>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    template <typename A, typename B>
    void Eq(char const* what, A const& got, B const& want)
    {
        ++g_checks;
        if (!(got == want))
        {
            ++g_fails;
            std::printf("FAIL %s: got %s, want %s\n", what, std::to_string(got).c_str(), std::to_string(want).c_str());
        }
    }

    void EqS(char const* what, std::string const& got, std::string const& want)
    {
        ++g_checks;
        if (got != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got '%s', want '%s'\n", what, got.c_str(), want.c_str());
        }
    }

    void True(char const* what, bool ok)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_fails;
            std::printf("FAIL %s\n", what);
        }
    }

    PbpPrice::ItemFacts Gear(uint32_t vendor, uint32_t quality, uint32_t req, uint32_t ilvl)
    {
        PbpPrice::ItemFacts f;
        f.vendor = vendor;
        f.quality = quality;
        f.requiredLevel = req;
        f.itemLevel = ilvl;
        f.gear = true;
        f.maxStack = 1;
        return f;
    }

    PbpPrice::ItemFacts Goods(uint32_t vendor, uint32_t ilvl, uint32_t stack)
    {
        PbpPrice::ItemFacts f;
        f.vendor = vendor;
        f.quality = 1;
        f.itemLevel = ilvl;
        f.gear = false;
        f.maxStack = stack;
        return f;
    }
}

int main()
{
    using namespace PbpPrice;
    Settings s;   // the defaults the module ships with

    // ---- levels and wealth -------------------------------------------------
    Eq("LevelOf required", LevelOf(Gear(100, 2, 45, 50)), 45u);
    Eq("LevelOf item level", LevelOf(Goods(13, 5, 20)), 5u);
    Eq("LevelOf capped", LevelOf(Gear(100, 4, 0, 226)), 80u);
    Eq("LevelOf at least 1", LevelOf(Goods(13, 0, 20)), 1u);

    True("wealth 10 about 2g 59s", Wealth(s, 10) > 25000 && Wealth(s, 10) < 26500);
    True("wealth 60 about 303g", Wealth(s, 60) > 3000000 && Wealth(s, 60) < 3070000);
    True("wealth 80 about 2038g", Wealth(s, 80) > 20000000 && Wealth(s, 80) < 20800000);
    True("wealth grows", Wealth(s, 30) < Wealth(s, 31));

    // ---- value, cap, floor -----------------------------------------------
    Eq("green worth 5x vendor", Value(s, Gear(10000, 2, 40, 45)), uint64_t(50000));
    Eq("epic worth 25x vendor", Value(s, Gear(200000, 4, 80, 213)), uint64_t(5000000));
    Eq("linen worth 3x vendor", Value(s, Goods(13, 5, 20)), uint64_t(39));
    Eq("worth at least vendor + 1", Value(s, Goods(1, 5, 20)), uint64_t(3));
    Eq("floor 110% of vendor", Floor(s, Goods(100, 5, 20)), uint64_t(110));
    Eq("floor at least 1c", Floor(s, Goods(0, 5, 20)), uint64_t(1));
    Eq("green cap at 20: 20% of wealth", Cap(s, Gear(5000, 2, 20, 25), 20), Wealth(s, 20) * 20 / 100);
    Eq("stack cap per one", Cap(s, Goods(13, 5, 20), 5), Wealth(s, 5) * 20 / 100 / 20);

    // ---- listing with a market ---------------------------------------------
    PbpPrice::ItemFacts linen = Goods(13, 5, 20);
    Eq("undercut 1%", ListUnit(s, linen, 100, 0, 0.0, 0.5), uint64_t(99));
    Eq("undercut 5%", ListUnit(s, linen, 100, 0, 0.99, 0.5), uint64_t(95));
    Eq("linen at 10s is capped at 1s 60c", ListUnit(s, linen, 1000, 0, 0.0, 0.5), Cap(s, linen, 5));
    Eq("a copper under, at least", ListUnit(s, Goods(1, 5, 20), 30, 0, 0.0, 0.5), uint64_t(29));
    Eq("market under the floor: not listed", ListUnit(s, Gear(1000, 2, 30, 35), 1050, 0, 0.0, 0.5), uint64_t(0));
    {
        // The market far above what anyone at 20 pays for a green: capped.
        PbpPrice::ItemFacts green20 = Gear(5000, 2, 20, 25);
        Eq("capped at the level's cap", ListUnit(s, green20, 100000000, 0, 0.0, 0.5), Cap(s, green20, 20));
    }

    // ---- listing with no market ----------------------------------------------
    {
        PbpPrice::ItemFacts blue60 = Gear(30000, 3, 60, 65);   // 3g vendor
        const uint64_t value = Value(s, blue60);                // 30g
        const uint64_t low   = ListUnit(s, blue60, 0, 0, 0.0, 0.0);
        const uint64_t high  = ListUnit(s, blue60, 0, 0, 0.0, 0.999);
        Eq("guess 95% of worth", low, value * 95 / 100);
        Eq("guess 115% of worth", high, value * 115 / 100);
    }
    {
        PbpPrice::ItemFacts epic80 = Gear(200000, 4, 80, 213);   // 20g vendor, worth 500g
        const uint64_t price = ListUnit(s, epic80, 0, 0, 0.0, 0.5);
        True("epic at 80 about 500g", price > 4500000 && price < 5600000);
    }

    // ---- unsold, cheaper -------------------------------------------------------
    Eq("15% off after one expiry", ListUnit(s, linen, 100, 1, 0.0, 0.5), uint64_t(99 * 85 / 100));
    Eq("never under the floor after many",
       ListUnit(s, Goods(100, 5, 20), 200, 9, 0.0, 0.5), uint64_t(110));

    // ---- buying ------------------------------------------------------------------
    {
        PbpPrice::ItemFacts green40 = Gear(10000, 2, 40, 45);
        Eq("budget: what it can spare", GearBudget(s, green40, 40, 5000), uint64_t(5000));
        Eq("budget: the cap at its level", GearBudget(s, green40, 40, 100000000), Cap(s, green40, 40));
        Eq("materials: 200% of worth", GoodsBudgetUnit(s, linen, 200), uint64_t(78));
    }

    // ---- money text ----------------------------------------------------------------
    EqS("money gsc", MoneyText(123456), "12g 34s 56c");
    EqS("money silver", MoneyText(100), "1s");
    EqS("money gold", MoneyText(10000), "1g");
    EqS("money zero", MoneyText(0), "0c");
    EqS("money gold copper", MoneyText(10005), "1g 5c");

    std::printf("%d/%d passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
