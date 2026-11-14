// Tests for city life's sums (src/PlayerbotsPlusCityLifeText.cpp), which
// have no game types in them: a hanging-out bot's temper, what makes a mount
// rare and how that is said, the ring of people round someone, and how fast
// a hop goes. Not part of the server build: run tests/build.sh.

#include "PlayerbotsPlusCityLifeText.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    void Eq(std::string const& what, uint64_t got, uint64_t want)
    {
        ++g_checks;
        if (got != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got %llu, want %llu\n", what.c_str(), (unsigned long long)got,
                        (unsigned long long)want);
        }
    }

    void Is(std::string const& what, std::string const& got, std::string const& want)
    {
        ++g_checks;
        if (got != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got '%s', want '%s'\n", what.c_str(), got.c_str(), want.c_str());
        }
    }

    void Near(std::string const& what, double got, double want, double within = 0.001)
    {
        ++g_checks;
        if (std::fabs(got - want) > within)
        {
            ++g_fails;
            std::printf("FAIL %s: got %f, want %f\n", what.c_str(), got, want);
        }
    }
}

int main()
{
    using namespace PbpCityLife;
    auto t = [](Temper x) { return uint64_t(x); };

    // ---- temper -------------------------------------------------------------------------
    Eq("roll 1, 30 parked: parked", t(PickTemper(1, 30, 15)), t(Temper::Parked));
    Eq("roll 30, 30 parked: parked", t(PickTemper(30, 30, 15)), t(Temper::Parked));
    Eq("roll 31: restless", t(PickTemper(31, 30, 15)), t(Temper::Restless));
    Eq("roll 45: restless", t(PickTemper(45, 30, 15)), t(Temper::Restless));
    Eq("roll 46: shuffler", t(PickTemper(46, 30, 15)), t(Temper::Shuffler));
    Eq("roll 100: shuffler", t(PickTemper(100, 30, 15)), t(Temper::Shuffler));
    Eq("none parked, none restless: shuffler", t(PickTemper(1, 0, 0)), t(Temper::Shuffler));
    Eq("all parked", t(PickTemper(100, 100, 50)), t(Temper::Parked));
    Eq("shares over 100: restless gets what is left", t(PickTemper(95, 90, 50)), t(Temper::Restless));

    // ---- rare mounts --------------------------------------------------------------------
    Eq("one owner, three allowed: rare", Rare(1, 3), 1);
    Eq("three owners, three allowed: rare", Rare(3, 3), 1);
    Eq("four owners: not", Rare(4, 3), 0);
    Eq("none allowed: never", Rare(1, 0), 0);
    Eq("no owners counted: not", Rare(0, 3), 0);
    Is("only the rider", RareText(1), "nobody else about has one");
    Is("the rider and one more", RareText(2), "only one other person about has one");
    Is("the rider and three more", RareText(4), "only 3 others about have one");

    // ---- the ring ------------------------------------------------------------------------
    std::vector<float> const four = RingAngles(4, 0.0f, {});
    Eq("four round someone", four.size(), 4);
    Near("the first where it starts", four[0], 0.0);
    Near("a quarter round", four[1], 1.5707963);
    Near("half round", four[2], 3.1415926);
    std::vector<float> const nudged = RingAngles(2, 6.0f, { 1.0f, -1.0f });
    Near("wraps past a full turn, nudged a third of the gap", nudged[0], std::fmod(6.0 + 3.1415926 / 3.0, 6.2831853));
    Near("the second nudged back", nudged[1], std::fmod(6.0 + 3.1415926 - 3.1415926 / 3.0, 6.2831853));
    Eq("nobody: no angles", RingAngles(0, 1.0f, {}).size(), 0);
    Near("jitter past 1 is held to 1", RingAngles(2, 0.0f, { 5.0f })[0], 3.1415926 / 3.0);

    // ---- hops ----------------------------------------------------------------------------
    Near("a one yard hop: as long in the air as a jump", 1.0 / HopSpeed(1.0f), 2.0 * 7.96 / 19.29);
    Near("on the spot: the slowest the core takes", HopSpeed(0.0f), 0.2);

    // ---- between ---------------------------------------------------------------------------
    Eq("roll 0: the low end", Between(20, 40, 0.0), 20);
    Eq("roll 1: the high end", Between(20, 40, 1.0), 40);
    Eq("roll 0.5: the middle", Between(20, 40, 0.5), 30);
    Eq("swapped ends", Between(40, 20, 0.0), 20);
    Eq("one value", Between(7, 7, 0.9), 7);

    // ---- temperaments ---------------------------------------------------------------------
    {
        Temperament base;
        base.parked = 15;
        base.jump   = 50;
        std::string bad;
        Temperament const clown = ParseTemperament("parked:0| Restless:60 |jump:70|showoff:70|mounted:80", base, bad);
        Is("a clean line: nothing bad", bad, "");
        Eq("parked set", clown.parked, 0);
        Eq("key case and spaces do not matter", clown.restless, 60);
        Eq("jump set", clown.jump, 70);
        Eq("showoff set", clown.showOff, 70);
        Eq("mounted set", clown.mounted, 80);
        Eq("rear kept from the base", clown.rear, base.rear);
        Eq("afk kept from the base", clown.afk, base.afk);

        bad.clear();
        Temperament const odd = ParseTemperament("jump:150|nap:5|rear|sit:x|wander:30", base, bad);
        Is("bad parts named", bad, "jump:150, nap:5, rear, sit:x");
        Eq("a bad number changes nothing", odd.jump, 50);
        Eq("the good part still counts", odd.wander, 30);
        bad.clear();
        Eq("empty: the base", ParseTemperament("", base, bad).parked, 15);
        Is("empty: nothing bad", bad, "");

        bad.clear();
        auto const shares = ParseShares("Calm:25|lively:20| clown:5 |odd", bad);
        Eq("three shares", shares.size(), 3);
        Is("names in lower case", shares[0].first, "calm");
        Is("bad share named", bad, "odd");
        Is("roll 1: the first", PickTemperament(1, shares), "calm");
        Is("roll 25: still the first", PickTemperament(25, shares), "calm");
        Is("roll 26: the second", PickTemperament(26, shares), "lively");
        Is("roll 50: the third", PickTemperament(50, shares), "clown");
        Is("roll 51: the default", PickTemperament(51, shares), "");
        Is("no shares: the default", PickTemperament(1, {}), "");

        // The same bot, the same roll; bots spread over all of 1..100.
        Eq("stable", StableRoll(4968), StableRoll(4968));
        uint32_t low = 101, high = 0, firstTen = 0;
        for (uint64_t g = 1; g <= 2000; ++g)
        {
            uint32_t const r = StableRoll(g);
            low  = std::min(low, r);
            high = std::max(high, r);
            firstTen += r <= 10 ? 1 : 0;
        }
        Eq("lowest roll", low, 1);
        Eq("highest roll", high, 100);
        Eq("about a tenth in 1..10", firstTen > 140 && firstTen < 260, 1);
        Is("key: trimmed, lower case", TemperamentKey("  Clown "), "clown");
    }

    std::printf("%d/%d passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
