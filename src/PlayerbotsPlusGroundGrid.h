#ifndef PLAYERBOTS_PLUS_GROUND_GRID_H
#define PLAYERBOTS_PLUS_GROUND_GRID_H

// --------------------------------------------------------------------------
// Feet on the ground's sums, with no game types in them so they can be
// tested without a server (tests/ground_test.cpp): playerbots' grid of grind
// spots, which spawn stands for a square of it, and how long a fall takes.
//
// The game side is PlayerbotsPlusGround.
// --------------------------------------------------------------------------

#include <cstdint>

namespace PbpGround
{
    // Playerbots' grind spots (TravelMgr's level cache, built from the
    // creatures' spawns): a grid 50 yards a side, the height rounded to 50
    // as well.
    constexpr float kGrid = 50.0f;

    struct Cell
    {
        int32_t x = 0, y = 0, z = 0;

        bool operator==(Cell const&) const = default;
    };

    // The square a point is in, rounded as playerbots rounds it
    // (std::lround(v / 50): halves away from zero).
    Cell CellOf(float x, float y, float z);

    // A point playerbots made out of a square -- its middle: x, y and z all
    // on the grid. A real position never is, in all three.
    bool OnGrid(float x, float y, float z, Cell& cell);

    // A square on a map, as one number.
    uint64_t Key(uint32_t map, Cell const& cell);

    // How far a point is from a square's middle, squared: of the spawns in a
    // square, the one nearest its middle stands for it.
    float FromMiddle2(Cell const& cell, float x, float y, float z);

    // How long a fall of `height` yards takes from standing still, in
    // milliseconds (the game's gravity). 0 for none.
    uint32_t FallMs(float height);
}

#endif // PLAYERBOTS_PLUS_GROUND_GRID_H
