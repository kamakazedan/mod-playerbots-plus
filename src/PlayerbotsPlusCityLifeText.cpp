#include "PlayerbotsPlusCityLifeText.h"

#include <algorithm>
#include <cmath>

namespace PbpCityLife
{
    Temper PickTemper(uint32_t roll, uint32_t parkedShare, uint32_t restlessShare)
    {
        parkedShare   = std::min<uint32_t>(parkedShare, 100);
        restlessShare = std::min<uint32_t>(restlessShare, 100 - parkedShare);
        if (roll <= parkedShare)
            return Temper::Parked;
        if (roll <= parkedShare + restlessShare)
            return Temper::Restless;
        return Temper::Shuffler;
    }

    bool Rare(uint32_t owners, uint32_t maxOwners)
    {
        return maxOwners > 0 && owners > 0 && owners <= maxOwners;
    }

    std::string RareText(uint32_t owners)
    {
        if (owners <= 1)
            return "nobody else about has one";
        if (owners == 2)
            return "only one other person about has one";
        return "only " + std::to_string(owners - 1) + " others about have one";
    }

    std::vector<float> RingAngles(uint32_t n, float start, std::vector<float> const& jitter)
    {
        constexpr float kTwoPi = 6.2831853f;
        std::vector<float> out;
        if (!n)
            return out;
        float const gap = kTwoPi / float(n);
        for (uint32_t i = 0; i < n; ++i)
        {
            float const nudge = i < jitter.size() ? std::clamp(jitter[i], -1.0f, 1.0f) * gap / 3.0f : 0.0f;
            float a = std::fmod(start + gap * float(i) + nudge, kTwoPi);
            if (a < 0.0f)
                a += kTwoPi;
            out.push_back(a);
        }
        return out;
    }

    float HopSpeed(float dist)
    {
        constexpr float kGravity = 19.29f;
        float const airborne = 2.0f * kHopRise / kGravity;
        return std::max(0.2f, std::max(0.0f, dist) / airborne);
    }

    uint32_t Between(uint32_t lo, uint32_t hi, double roll)
    {
        if (lo > hi)
            std::swap(lo, hi);
        roll = std::clamp(roll, 0.0, 1.0);
        uint32_t const span = hi - lo;
        return lo + std::min<uint32_t>(span, uint32_t(std::floor(roll * double(span + 1))));
    }
}
