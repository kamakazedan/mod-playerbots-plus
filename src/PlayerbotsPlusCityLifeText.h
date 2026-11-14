#ifndef PLAYERBOTS_PLUS_CITY_LIFE_TEXT_H
#define PLAYERBOTS_PLUS_CITY_LIFE_TEXT_H

// --------------------------------------------------------------------------
// The sums behind city life (PlayerbotsPlusHangout, PlayerbotsPlus.CityLife.*),
// with no game types in them, so they can be tested without a server
// (tests/citylife_test.cpp).
// --------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace PbpCityLife
{
    // How a bot hanging out spends it: one that stays put the whole time, one
    // that shifts about every minute or two, or one that cannot keep still.
    enum class Temper : uint8_t
    {
        Parked,
        Shuffler,
        Restless
    };

    // From a roll of 1-100: Parked for the first `parkedShare`, Restless for
    // the next `restlessShare`, Shuffler for the rest.
    Temper PickTemper(uint32_t roll, uint32_t parkedShare, uint32_t restlessShare);

    // A mount at most `maxOwners` of those online have is a rare one (0: none
    // is). `owners` counts the rider too.
    bool Rare(uint32_t owners, uint32_t maxOwners);

    // How rare, said to a bot: "nobody else about has one", "only one other
    // person about has one", "only 3 others about have one". `owners`
    // counts the rider.
    std::string RareText(uint32_t owners);

    // `n` people round someone, spread out evenly from `start` (radians), each
    // nudged by `jitter[i]` (-1..1, from the caller's dice) times a third of
    // the gap between them. Fewer jitters than `n`: the rest are not nudged.
    std::vector<float> RingAngles(uint32_t n, float start, std::vector<float> const& jitter);

    // The ground speed for a hop of `dist` yards that is in the air as long
    // as a player's jump is (up at WoW's 7.96 yd/s against its 19.29 yd/s/s
    // of gravity: 0.83 s). Never under 0.2: the core refuses a slower jump.
    float HopSpeed(float dist);
    constexpr float kHopRise = 7.96f;   // a player's jump, straight up, yd/s

    // `lo`..`hi` seconds, `roll` 0..1 picking where in it. lo > hi swaps.
    uint32_t Between(uint32_t lo, uint32_t hi, double roll);

    // A temperament (PlayerbotsPlus.CityLife.Temperament.<Name>): how a bot
    // of that kind spends its time hanging out. Every one a percentage.
    struct Temperament
    {
        uint32_t parked   = 30;   // of its visits, it stays put the whole time
        uint32_t restless = 15;   // ...it cannot keep still
        uint32_t jump     = 12;   // of its moves on foot, a few hops on the spot
        uint32_t rear     = 15;   // ...on a mount, its mount rearing up or roaring
        uint32_t wander   = 0;    // ...a run off somewhere near, and back
        uint32_t showOff  = 0;    // every ShowOffMinutes or so, the chance of a bout of showing off
        uint32_t bench    = 60;   // it sits on a free bench (Hangout.BenchChance)
        uint32_t mounted  = 45;   // ...else on its mount, where it can ride (Hangout.MountedChance)
        uint32_t sit      = 35;   // ...else on the ground (Hangout.SitChance)
        uint32_t afk      = 35;   // it goes AFK there (Hangout.AfkChance)
    };

    // "parked:10|restless:60|jump:60": `base` with those changed (0 to 100).
    // A part that is not a known key with such a number goes to `bad`,
    // comma-separated, and changes nothing.
    Temperament ParseTemperament(std::string const& text, Temperament base, std::string& bad);

    // "calm:25|lively:20|clown:5": who gets which when nothing says, by
    // percent, names in lower case. Bad parts go to `bad`.
    std::vector<std::pair<std::string, uint32_t>> ParseShares(std::string const& text, std::string& bad);

    // From `roll` 1..100: the first of `shares` it falls in, in order; ""
    // (the default) for the rest.
    std::string PickTemperament(uint32_t roll, std::vector<std::pair<std::string, uint32_t>> const& shares);

    // 1..100, always the same for the same bot: its temperament does not
    // change from one visit to the next.
    uint32_t StableRoll(uint64_t guid);

    // `name` in lower case, trimmed: how temperaments are matched.
    std::string TemperamentKey(std::string const& name);
}

#endif // PLAYERBOTS_PLUS_CITY_LIFE_TEXT_H
