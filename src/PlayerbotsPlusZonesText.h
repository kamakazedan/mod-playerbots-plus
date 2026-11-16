#ifndef PLAYERBOTS_PLUS_ZONES_TEXT_H
#define PLAYERBOTS_PLUS_ZONES_TEXT_H

// --------------------------------------------------------------------------
// The sums behind a city's hangout zones (PlayerbotsPlusHangout,
// PlayerbotsPlus.Hangout.Zone.*), with no game types in them, so they can be
// tested without a server (tests/zones_test.cpp).
//
// A zoned city's people are shared out among its zones (three to six of
// them) by a share each, in whole percent, always adding up to 100, each
// within its tier's limits (Hangout.ZoneTier.*: low 1-10, medium 10-20,
// high 50-80). Every Hangout.ZoneShiftSeconds the shares drift: each goes
// Hangout.ZonePull percent of the way back to where it started, then up or
// down a random step of up to Hangout.ZoneShiftStep points, within its
// limits; and whatever that comes to over or under 100 is taken from or
// given to the zones a point at a time, more likely the more room a zone
// has for it. A zone's places are its share of the city's, and each of its
// spots has its part of those by its written capacity. Then bots move from
// a zone with more there than its room to one with less (PlanMoves).
// --------------------------------------------------------------------------

#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace PbpZones
{
    // A zone's limits, in percent of its city's people: the least it holds,
    // where it starts, the most ("1 5 10").
    struct Tier
    {
        uint32_t min   = 0;
        uint32_t start = 0;
        uint32_t max   = 0;
    };

    // "min start max", whole numbers, min <= start <= max <= 100. False for
    // anything else.
    bool ParseTier(std::string const& text, Tier& out);

    // A zone as the conf writes it: "<tier> <spot key> <spot key> ...", the
    // tier lower-cased. False with no tier, or no spot.
    struct ZoneLine
    {
        std::string              tier;
        std::vector<std::string> spots;
    };
    bool ParseZoneLine(std::string const& text, ZoneLine& out);

    // Whether shares within these limits can add up to 100 at all: the least
    // of them no more than 100, the most no less. False for no zones.
    bool CanAddUp(std::vector<Tier> const& tiers);

    // Dice: a whole number from 0 to n - 1, for n > 0.
    using Dice = std::function<uint32_t(uint32_t n)>;

    // `shares` (one for each tier, missing ones 0) brought within their
    // limits, then to 100 in all, a point at a time: each point given to, or
    // taken from, a zone with room for it, picked by `dice`, the more room
    // the likelier. Limits that cannot add up (CanAddUp): only within them.
    void AddUpTo100(std::vector<uint32_t>& shares, std::vector<Tier> const& tiers, Dice const& dice);

    // Where the shares start: each zone's start, made to add up to 100.
    std::vector<uint32_t> StartShares(std::vector<Tier> const& tiers, Dice const& dice);

    // One drift: each share `pull` percent of the way back to its start
    // (rounded to the nearest point, half away from nothing), then a step
    // of -step..+step from `dice`, within its limits; then made to add up to
    // 100. A share missing from `shares` drifts from its start.
    std::vector<uint32_t> Drift(std::vector<uint32_t> const& shares, std::vector<Tier> const& tiers, uint32_t step,
                                uint32_t pull, Dice const& dice);

    // Each zone's places, `places` shared out by the shares (the largest
    // remainders taking the odd ones), and each zone's among its spots by
    // their written capacities (`written`, one list per zone; alike when
    // they are all 0).
    std::vector<std::vector<uint32_t>> SpotPlaces(uint32_t places, std::vector<uint32_t> const& shares,
                                                  std::vector<std::vector<uint32_t>> const& written);

    // The most of `places` a zone holds at `percent` of them: rounded up.
    uint32_t AtMost(uint32_t places, uint32_t percent);

    // How a zone stands now: its room, how many are there or on their way,
    // and of those how many could be moved.
    struct Count
    {
        uint32_t room    = 0;
        uint32_t there   = 0;
        uint32_t movable = 0;
    };

    // Up to `cap` moves of one bot each (from, to), from a zone with more
    // there than its room to one with fewer: each time from the zone furthest
    // over that has someone to move, to the one furthest under (the first of
    // equals). Fewer once the zones even out, or nobody over can be moved.
    std::vector<std::pair<size_t, size_t>> PlanMoves(std::vector<Count> zones, uint32_t cap);
}

#endif // PLAYERBOTS_PLUS_ZONES_TEXT_H
