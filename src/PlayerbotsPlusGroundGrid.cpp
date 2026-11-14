#include "PlayerbotsPlusGroundGrid.h"

#include <cmath>

namespace PbpGround
{
    namespace
    {
        // Movement::gravity: what the game's falls and jumps take.
        constexpr float kGravity = 19.29110527f;

        bool On(float v, int32_t cell)
        {
            return std::fabs(v - float(cell) * kGrid) < 0.01f;
        }

        uint64_t Part(int32_t v)
        {
            return uint64_t(uint16_t(v + 32768));
        }
    }

    Cell CellOf(float x, float y, float z)
    {
        return { int32_t(std::lround(x / kGrid)), int32_t(std::lround(y / kGrid)), int32_t(std::lround(z / kGrid)) };
    }

    bool OnGrid(float x, float y, float z, Cell& cell)
    {
        cell = CellOf(x, y, z);
        return On(x, cell.x) && On(y, cell.y) && On(z, cell.z);
    }

    uint64_t Key(uint32_t map, Cell const& cell)
    {
        return (uint64_t(map & 0xFFFF) << 48) | (Part(cell.x) << 32) | (Part(cell.y) << 16) | Part(cell.z);
    }

    float FromMiddle2(Cell const& cell, float x, float y, float z)
    {
        float const dx = x - float(cell.x) * kGrid;
        float const dy = y - float(cell.y) * kGrid;
        float const dz = z - float(cell.z) * kGrid;
        return dx * dx + dy * dy + dz * dz;
    }

    uint32_t FallMs(float height)
    {
        if (!(height > 0.0f))
            return 0;
        return uint32_t(std::lround(std::sqrt(2.0f * height / kGravity) * 1000.0f));
    }
}
