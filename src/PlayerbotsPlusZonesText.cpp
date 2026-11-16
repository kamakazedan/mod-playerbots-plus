#include "PlayerbotsPlusZonesText.h"
#include "PlayerbotsPlusText.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace PbpZones
{
    bool ParseTier(std::string const& text, Tier& out)
    {
        std::istringstream in(text);
        long long least = -1, start = -1, most = -1;
        std::string more;
        if (!(in >> least >> start >> most) || (in >> more))
            return false;
        if (least < 0 || start < least || most < start || most > 100)
            return false;
        out = Tier{ uint32_t(least), uint32_t(start), uint32_t(most) };
        return true;
    }

    bool ParseZoneLine(std::string const& text, ZoneLine& out)
    {
        std::istringstream in(text);
        ZoneLine line;
        if (!(in >> line.tier))
            return false;
        for (char& c : line.tier)
            c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        for (std::string key; in >> key;)
            line.spots.push_back(key);
        if (line.spots.empty())
            return false;
        out = std::move(line);
        return true;
    }

    bool CanAddUp(std::vector<Tier> const& tiers)
    {
        uint64_t least = 0, most = 0;
        for (Tier const& t : tiers)
        {
            least += t.min;
            most += t.max;
        }
        return !tiers.empty() && least <= 100 && most >= 100;
    }

    void AddUpTo100(std::vector<uint32_t>& shares, std::vector<Tier> const& tiers, Dice const& dice)
    {
        shares.resize(tiers.size(), 0);
        for (size_t i = 0; i < tiers.size(); ++i)
            shares[i] = std::clamp(shares[i], tiers[i].min, tiers[i].max);
        if (!CanAddUp(tiers))
            return;
        uint32_t sum = 0;
        for (uint32_t s : shares)
            sum += s;
        while (sum != 100)
        {
            const bool up = sum < 100;
            std::vector<uint32_t> room;   // how far each can go that way
            uint32_t all = 0;
            for (size_t i = 0; i < tiers.size(); ++i)
            {
                room.push_back(up ? tiers[i].max - shares[i] : shares[i] - tiers[i].min);
                all += room.back();
            }
            if (!all)
                return;   // (cannot be, when they can add up)
            uint32_t roll = dice(all) % all;
            size_t pick = 0;
            while (roll >= room[pick])
                roll -= room[pick++];
            if (up)
            {
                ++shares[pick];
                ++sum;
            }
            else
            {
                --shares[pick];
                --sum;
            }
        }
    }

    std::vector<uint32_t> StartShares(std::vector<Tier> const& tiers, Dice const& dice)
    {
        std::vector<uint32_t> shares;
        for (Tier const& t : tiers)
            shares.push_back(t.start);
        AddUpTo100(shares, tiers, dice);
        return shares;
    }

    std::vector<uint32_t> Drift(std::vector<uint32_t> const& shares, std::vector<Tier> const& tiers, uint32_t step,
                                uint32_t pull, Dice const& dice)
    {
        std::vector<uint32_t> out;
        const int64_t towards = std::min<uint32_t>(pull, 100);
        for (size_t i = 0; i < tiers.size(); ++i)
        {
            const int64_t now  = i < shares.size() ? shares[i] : tiers[i].start;
            const int64_t gap  = int64_t(tiers[i].start) - now;
            const int64_t back = (gap * towards + (gap < 0 ? -50 : 50)) / 100;
            const int64_t move = step ? int64_t(dice(2 * step + 1) % (2 * step + 1)) - int64_t(step) : 0;
            out.push_back(uint32_t(std::clamp<int64_t>(now + back + move, tiers[i].min, tiers[i].max)));
        }
        AddUpTo100(out, tiers, dice);
        return out;
    }

    std::vector<std::vector<uint32_t>> SpotPlaces(uint32_t places, std::vector<uint32_t> const& shares,
                                                  std::vector<std::vector<uint32_t>> const& written)
    {
        std::vector<uint64_t> byShare(shares.begin(), shares.end());
        byShare.resize(written.size(), 0);
        const std::vector<uint32_t> zones = PbpApportion(places, byShare);
        std::vector<std::vector<uint32_t>> out;
        for (size_t z = 0; z < written.size(); ++z)
        {
            std::vector<uint64_t> parts(written[z].begin(), written[z].end());
            if (std::all_of(parts.begin(), parts.end(), [](uint64_t n) { return n == 0; }))
                std::fill(parts.begin(), parts.end(), 1);
            out.push_back(PbpApportion(zones[z], parts));
        }
        return out;
    }

    uint32_t AtMost(uint32_t places, uint32_t percent)
    {
        return uint32_t((uint64_t(places) * std::min<uint32_t>(percent, 100) + 99) / 100);
    }

    std::vector<std::pair<size_t, size_t>> PlanMoves(std::vector<Count> zones, uint32_t cap)
    {
        std::vector<std::pair<size_t, size_t>> moves;
        while (moves.size() < cap)
        {
            size_t from = zones.size(), to = zones.size();
            uint32_t over = 0, under = 0;
            for (size_t i = 0; i < zones.size(); ++i)
            {
                Count const& z = zones[i];
                if (z.there > z.room && z.movable && z.there - z.room > over)
                {
                    over = z.there - z.room;
                    from = i;
                }
                if (z.room > z.there && z.room - z.there > under)
                {
                    under = z.room - z.there;
                    to = i;
                }
            }
            if (from == zones.size() || to == zones.size())
                break;
            moves.emplace_back(from, to);
            --zones[from].there;
            --zones[from].movable;
            ++zones[to].there;
        }
        return moves;
    }
}
