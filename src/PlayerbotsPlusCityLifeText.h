#ifndef PLAYERBOTS_PLUS_CITY_LIFE_TEXT_H
#define PLAYERBOTS_PLUS_CITY_LIFE_TEXT_H

// --------------------------------------------------------------------------
// The sums behind city life (PlayerbotsPlusHangout, PlayerbotsPlus.CityLife.*),
// with no game types in them, so they can be tested without a server
// (tests/citylife_test.cpp).
// --------------------------------------------------------------------------

#include <cstdint>
#include <string>
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
}

#endif // PLAYERBOTS_PLUS_CITY_LIFE_TEXT_H
