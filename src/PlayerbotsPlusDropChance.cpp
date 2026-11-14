#include "PlayerbotsPlusDropChance.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace PbpDrops
{
    namespace
    {
        constexpr uint32_t kMaxDepth = 8;   // references inside references; the world's go three or four deep

        // The chance of at least one in `times` rolls at `chance` each.
        double AtLeastOnce(double chance, uint32_t times)
        {
            return 1.0 - std::pow(1.0 - std::clamp(chance, 0.0, 1.0), double(std::max<uint32_t>(times, 1)));
        }
    }

    Chances Roller::Roll(Table const& table)
    {
        return Roll(table, 0);
    }

    Chances const& Roller::Reference(uint32_t entry, uint32_t depth)
    {
        static const Chances nothing;
        if (auto known = _known.find(entry); known != _known.end())
            return known->second;
        auto table = _references.find(entry);
        if (table == _references.end() || depth > kMaxDepth || _working.contains(entry))
            return nothing;
        _working.insert(entry);
        Chances got = Roll(table->second, depth);
        _working.erase(entry);
        return _known.emplace(entry, std::move(got)).first->second;
    }

    Chances Roller::Roll(Table const& table, uint32_t depth)
    {
        // Worked out as the chance each item does NOT come out: every roll
        // that could give it is another chance it does.
        Chances missed;
        auto mayGive = [&missed](uint32_t item, double chance)
        {
            if (chance <= 0.0)
                return;
            missed.try_emplace(item, 1.0).first->second *= 1.0 - std::min(chance, 1.0);
        };

        std::map<uint8_t, std::vector<Row const*>> groups;
        for (Row const& row : table)
        {
            if (row.group)
            {
                groups[row.group].push_back(&row);
                continue;
            }
            const double chance = std::clamp(double(row.chance), 0.0, 100.0) / 100.0;
            if (chance <= 0.0)
                continue;
            if (row.reference)
            {
                for (auto const& [item, inner] : Reference(row.reference, depth + 1))
                    mayGive(item, chance * AtLeastOnce(inner, row.maxCount));
            }
            else
                mayGive(row.item, chance);
        }

        for (auto const& [group, rows] : groups)
        {
            // Which row a roll of the group picks: the ones with a chance of
            // their own in order (a roll of 0-100 less each in turn), then,
            // with none of those, any one of the rest alike.
            double   chanced = 0.0;
            uint32_t alike   = 0;
            for (Row const* row : rows)
            {
                if (row->chance > 0.0f)
                    chanced += row->chance;
                else
                    ++alike;
            }
            const double left = std::max(0.0, 100.0 - std::min(chanced, 100.0)) / 100.0;

            Chances gives;   // the one roll's: picks are exclusive, so they add up
            double before = 0.0;
            for (Row const* row : rows)
            {
                double pick = 0.0;
                if (row->chance > 0.0f)
                {
                    pick = std::clamp(std::min(double(row->chance), 100.0 - before), 0.0, 100.0) / 100.0;
                    before += row->chance;
                }
                else if (alike)
                    pick = left / alike;
                if (pick <= 0.0)
                    continue;
                if (row->reference)
                {
                    for (auto const& [item, inner] : Reference(row->reference, depth + 1))
                        gives[item] += pick * AtLeastOnce(inner, row->maxCount);
                }
                else
                    gives[row->item] += pick;
            }
            for (auto const& [item, chance] : gives)
                mayGive(item, chance);
        }

        Chances out;
        out.reserve(missed.size());
        for (auto const& [item, miss] : missed)
            if (miss < 1.0)
                out.emplace(item, 1.0 - miss);
        return out;
    }

    std::unordered_map<uint32_t, Drop> Drops(std::vector<Source> const& sources, Tables const& references,
                                             Tables const& containers)
    {
        Roller roller(references);
        std::unordered_map<uint32_t, Chances> opened;   // a container: what one gives
        struct Sum
        {
            double   best  = 0.0;
            double   total = 0.0;   // chance x how many
            double   many  = 0.0;
            From     from;
            uint32_t topMany[3] = {};   // how many of each of from.top: of two as likely, the commoner first
            uint32_t maps[4]    = {};   // the maps its creatures are on...
            uint32_t mapMany[4] = {};   // ...how many on each
            uint8_t  mapCount   = 0;
            bool     spread     = false;   // on more maps than that: no one map
        };
        std::unordered_map<uint32_t, Sum> sums;
        auto note = [&sums](uint32_t item, double chance, Source const& source)
        {
            Sum& s = sums[item];
            s.best = std::max(s.best, chance);
            s.total += chance * source.many;
            s.many += source.many;
            From& f = s.from;
            if (source.chest)
            {
                f.chests = true;
                return;
            }
            ++f.kinds;
            f.many += source.many;
            if (source.map != kNoMap && source.many && !s.spread)
            {
                uint8_t i = 0;
                while (i < s.mapCount && s.maps[i] != source.map)
                    ++i;
                if (i == s.mapCount)
                {
                    if (s.mapCount == 4)
                        s.spread = true;
                    else
                        s.maps[s.mapCount++] = source.map;
                }
                if (!s.spread)
                    s.mapMany[i] += source.many;
            }
            f.levelSum += double(source.level) * source.many;
            f.levelSq  += double(source.level) * source.level * source.many;
            for (size_t i = 0; i < 3; ++i)
            {
                const bool same   = float(chance) == f.topChance[i];
                const bool better = !f.top[i] || float(chance) > f.topChance[i] ||
                                    (same && source.many > s.topMany[i]) ||
                                    (same && source.many == s.topMany[i] && source.id < f.top[i]);
                if (!better)
                    continue;
                for (size_t j = 2; j > i; --j)
                {
                    f.top[j]       = f.top[j - 1];
                    f.topChance[j] = f.topChance[j - 1];
                    s.topMany[j]   = s.topMany[j - 1];
                }
                f.top[i]       = source.id;
                f.topChance[i] = float(chance);
                s.topMany[i]   = source.many;
                break;
            }
        };

        for (Source const& source : sources)
        {
            if (!source.table)
                continue;
            const Chances got = roller.Roll(*source.table);

            // What is in the containers it gives, at the container's chance
            // times the chance inside (as a chance missed, again).
            Chances inside;
            for (auto const& [item, chance] : got)
            {
                auto table = containers.find(item);
                if (table == containers.end())
                    continue;
                auto held = opened.find(item);
                if (held == opened.end())
                    held = opened.emplace(item, roller.Roll(table->second)).first;
                for (auto const& [thing, inner] : held->second)
                    inside.try_emplace(thing, 1.0).first->second *= 1.0 - chance * inner;
            }

            for (auto const& [item, chance] : got)
            {
                double total = chance;
                if (auto more = inside.find(item); more != inside.end())
                {
                    total = 1.0 - (1.0 - chance) * more->second;
                    inside.erase(more);
                }
                note(item, total, source);
            }
            for (auto const& [thing, miss] : inside)
                note(thing, 1.0 - miss, source);
        }

        std::unordered_map<uint32_t, Drop> out;
        out.reserve(sums.size());
        for (auto const& [item, s] : sums)
        {
            Drop d;
            d.best    = float(s.best);
            d.typical = s.many > 0.0 ? float(s.total / s.many) : 0.0f;
            d.from    = s.from;
            if (s.spread)
                d.from.map = kMapsMany;
            else if (s.mapCount)
            {
                uint8_t main = 0;
                uint64_t onMaps = 0;
                for (uint8_t i = 0; i < s.mapCount; ++i)
                {
                    onMaps += s.mapMany[i];
                    if (s.mapMany[i] > s.mapMany[main])
                        main = i;
                }
                d.from.map = uint64_t(s.mapMany[main]) * 10 >= onMaps * 9 ? s.maps[main] : kMapsMany;
            }
            out.emplace(item, d);
        }
        return out;
    }

    bool Levels(From const& from, uint32_t& lo, uint32_t& hi)
    {
        lo = hi = 0;
        if (!from.many)
            return false;
        const double mean   = from.levelSum / from.many;
        const double spread = std::sqrt(std::max(0.0, from.levelSq / from.many - mean * mean));
        lo = uint32_t(std::max(1.0, std::round(mean - spread)));
        hi = uint32_t(std::max(double(lo), std::round(mean + spread)));
        return true;
    }

    uint64_t OneIn(double chance)
    {
        if (chance <= 0.0)
            return 0;
        const double n = 1.0 / std::min(chance, 1.0);
        if (n < 100.0)
            return uint64_t(std::llround(n));
        // Two figures are all anyone says: 1 in 1,700, not 1 in 1,724.
        const double step = std::pow(10.0, std::floor(std::log10(n)) - 1.0);
        return uint64_t(std::llround(n / step) * step);
    }
}
