#include "PlayerbotsPlusCraftText.h"

#include <algorithm>
#include <array>
#include <map>

namespace PbpCraft
{
    namespace
    {
        struct ProfessionInfo
        {
            uint32_t    skill;
            char const* name;
            uint32_t    apprentice;
            bool        primary;
        };

        // The Apprentice spells are playerbots' own (GetProfessionStarterSpell).
        constexpr std::array<ProfessionInfo, 14> kProfessions = { {
            { Skill::Alchemy,        "Alchemy",        2259,  true },
            { Skill::Blacksmithing,  "Blacksmithing",  2018,  true },
            { Skill::Cooking,        "Cooking",        2550,  false },
            { Skill::Enchanting,     "Enchanting",     7411,  true },
            { Skill::Engineering,    "Engineering",    4036,  true },
            { Skill::FirstAid,       "First Aid",      3273,  false },
            { Skill::Fishing,        "Fishing",        7620,  false },
            { Skill::Herbalism,      "Herbalism",      2366,  true },
            { Skill::Inscription,    "Inscription",    45357, true },
            { Skill::Jewelcrafting,  "Jewelcrafting",  25229, true },
            { Skill::Leatherworking, "Leatherworking", 2108,  true },
            { Skill::Mining,         "Mining",         2575,  true },
            { Skill::Skinning,       "Skinning",       8613,  true },
            { Skill::Tailoring,      "Tailoring",      3908,  true },
        } };

        ProfessionInfo const* Find(uint32_t skill)
        {
            for (ProfessionInfo const& p : kProfessions)
                if (p.skill == skill)
                    return &p;
            return nullptr;
        }

        // Apprentice to Grand Master, and what it takes to train each (WotLK).
        constexpr std::array<Rank, 6> kRanks = { {
            { 1, 75,  1,  0 },
            { 2, 150, 10, 50 },
            { 3, 225, 20, 125 },
            { 4, 300, 35, 200 },
            { 5, 375, 50, 275 },
            { 6, 450, 65, 350 },
        } };
    }

    bool IsPrimary(uint32_t skill)
    {
        ProfessionInfo const* p = Find(skill);
        return p && p->primary;
    }

    std::string Name(uint32_t skill)
    {
        ProfessionInfo const* p = Find(skill);
        return p ? p->name : "";
    }

    uint32_t ApprenticeSpell(uint32_t skill)
    {
        ProfessionInfo const* p = Find(skill);
        return p ? p->apprentice : 0;
    }

    uint32_t PointChance(uint32_t skill, uint32_t yellow, uint32_t grey)
    {
        if (grey <= yellow)   // as the core does with thresholds it cannot spread
            return skill < grey ? 100 : 0;
        if (skill <= yellow)
            return 100;
        if (skill >= grey)
            return 0;
        return static_cast<uint32_t>(uint64_t(grey - skill) * 100 / (grey - yellow));
    }

    Rank RankFor(uint32_t level, uint32_t skill)
    {
        Rank rank = kRanks.front();
        for (Rank const& r : kRanks)
            if (level >= r.minLevel && skill >= r.minSkill)
                rank = r;
            else
                break;
        return rank;
    }

    std::vector<Pair> ClassPairs(uint8_t cls)
    {
        using namespace Skill;
        switch (cls)
        {
            case Class::Warrior:
                return { { Mining, Blacksmithing, 45 }, { Mining, Engineering, 30 }, { Mining, Jewelcrafting, 15 },
                         { Herbalism, Alchemy, 10 } };
            case Class::Paladin:
                return { { Mining, Blacksmithing, 45 }, { Mining, Jewelcrafting, 30 }, { Mining, Engineering, 15 },
                         { Herbalism, Alchemy, 10 } };
            case Class::DeathKnight:
                return { { Mining, Blacksmithing, 45 }, { Mining, Engineering, 35 }, { Mining, Jewelcrafting, 20 } };
            case Class::Hunter:
                return { { Skinning, Leatherworking, 45 }, { Mining, Engineering, 35 }, { Herbalism, Alchemy, 10 },
                         { Mining, Jewelcrafting, 10 } };
            case Class::Rogue:
                return { { Skinning, Leatherworking, 35 }, { Herbalism, Alchemy, 25 }, { Mining, Engineering, 25 },
                         { Mining, Jewelcrafting, 10 }, { Herbalism, Inscription, 5 } };
            case Class::Druid:
                return { { Skinning, Leatherworking, 35 }, { Herbalism, Alchemy, 35 }, { Herbalism, Inscription, 20 },
                         { Mining, Jewelcrafting, 10 } };
            case Class::Shaman:
                return { { Herbalism, Alchemy, 35 }, { Skinning, Leatherworking, 25 }, { Herbalism, Inscription, 25 },
                         { Mining, Jewelcrafting, 15 } };
            case Class::Priest:
                return { { Tailoring, Enchanting, 45 }, { Herbalism, Inscription, 30 }, { Herbalism, Alchemy, 25 } };
            case Class::Mage:
                return { { Tailoring, Enchanting, 50 }, { Herbalism, Alchemy, 25 }, { Herbalism, Inscription, 25 } };
            case Class::Warlock:
            default:
                return { { Tailoring, Enchanting, 50 }, { Herbalism, Alchemy, 25 }, { Herbalism, Inscription, 25 } };
        }
    }

    bool PairFits(uint8_t cls, uint32_t a, uint32_t b)
    {
        for (Pair const& p : ClassPairs(cls))
            if ((p.first == a && p.second == b) || (p.first == b && p.second == a))
                return true;
        return false;
    }

    namespace
    {
        // The pairs PickPair chooses from: those with `keep` in them, else all.
        std::vector<Pair> Candidates(uint8_t cls, uint32_t keep)
        {
            std::vector<Pair> all = ClassPairs(cls);
            if (!keep)
                return all;
            std::vector<Pair> with;
            for (Pair const& p : all)
                if (p.first == keep || p.second == keep)
                    with.push_back(p);
            return with.empty() ? all : with;
        }
    }

    uint32_t WeightOf(uint8_t cls, uint32_t keep)
    {
        uint32_t total = 0;
        for (Pair const& p : Candidates(cls, keep))
            total += p.weight;
        return total;
    }

    std::pair<uint32_t, uint32_t> PickPair(uint8_t cls, uint32_t keep, uint32_t roll)
    {
        const std::vector<Pair> pairs = Candidates(cls, keep);
        if (pairs.empty())
            return { 0, 0 };
        for (Pair const& p : pairs)
        {
            if (roll <= p.weight)
                return { p.first, p.second };
            roll -= p.weight;
        }
        return { pairs.back().first, pairs.back().second };
    }

    Choice Choose(std::vector<Option> const& options, ChoiceSettings const& s)
    {
        Choice c;
        const uint32_t maxCasts = std::max<uint32_t>(1, s.maxCasts);

        // An upgrade for itself: the best it can make, once.
        for (size_t i = 0; i < options.size(); ++i)
        {
            Option const& o = options[i];
            if (!o.upgrade || !o.canMake)
                continue;
            if (c.why != Why::Use || o.worthEach > options[c.index].worthEach)
            {
                c.index = i;
                c.why   = Why::Use;
                c.casts = 1;
            }
        }
        if (c.why == Why::Use)
            return c;

        // A chance at a point: the least copper of materials for each point it
        // can expect, a surer one first when that is the same.
        auto costPerPoint = [](Option const& o)
        { return (o.costEach * 100 + o.pointChance - 1) / std::max<uint32_t>(o.pointChance, 1); };
        for (size_t i = 0; i < options.size(); ++i)
        {
            Option const& o = options[i];
            if (!o.canMake || !o.pointChance || o.pointChance < s.minChance)
                continue;
            if (c.why == Why::Skill)
            {
                Option const& best = options[c.index];
                const uint64_t mine = costPerPoint(o), theirs = costPerPoint(best);
                if (mine > theirs || (mine == theirs && o.pointChance <= best.pointChance))
                    continue;
            }
            c.index = i;
            c.why   = Why::Skill;
        }
        if (c.why == Why::Skill)
        {
            c.casts = std::min(options[c.index].canMake, maxCasts);
            return c;
        }

        // What a craft that would give a point is short of: the nearest to
        // that point first, the cheaper then.
        for (size_t i = 0; i < options.size(); ++i)
        {
            Option const& o = options[i];
            if (!o.canMake || !o.steps)
                continue;
            if (c.why == Why::Materials)
            {
                Option const& best = options[c.index];
                if (o.steps > best.steps || (o.steps == best.steps && o.costEach >= best.costEach))
                    continue;
            }
            c.index = i;
            c.why   = Why::Materials;
        }
        if (c.why == Why::Materials)
        {
            c.casts = std::min(options[c.index].canMake, maxCasts);
            return c;
        }

        // Worth more than what goes into it: the most over its materials.
        uint64_t bestGain = 0;
        for (size_t i = 0; i < options.size(); ++i)
        {
            Option const& o = options[i];
            if (!o.canMake || !o.sellable || !o.worthEach)
                continue;
            if (o.worthEach * 100 < o.costEach * std::max<uint32_t>(s.sellMarginPct, 100))
                continue;
            const uint64_t gain = o.worthEach - std::min(o.worthEach, o.costEach);
            if (c.why == Why::Sell && gain <= bestGain)
                continue;
            c.index  = i;
            c.why    = Why::Sell;
            bestGain = gain;
        }
        if (c.why == Why::Sell)
            c.casts = std::min(options[c.index].canMake, maxCasts);
        return c;
    }

    char const* WhyName(Why why)
    {
        switch (why)
        {
            case Why::Use:       return "use";
            case Why::Skill:     return "skill";
            case Why::Materials: return "materials";
            case Why::Sell:      return "sell";
            default:             return "none";
        }
    }

    std::unordered_map<uint32_t, uint32_t> Wanted(std::vector<Known> const& known,
                                                  std::unordered_map<uint32_t, uint32_t> const& have,
                                                  uint32_t minChance, uint32_t maxSteps)
    {
        std::unordered_map<uint32_t, uint32_t> wanted;
        auto shortOf = [&](Known const& k, uint32_t step)
        {
            bool added = false;
            for (auto const& [item, need] : k.needs)
            {
                auto h = have.find(item);
                if (h != have.end() && h->second >= need)
                    continue;
                if (wanted.emplace(item, step).second)
                    added = true;
            }
            return added;
        };
        for (Known const& k : known)
            if (k.pointChance && k.pointChance >= minChance)
                shortOf(k, 1);
        for (uint32_t step = 2; step <= maxSteps; ++step)
        {
            bool any = false;
            for (Known const& k : known)
            {
                auto w = wanted.find(k.product);
                if (!k.product || w == wanted.end() || w->second != step - 1)
                    continue;
                any = shortOf(k, step) || any;
            }
            if (!any)
                break;
        }
        return wanted;
    }

    Yields Expected(LootTable const& table, LootTables const& references, uint32_t depth)
    {
        Yields out;
        if (depth > 4)
            return out;   // a table that refers back to itself gives nothing more
        auto give = [&](LootRow const& r, double chance)
        {
            if (chance <= 0.0)
                return;
            if (r.reference)
            {
                auto ref = references.find(r.reference);
                if (ref == references.end())
                    return;
                const double times = std::max<uint32_t>(1, r.maxCount);
                for (auto const& [item, many] : Expected(ref->second, references, depth + 1))
                    out[item] += chance * times * many;
                return;
            }
            const double low = r.minCount, high = std::max(r.minCount, r.maxCount);
            out[r.item] += chance * (low + high) / 2.0;
        };

        std::map<uint8_t, std::vector<LootRow const*>> groups;
        for (LootRow const& r : table)
            if (r.group)
                groups[r.group].push_back(&r);
            else
                give(r, std::clamp(double(r.chance) / 100.0, 0.0, 1.0));
        for (auto const& [id, rows] : groups)
        {
            double left = 1.0;
            std::vector<LootRow const*> alike;
            for (LootRow const* r : rows)
            {
                if (r->chance <= 0.0f)
                {
                    alike.push_back(r);
                    continue;
                }
                const double chance = std::min(left, double(r->chance) / 100.0);
                give(*r, chance);
                left -= chance;
            }
            if (!alike.empty() && left > 0.0)
                for (LootRow const* r : alike)
                    give(*r, left / double(alike.size()));
        }
        return out;
    }

    uint32_t GatherChance(uint32_t skill, uint32_t required)
    {
        if (skill >= required + 100)
            return 0;
        if (skill >= required + 50)
            return 25;
        if (skill >= required + 25)
            return 75;
        return 100;
    }
}
