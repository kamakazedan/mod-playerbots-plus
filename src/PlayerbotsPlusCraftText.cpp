#include "PlayerbotsPlusCraftText.h"

#include <algorithm>
#include <array>
#include <cctype>
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

    // ---- leanings --------------------------------------------------------------

    namespace
    {
        std::string Lower(std::string text)
        {
            for (char& c : text)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }

        // The whole of `text` a number up to `most`.
        bool Number(std::string const& text, uint32_t most, uint32_t& out)
        {
            if (text.empty() || text.size() > 5 ||
                !std::all_of(text.begin(), text.end(), [](char c) { return c >= '0' && c <= '9'; }))
                return false;
            out = uint32_t(std::stoul(text));
            return out <= most;
        }

        std::vector<std::string> Split(std::string const& text, char by)
        {
            std::vector<std::string> out;
            std::string part;
            for (char c : text + by)
            {
                if (c != by)
                {
                    part += c;
                    continue;
                }
                if (!part.empty())
                    out.push_back(part);
                part.clear();
            }
            return out;
        }

        bool Gathers(uint32_t skill)
        {
            return skill == Skill::Herbalism || skill == Skill::Mining || skill == Skill::Skinning;
        }

        // What a crafting profession works with that only gathering gets: 0
        // for one that wants nothing gathered, and for gathering.
        uint32_t SupplierOf(uint32_t skill)
        {
            switch (skill)
            {
                case Skill::Blacksmithing:
                case Skill::Engineering:
                case Skill::Jewelcrafting:  return Skill::Mining;
                case Skill::Alchemy:
                case Skill::Inscription:    return Skill::Herbalism;
                case Skill::Leatherworking: return Skill::Skinning;
                default:                    return 0;
            }
        }

        bool Same(uint32_t a1, uint32_t b1, uint32_t a2, uint32_t b2)
        {
            return (a1 == a2 && b1 == b2) || (a1 == b2 && b1 == a2);
        }

        bool Holds(std::vector<uint32_t> const& never, uint32_t a, uint32_t b)
        {
            return std::find(never.begin(), never.end(), a) != never.end() ||
                   std::find(never.begin(), never.end(), b) != never.end();
        }

        void Note(std::string& bad, std::string const& part)
        {
            bad += (bad.empty() ? "" : ", ") + part;
        }
    }

    uint32_t PrimaryByName(std::string const& name)
    {
        const std::string want = Lower(name);
        for (ProfessionInfo const& p : kProfessions)
            if (p.primary && Lower(p.name) == want)
                return p.skill;
        return 0;
    }

    bool FeedsItself(uint32_t a, uint32_t b)
    {
        if (a == b || !IsPrimary(a) || !IsPrimary(b))
            return false;
        const uint32_t needA = SupplierOf(a), needB = SupplierOf(b);
        return (!needA || needA == b) && (!needB || needB == a);
    }

    Pair ArmourPair(uint8_t cls)
    {
        switch (cls)
        {
            case Class::Warrior:
            case Class::Paladin:
            case Class::DeathKnight: return { Skill::Mining, Skill::Blacksmithing, 0 };
            case Class::Hunter:
            case Class::Rogue:
            case Class::Shaman:
            case Class::Druid:       return { Skill::Skinning, Skill::Leatherworking, 0 };
            case Class::Priest:
            case Class::Mage:
            case Class::Warlock:     return { Skill::Tailoring, Skill::Enchanting, 0 };
            default:                 return {};
        }
    }

    uint32_t RaceKnack(uint8_t race)
    {
        switch (race)
        {
            case Race::Gnome:    return Skill::Engineering;     // Engineering Specialization
            case Race::Tauren:   return Skill::Herbalism;       // Cultivation
            case Race::BloodElf: return Skill::Enchanting;      // Arcane Affinity
            case Race::Draenei:  return Skill::Jewelcrafting;   // Gemcutting
            default:             return 0;
        }
    }

    Pair KnackPair(uint8_t race)
    {
        switch (race)
        {
            case Race::Gnome:    return { Skill::Mining, Skill::Engineering, 0 };
            case Race::Tauren:   return { Skill::Herbalism, Skill::Alchemy, 0 };
            case Race::BloodElf: return { Skill::Tailoring, Skill::Enchanting, 0 };
            case Race::Draenei:  return { Skill::Mining, Skill::Jewelcrafting, 0 };
            default:             return {};
        }
    }

    Leaning ParseLeaning(std::string const& text, std::string& bad)
    {
        Leaning l;
        for (std::string const& raw : Split(text, ' '))
        {
            const std::string word = Lower(raw);
            const size_t colon = word.find(':');
            const std::string head = word.substr(0, colon);
            const std::string tail = colon == std::string::npos ? std::string() : word.substr(colon + 1);
            if (word == "sell")
                l.sellFirst = true;
            else if (head == "never" && colon != std::string::npos)
            {
                const std::vector<std::string> names = Split(tail, ',');
                bool all = !names.empty();
                for (std::string const& name : names)
                    if (const uint32_t skill = PrimaryByName(name))
                        l.never.push_back(skill);
                    else
                        all = false;
                if (!all)
                    Note(bad, raw);
            }
            else if (head == "list" && colon != std::string::npos)
            {
                if (!Number(tail, 1000, l.listPct))
                {
                    l.listPct = 100;
                    Note(bad, raw);
                }
            }
            else if ((head == "class" || head == "armour" || head == "armor" || head == "race" || head == "rare") &&
                     colon != std::string::npos)
            {
                uint32_t share = 0;
                if (!Number(tail, 10000, share))
                    Note(bad, raw);
                else if (head == "class")
                    l.classShare = share;
                else if (head == "race")
                    l.raceShare = share;
                else if (head == "rare")
                    l.rareShare = share;
                else
                    l.armourShare = share;
            }
            else if (head.find('+') != std::string::npos && colon != std::string::npos)
            {
                const std::vector<std::string> names = Split(head, '+');
                uint32_t weight = 0;
                const uint32_t a = names.size() == 2 ? PrimaryByName(names[0]) : 0;
                const uint32_t b = names.size() == 2 ? PrimaryByName(names[1]) : 0;
                if (!a || !b || !Number(tail, 10000, weight) || !FeedsItself(a, b))
                {
                    Note(bad, raw);
                    continue;
                }
                if (weight)   // the gathering one first, as the class pairs are
                    l.pairs.push_back(Gathers(b) && !Gathers(a) ? Pair{ b, a, weight } : Pair{ a, b, weight });
            }
            else
                Note(bad, raw);
        }
        // A pair with what it will never have is not one it takes.
        std::vector<Pair> kept;
        for (Pair const& p : l.pairs)
            if (Holds(l.never, p.first, p.second))
                Note(bad, Name(p.first) + "+" + Name(p.second) + " (never)");
            else
                kept.push_back(p);
        l.pairs = std::move(kept);
        return l;
    }

    std::vector<std::pair<std::string, std::string>> const& DefaultLeanings()
    {
        static const std::vector<std::pair<std::string, std::string>> leanings = {
            { "Profit",   "Herbalism+Mining:70 class:30 sell list:200" },
            { "Power",    "armour:70 Mining+Jewelcrafting:30" },
            { "PvP",      "Mining+Engineering:70 armour:30" },
            { "Maker",    "Tailoring+Enchanting:50 Herbalism+Alchemy:50" },
            { "Style",    "Tailoring+Enchanting:50 Mining+Jewelcrafting:50" },
            { "Tinker",   "Mining+Engineering:80 class:20" },
            { "Pets",     "Mining+Engineering:70 class:30 never:Skinning" },
            { "Outdoors", "Herbalism+Mining:40 Herbalism+Skinning:30 Mining+Skinning:30" },
            { "Scholar",  "Herbalism+Inscription:50 Tailoring+Enchanting:50" },
            { "OldWays",  "class:100 never:Inscription,Jewelcrafting" },
            { "Heritage", "race:100" },
            { "Contrary", "rare:100" },
            { "Newbie",   "class:100" },
        };
        return leanings;
    }

    std::string LeaningKey(std::string const& name)
    {
        const size_t from = name.find_first_not_of(" \t");
        if (from == std::string::npos)
            return {};
        return Lower(name.substr(from, name.find_last_not_of(" \t") - from + 1));
    }

    char const* MatchName(Match m)
    {
        switch (m)
        {
            case Match::Own:    return "own";
            case Match::Armour: return "armour";
            case Match::Race:   return "race";
            case Match::Rare:   return "rare";
            case Match::Class:  return "class";
            case Match::Never:  return "never";
            default:            return "none";
        }
    }

    std::vector<Pick> Picks(Leaning const& l, uint8_t cls, uint8_t race)
    {
        const std::vector<Pair> classPairs = ClassPairs(cls);
        uint32_t classTotal = 0;
        for (Pair const& p : classPairs)
            classTotal += p.weight;
        classTotal = std::max<uint32_t>(classTotal, 1);

        std::vector<Pick> picks;
        auto add = [&](uint32_t a, uint32_t b, uint32_t weight, Match from)
        {
            if (!weight || !a || !b || Holds(l.never, a, b))
                return;
            for (Pick& p : picks)
                if (Same(p.first, p.second, a, b))
                {
                    p.weight += weight;
                    return;
                }
            picks.push_back({ a, b, weight, from });
        };
        auto addClass = [&](uint32_t share, Match from)
        {
            for (Pair const& p : classPairs)
                add(p.first, p.second, p.weight * share, from);
        };

        // Shares against each other: one written as 70 is 70 of the class's
        // pairs' whole weight, so "class:30" beside it is the other 30.
        for (Pair const& p : l.pairs)
            add(p.first, p.second, p.weight * classTotal, Match::Own);
        if (l.armourShare)
        {
            const Pair a = ArmourPair(cls);
            add(a.first, a.second, l.armourShare * classTotal, Match::Armour);
        }
        if (l.raceShare)
        {
            const Pair k = KnackPair(race);
            if (k.first)
                add(k.first, k.second, l.raceShare * classTotal, Match::Race);
            else
                addClass(l.raceShare, Match::Class);
        }
        if (l.rareShare && !classPairs.empty())
        {
            uint32_t least = UINT32_MAX;
            for (Pair const& p : classPairs)
                if (!Holds(l.never, p.first, p.second))
                    least = std::min(least, p.weight);
            std::vector<Pair> rare;
            for (Pair const& p : classPairs)
                if (p.weight == least && !Holds(l.never, p.first, p.second))
                    rare.push_back(p);
            if (!rare.empty())
            {
                const uint32_t whole = l.rareShare * classTotal, each = whole / uint32_t(rare.size());
                for (size_t i = 0; i < rare.size(); ++i)
                    add(rare[i].first, rare[i].second, i ? each : whole - each * uint32_t(rare.size() - 1),
                        Match::Rare);
            }
        }
        if (l.classShare)
            addClass(l.classShare, Match::Class);
        if (picks.empty())
            addClass(1, Match::Class);
        return picks;
    }

    Match MatchOf(Leaning const& l, uint8_t cls, uint8_t race, uint32_t a, uint32_t b)
    {
        if (a == b || !IsPrimary(a) || !IsPrimary(b))
            return Match::None;
        if (Holds(l.never, a, b))
            return Match::Never;
        for (Pick const& p : Picks(l, cls, race))
            if (Same(p.first, p.second, a, b))
                return p.from;
        return PairFits(cls, a, b) ? Match::Class : Match::None;
    }

    uint32_t PicksWeight(std::vector<Pick> const& picks, uint32_t keep)
    {
        uint32_t with = 0, all = 0;
        for (Pick const& p : picks)
        {
            all += p.weight;
            if (keep && (p.first == keep || p.second == keep))
                with += p.weight;
        }
        return with ? with : all;
    }

    Pick PickOne(std::vector<Pick> const& picks, uint32_t keep, uint32_t roll)
    {
        bool any = false;
        for (Pick const& p : picks)
            if (keep && (p.first == keep || p.second == keep))
                any = true;
        Pick last;
        for (Pick const& p : picks)
        {
            if (any && p.first != keep && p.second != keep)
                continue;
            if (roll <= p.weight)
                return p;
            roll -= p.weight;
            last = p;
        }
        return last;
    }

    uint32_t StableRoll(uint64_t seed, uint32_t total)
    {
        if (!total)
            return 0;
        // splitmix64's finish: seeds next to each other land far apart.
        uint64_t z = seed + 0x9E3779B97F4A7C15ull;
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
        z ^= z >> 31;
        return uint32_t(z % total) + 1;
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

        // Worth more than what goes into it: the most over its materials.
        auto forSale = [&]()
        {
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
            return c.why == Why::Sell;
        };
        if (s.sellFirst && forSale())
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

        if (!s.sellFirst)
            forSale();
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
