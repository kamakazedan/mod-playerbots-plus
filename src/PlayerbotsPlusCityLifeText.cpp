#include "PlayerbotsPlusCityLifeText.h"

#include <algorithm>
#include <cctype>
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

    namespace
    {
        std::string Trim(std::string const& s)
        {
            size_t const a = s.find_first_not_of(" \t\r\n");
            if (a == std::string::npos)
                return {};
            size_t const b = s.find_last_not_of(" \t\r\n");
            return s.substr(a, b - a + 1);
        }

        std::vector<std::string> Parts(std::string const& text)
        {
            std::vector<std::string> out;
            size_t from = 0;
            while (from <= text.size())
            {
                size_t const bar = text.find('|', from);
                std::string const part = Trim(text.substr(from, bar == std::string::npos ? std::string::npos
                                                                                         : bar - from));
                if (!part.empty())
                    out.push_back(part);
                if (bar == std::string::npos)
                    break;
                from = bar + 1;
            }
            return out;
        }

        // "key:number" with a number 0 to 100.
        bool KeyNumber(std::string const& part, std::string& key, uint32_t& value)
        {
            size_t const colon = part.find(':');
            if (colon == std::string::npos)
                return false;
            key = TemperamentKey(part.substr(0, colon));
            std::string const digits = Trim(part.substr(colon + 1));
            if (key.empty() || digits.empty() || digits.size() > 3 ||
                !std::all_of(digits.begin(), digits.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return false;
            value = uint32_t(std::stoul(digits));
            return value <= 100;
        }

        void Note(std::string& bad, std::string const& part)
        {
            bad += (bad.empty() ? "" : ", ") + part;
        }
    }

    std::string TemperamentKey(std::string const& name)
    {
        std::string out = Trim(name);
        for (char& c : out)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return out;
    }

    Temperament ParseTemperament(std::string const& text, Temperament base, std::string& bad)
    {
        for (std::string const& part : Parts(text))
        {
            std::string key;
            uint32_t value = 0;
            if (!KeyNumber(part, key, value))
            {
                Note(bad, part);
                continue;
            }
            uint32_t* field = key == "parked"   ? &base.parked
                            : key == "restless" ? &base.restless
                            : key == "jump"     ? &base.jump
                            : key == "rear"     ? &base.rear
                            : key == "wander"   ? &base.wander
                            : key == "showoff"  ? &base.showOff
                            : key == "bench"    ? &base.bench
                            : key == "mounted"  ? &base.mounted
                            : key == "sit"      ? &base.sit
                            : key == "afk"      ? &base.afk
                                                : nullptr;
            if (!field)
            {
                Note(bad, part);
                continue;
            }
            *field = value;
        }
        return base;
    }

    std::vector<std::pair<std::string, uint32_t>> ParseShares(std::string const& text, std::string& bad)
    {
        std::vector<std::pair<std::string, uint32_t>> out;
        for (std::string const& part : Parts(text))
        {
            std::string key;
            uint32_t value = 0;
            if (KeyNumber(part, key, value))
                out.emplace_back(key, value);
            else
                Note(bad, part);
        }
        return out;
    }

    std::string PickTemperament(uint32_t roll, std::vector<std::pair<std::string, uint32_t>> const& shares)
    {
        uint32_t upTo = 0;
        for (auto const& [name, share] : shares)
        {
            upTo += share;
            if (roll <= upTo)
                return name;
        }
        return {};
    }

    uint32_t StableRoll(uint64_t guid)
    {
        // splitmix64's finish: next-door guids land far apart.
        uint64_t z = guid + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return uint32_t(z % 100) + 1;
    }
}
