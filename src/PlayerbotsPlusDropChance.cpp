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
            double best  = 0.0;
            double total = 0.0;   // chance x how many
            double many  = 0.0;
        };
        std::unordered_map<uint32_t, Sum> sums;
        auto note = [&sums](uint32_t item, double chance, uint32_t many)
        {
            Sum& s = sums[item];
            s.best = std::max(s.best, chance);
            s.total += chance * many;
            s.many += many;
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
                note(item, total, source.many);
            }
            for (auto const& [thing, miss] : inside)
                note(thing, 1.0 - miss, source.many);
        }

        std::unordered_map<uint32_t, Drop> out;
        out.reserve(sums.size());
        for (auto const& [item, s] : sums)
        {
            Drop d;
            d.best    = float(s.best);
            d.typical = s.many > 0.0 ? float(s.total / s.many) : 0.0f;
            out.emplace(item, d);
        }
        return out;
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
