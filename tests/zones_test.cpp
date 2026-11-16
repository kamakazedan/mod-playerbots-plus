// Tests for the sums behind a city's hangout zones
// (src/PlayerbotsPlusZonesText.cpp), which have no game types in them:
// reading tiers and zones, shares that always add up to 100 within their
// limits as they drift, places shared out by them, and the moves between
// zones -- and for what the conf ships: every zone reads, is the code's
// default, word for word, and its spots are hangout spots of one city that
// can add up. Not part of the server build: run tests/build.sh.

#include "PlayerbotsPlusText.h"
#include "PlayerbotsPlusZonesText.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <random>
#include <set>
#include <sstream>
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

    std::string List(std::vector<uint32_t> const& v)
    {
        std::string out;
        for (uint32_t n : v)
            out += (out.empty() ? "" : ",") + std::to_string(n);
        return out;
    }

    void Is(std::string const& what, std::vector<uint32_t> const& got, std::string const& want)
    {
        ++g_checks;
        if (List(got) != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got %s, want %s\n", what.c_str(), List(got).c_str(), want.c_str());
        }
    }

    std::string ReadFile(std::string const& path)
    {
        std::ifstream in(path);
        std::stringstream all;
        all << in.rdbuf();
        return all.str();
    }

    // Every "<prefix><key> = "value"" line of the conf (not commented out).
    std::map<std::string, std::string> ConfLines(std::string const& conf, std::string const& prefix)
    {
        std::map<std::string, std::string> out;
        std::istringstream in(conf);
        for (std::string text; std::getline(in, text);)
        {
            if (text.rfind(prefix, 0) != 0)
                continue;
            const std::string key = text.substr(prefix.size(), text.find(' ') - prefix.size());
            const size_t q1 = text.find('"'), q2 = text.rfind('"');
            out[key] = q1 != std::string::npos && q2 > q1 ? text.substr(q1 + 1, q2 - q1 - 1) : "";
        }
        return out;
    }

    // The entries of a `{ "key", "value" }` list in the code, a value on one
    // line (the key may be on the line before).
    std::map<std::string, std::string> CodeList(std::string const& source, std::string const& name)
    {
        std::map<std::string, std::string> out;
        const size_t start = source.find(name + "[]");
        const size_t end   = source.find("};", start);
        if (start == std::string::npos)
            return out;
        const std::string body = source.substr(start, end - start);
        size_t at = 0;
        while ((at = body.find("{ \"", at)) != std::string::npos)
        {
            const size_t k1 = at + 3, k2 = body.find('"', k1);
            const size_t v1 = body.find('"', k2 + 1) + 1, v2 = body.find('"', v1);
            out[body.substr(k1, k2 - k1)] = body.substr(v1, v2 - v1);
            at = v2;
        }
        return out;
    }

    PbpZones::Tier T(uint32_t a, uint32_t b, uint32_t c)
    {
        return PbpZones::Tier{ a, b, c };
    }

    uint32_t Sum(std::vector<uint32_t> const& v)
    {
        uint32_t n = 0;
        for (uint32_t x : v)
            n += x;
        return n;
    }

    bool Within(std::vector<uint32_t> const& shares, std::vector<PbpZones::Tier> const& tiers)
    {
        if (shares.size() != tiers.size())
            return false;
        for (size_t i = 0; i < tiers.size(); ++i)
            if (shares[i] < tiers[i].min || shares[i] > tiers[i].max)
                return false;
        return true;
    }
}

int main(int argc, char** argv)
{
    const PbpZones::Tier low = T(1, 5, 10), medium = T(10, 15, 20), high = T(50, 65, 80);
    // Orgrimmar's, as the conf ships them: Strength, Spirits, Wisdom, Cleft, Honor, the west road.
    const std::vector<PbpZones::Tier> org = { high, medium, low, low, low, low };
    const PbpZones::Dice first = [](uint32_t) { return 0u; };
    const PbpZones::Dice last  = [](uint32_t n) { return n - 1; };

    // ---- reading them ---------------------------------------------------------------------
    {
        PbpZones::Tier t;
        True("low reads", PbpZones::ParseTier("1 5 10", t) && t.min == 1 && t.start == 5 && t.max == 10);
        True("spaces about", PbpZones::ParseTier("  50  65 80 ", t) && t.min == 50 && t.max == 80);
        True("all nothing", PbpZones::ParseTier("0 0 0", t) && t.max == 0);
        True("all of it", PbpZones::ParseTier("100 100 100", t));
        True("start under the least", !PbpZones::ParseTier("5 1 10", t));
        True("most under the start", !PbpZones::ParseTier("1 10 5", t));
        True("over 100", !PbpZones::ParseTier("1 5 101", t));
        True("negative", !PbpZones::ParseTier("-1 5 10", t));
        True("two numbers", !PbpZones::ParseTier("1 5", t));
        True("four numbers", !PbpZones::ParseTier("1 5 10 20", t));
        True("not whole", !PbpZones::ParseTier("1.5 5 10", t));
        True("words", !PbpZones::ParseTier("low medium high", t));
        True("empty", !PbpZones::ParseTier("", t));

        PbpZones::ZoneLine z;
        True("zone reads", PbpZones::ParseZoneLine("High OrgrimmarValley  OrgrimmarBank", z) && z.tier == "high" &&
                               z.spots.size() == 2 && z.spots[0] == "OrgrimmarValley" && z.spots[1] == "OrgrimmarBank");
        True("one spot", PbpZones::ParseZoneLine("low OrgrimmarHonor", z) && z.spots.size() == 1);
        True("no spot", !PbpZones::ParseZoneLine("low", z));
        True("nothing", !PbpZones::ParseZoneLine("   ", z));
    }

    // ---- can they add up ------------------------------------------------------------------
    True("Orgrimmar's add up", PbpZones::CanAddUp(org));
    True("two high", PbpZones::CanAddUp({ high, high }));
    True("one high: at most 80", !PbpZones::CanAddUp({ high }));
    True("three low: at most 30", !PbpZones::CanAddUp({ low, low, low }));
    True("three high: at least 150", !PbpZones::CanAddUp({ high, high, high }));
    True("one of all of it", PbpZones::CanAddUp({ T(100, 100, 100) }));
    True("no zones", !PbpZones::CanAddUp({}));

    // ---- where they start ---------------------------------------------------------------------
    Is("Orgrimmar starts as written", PbpZones::StartShares(org, first), "65,15,5,5,5,5");
    // Two low and a high start at 75: every point of room up is needed.
    Is("short of 100: up to the most", PbpZones::StartShares({ low, low, high }, first), "10,10,80");
    // Two high start at 130: down to 100, the points taken by room.
    {
        const std::vector<uint32_t> two = PbpZones::StartShares({ high, high }, first);
        Eq("two high add up", Sum(two), 100);
        True("two high within", Within(two, { high, high }));
        Is("first takes the first points down", two, "50,50");
    }
    {
        // A point at a time, by room: the zone with the most room up takes
        // most of them. Dice 0 always takes the first with room.
        std::vector<uint32_t> s = { 60, 15, 5, 5, 5, 5 };   // 95
        PbpZones::AddUpTo100(s, org, first);
        Is("five points up, first with room", s, "65,15,5,5,5,5");
        std::vector<uint32_t> s2 = { 60, 15, 5, 5, 5, 5 };
        PbpZones::AddUpTo100(s2, org, last);
        Is("five points up, last with room", s2, "60,15,5,5,5,10");
        std::vector<uint32_t> over = { 90, 25, 0, 5, 5, 5 };   // out of its limits
        PbpZones::AddUpTo100(over, org, first);
        True("brought within", Within(over, org) && Sum(over) == 100);
        std::vector<uint32_t> cannot = { 90, 1 };
        PbpZones::AddUpTo100(cannot, { high, low }, first);
        Is("cannot add up: only within", cannot, "80,1");
        std::vector<uint32_t> missing;
        PbpZones::AddUpTo100(missing, org, first);
        True("missing shares start at nothing and add up", Within(missing, org) && Sum(missing) == 100);
    }

    // ---- drifting ------------------------------------------------------------------------------
    {
        const std::vector<uint32_t> at = { 65, 15, 5, 5, 5, 5 };
        Is("no step, no pull: as it was", PbpZones::Drift(at, org, 0, 0, first), "65,15,5,5,5,5");
        Is("all the way back", PbpZones::Drift({ 50, 20, 10, 10, 1, 9 }, org, 0, 100, first), "65,15,5,5,5,5");
        // Half way back, the way back rounded half away from nothing: 80 ->
        // 72 (7.5 back, so 8), 10 -> 13, 1 -> 3, 2 -> 4, 2 -> 4, 5 -> 5: 101,
        // so a point comes off, here the first's (dice 0).
        const std::vector<uint32_t> half = PbpZones::Drift({ 80, 10, 1, 2, 2, 5 }, org, 0, 50, first);
        True("half way back: adds up within", Within(half, org) && Sum(half) == 100);
        Is("half way back", half, "71,13,3,4,4,5");
        // A step only: dice n-1 is +step for each, then taken back down.
        const std::vector<uint32_t> up = PbpZones::Drift(at, org, 3, 0, last);
        True("a step up for all, then down to 100", Within(up, org) && Sum(up) == 100);

        // Thousands of drifts: always 100, always within; every zone moves
        // about its limits, and the big one stays about where it started.
        std::mt19937 rng(20261116);
        const PbpZones::Dice dice = [&rng](uint32_t n) { return std::uniform_int_distribution<uint32_t>(0, n - 1)(rng); };
        std::vector<uint32_t> s = PbpZones::StartShares(org, dice);
        bool alwaysOk = true;
        double strength = 0.0;
        std::vector<std::set<uint32_t>> seen(org.size());
        uint64_t moved = 0;
        const int rounds = 20000;
        for (int i = 0; i < rounds; ++i)
        {
            const std::vector<uint32_t> next = PbpZones::Drift(s, org, 3, 10, dice);
            alwaysOk = alwaysOk && Within(next, org) && Sum(next) == 100;
            for (size_t z = 0; z < org.size(); ++z)
            {
                seen[z].insert(next[z]);
                moved += next[z] > s[z] ? next[z] - s[z] : s[z] - next[z];
            }
            strength += next[0];
            s = next;
        }
        True("drifts: always 100, always within", alwaysOk);
        const double mean = strength / rounds;
        True("Strength stays about 65: " + std::to_string(mean), mean > 60.0 && mean < 70.0);
        True("Strength goes high and low", *seen[0].begin() <= 56 && *seen[0].rbegin() >= 74);
        True("Spirits goes all the way", seen[1].size() == 11);
        for (size_t z = 2; z < org.size(); ++z)
            True("a low zone goes all the way: " + std::to_string(z), seen[z].size() == 10);
        const double perShift = double(moved) / rounds;
        True("a few points a shift: " + std::to_string(perShift), perShift > 4.0 && perShift < 16.0);
    }

    // ---- places ------------------------------------------------------------------------------------
    {
        // Orgrimmar on this realm: 120 places, 19 of them outside the zones.
        const std::vector<std::vector<uint32_t>> written = {
            { 18, 11, 14, 26, 16 }, { 12 }, { 8 }, { 12, 6, 4 }, { 10 }, { 8 } };
        const std::vector<std::vector<uint32_t>> at = PbpZones::SpotPlaces(101, { 65, 15, 5, 5, 5, 5 }, written);
        Eq("zones", at.size(), 6);
        Is("Strength by capacity", at[0], "14,9,11,20,12");
        Is("Spirits", at[1], "15");
        Is("the Cleft: most at its mouth", at[3], "3,1,1");
        uint32_t all = 0;
        for (auto const& zone : at)
            all += Sum(zone);
        Eq("every place given", all, 101);
        Is("capacities all nothing: by turns", PbpZones::SpotPlaces(3, { 100 }, { { 0, 0 } })[0], "2,1");
        Is("no places", PbpZones::SpotPlaces(0, { 50, 50 }, { { 1 }, { 1 } })[1], "0");
        Eq("the most at 80 percent of 101", PbpZones::AtMost(101, 80), 81);
        Eq("the most at 10 percent of 101", PbpZones::AtMost(101, 10), 11);
        Eq("the most at 0", PbpZones::AtMost(101, 0), 0);
        Eq("the most at all of it", PbpZones::AtMost(101, 100), 101);
    }

    // ---- moves between zones ---------------------------------------------------------------------------
    {
        using C = PbpZones::Count;
        auto text = [](std::vector<std::pair<size_t, size_t>> const& moves)
        {
            std::string out;
            for (auto const& [from, to] : moves)
                out += (out.empty() ? "" : " ") + std::to_string(from) + ">" + std::to_string(to);
            return out;
        };
        auto check = [&text](std::string const& what, std::vector<std::pair<size_t, size_t>> const& got,
                             std::string const& want)
        {
            ++g_checks;
            if (text(got) != want)
            {
                ++g_fails;
                std::printf("FAIL %s: got '%s', want '%s'\n", what.c_str(), text(got).c_str(), want.c_str());
            }
        };
        const std::vector<C> zones = { { 60, 66, 4 }, { 20, 15, 2 }, { 5, 5, 1 }, { 16, 14, 0 } };
        check("dice first: the first over to the first short", PbpZones::PlanMoves(zones, 3, first), "0>1 0>1 0>1");
        check("dice last: the last short, until it is not", PbpZones::PlanMoves(zones, 3, last), "0>3 0>3 0>1");
        check("on until nobody over can move", PbpZones::PlanMoves(zones, 10, first), "0>1 0>1 0>1 0>1");
        check("none at once", PbpZones::PlanMoves(zones, 0, first), "");
        check("even: none", PbpZones::PlanMoves({ { 5, 5, 3 }, { 10, 10, 3 } }, 3, first), "");
        check("over but nobody free", PbpZones::PlanMoves({ { 5, 9, 0 }, { 10, 6, 3 } }, 3, first), "");
        check("over, nowhere short", PbpZones::PlanMoves({ { 5, 9, 4 }, { 10, 10, 3 } }, 3, first), "");
        // However far over or short: any of them, as the dice fall.
        const std::vector<C> four = { { 5, 7, 2 }, { 5, 9, 2 }, { 5, 4, 0 }, { 5, 2, 0 } };
        check("any over to any short (first)", PbpZones::PlanMoves(four, 3, first), "0>2 0>3 1>3");
        check("any over to any short (last)", PbpZones::PlanMoves(four, 3, last), "1>3 1>3 0>3");
        // Someone over with nobody free leaves it to the others over.
        check("only those with someone free", PbpZones::PlanMoves({ { 5, 9, 0 }, { 5, 6, 1 }, { 5, 3, 0 } }, 3, first),
              "1>2");
        // Every zone over gives and every zone short takes, all alike: one
        // just over as likely as one far over, so anyone may cross the city.
        std::mt19937 rng(161126);
        const PbpZones::Dice dice = [&rng](uint32_t n)
        { return std::uniform_int_distribution<uint32_t>(0, n - 1)(rng); };
        const std::vector<C> six = { { 10, 30, 20 }, { 10, 12, 20 }, { 10, 11, 20 }, { 10, 0, 0 }, { 10, 8, 0 },
                                     { 10, 9, 0 } };
        std::map<std::string, int> pairs;
        for (int i = 0; i < 9000; ++i)
            ++pairs[text(PbpZones::PlanMoves(six, 1, dice))];
        Eq("every pair of over and short comes up", pairs.size(), 9);
        bool alike = true;
        for (auto const& [pair, n] : pairs)
            alike = alike && n > 850 && n < 1150;
        True("each pair about as likely", alike);
    }

    // ---- what the conf ships -------------------------------------------------------------------------
    const std::string root = argc > 1 ? argv[1] : ".";
    const std::string conf   = ReadFile(root + "/conf/mod_playerbots_plus.conf.dist");
    const std::string config = ReadFile(root + "/src/PlayerbotsPlusConfig.cpp");
    True("the conf is there", !conf.empty());
    True("the config source is there", !config.empty());
    {
        const auto tiers     = ConfLines(conf, "PlayerbotsPlus.Hangout.ZoneTier.");
        const auto codeTiers = CodeList(config, "kDefaultZoneTiers");
        Eq("tiers in the conf (three, and Stormwind's own high and low)", tiers.size(), 5);
        True("the conf's tiers are the code's", tiers == codeTiers);
        std::map<std::string, PbpZones::Tier> byName;
        for (auto const& [name, value] : tiers)
        {
            PbpZones::Tier t;
            True("tier reads: " + name, PbpZones::ParseTier(value, t));
            std::string lower = name;
            std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return std::tolower(c); });
            byName[lower] = t;
        }

        const auto zones     = ConfLines(conf, "PlayerbotsPlus.Hangout.Zone.");
        const auto codeZones = CodeList(config, "kDefaultHangoutZones");
        Eq("zones in the conf (Orgrimmar's six, Stormwind's nine)", zones.size(), 15);
        True("the conf's zones are the code's", zones == codeZones);

        const auto spots = ConfLines(conf, "PlayerbotsPlus.Hangout.Spot.");
        std::string duels;
        {
            std::istringstream in(conf);
            for (std::string text; std::getline(in, text);)
                if (text.rfind("PlayerbotsPlus.Duels.Spots = ", 0) == 0)
                    duels = text;
        }
        std::map<std::string, std::vector<PbpZones::Tier>> byCity;
        std::set<std::string> taken;
        for (auto const& [key, value] : zones)
        {
            PbpZones::ZoneLine line;
            True("zone reads: " + key, PbpZones::ParseZoneLine(value, line));
            True("zone's tier is one: " + key, byName.contains(line.tier));
            std::string city;
            for (std::string const& spotKey : line.spots)
            {
                auto spot = spots.find(spotKey);
                PbpSpotLine s;
                const bool there = spot != spots.end() && PbpParseSpotLine(spot->second, s);
                True(key + ": " + spotKey + " is a spot", there);
                if (!there)
                    continue;
                const std::string in = PbpCityOf(s.name);
                True(key + ": " + spotKey + " is in its city", city.empty() || in == city);
                city = in;
                True(key + ": " + spotKey + " is in no other zone", taken.insert(spotKey).second);
                True(key + ": " + spotKey + " is not where duels are", duels.find(spotKey + "|") == std::string::npos &&
                                                                       duels.find(spotKey + "\"") == std::string::npos);
            }
            True(key + " is named for its city", key.rfind(city + ".", 0) == 0);
            // The city's own numbers for the tier, if it has them (as the code reads them).
            std::string own = key.substr(0, key.find('.')) + "." + line.tier;
            std::transform(own.begin(), own.end(), own.begin(), [](unsigned char c) { return std::tolower(c); });
            byCity[city].push_back(byName.contains(own) ? byName[own] : byName[line.tier]);
        }
        for (auto const& [city, tiers] : byCity)
        {
            True(city + "'s zones add up", PbpZones::CanAddUp(tiers));
            True(city + " has three to ten zones", tiers.size() >= 3 && tiers.size() <= 10);
            uint32_t starts = 0;
            for (PbpZones::Tier const& t : tiers)
                starts += t.start;
            Eq(city + "'s zones start at 100 in all", starts, 100);
        }
        Eq("Stormwind's zones", byCity["Stormwind"].size(), 9);
        // Every spot in Orgrimmar and Stormwind is in a zone; outside their
        // walls, the gates (and Orgrimmar's zeppelin tower) are not.
        for (auto const& [key, value] : spots)
        {
            PbpSpotLine s;
            if (!PbpParseSpotLine(value, s) || (PbpCityOf(s.name) != "Orgrimmar" && PbpCityOf(s.name) != "Stormwind"))
                continue;
            const bool outside = key == "OrgrimmarGates" || key == "OrgrimmarZeppelins" || key == "StormwindGates";
            True(PbpCityOf(s.name) + "'s " + key + (outside ? " is in no zone" : " is in a zone"),
                 taken.contains(key) != outside);
        }
    }

    std::printf("%d/%d passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
