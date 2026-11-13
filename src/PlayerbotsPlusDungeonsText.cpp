#include "PlayerbotsPlusDungeonsText.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <numeric>
#include <string>

namespace PbpDungeons
{
    char const* RoleName(Role role)
    {
        switch (role)
        {
            case Role::Tank:   return "tank";
            case Role::Healer: return "healer";
            default:           return "damage";
        }
    }

    std::vector<uint64_t> PickParty(std::vector<Candidate> const& candidates, uint32_t target, bool needQuest)
    {
        std::vector<size_t> order(candidates.size());
        std::iota(order.begin(), order.end(), size_t(0));
        auto gap = [target](uint32_t level) { return level > target ? level - target : target - level; };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
        {
            Candidate const& x = candidates[a];
            Candidate const& y = candidates[b];
            if ((x.quests > 0) != (y.quests > 0))
                return x.quests > 0;
            if (x.quests != y.quests)
                return x.quests > y.quests;
            return gap(x.level) < gap(y.level);
        });

        Candidate const* tank = nullptr;
        Candidate const* healer = nullptr;
        std::vector<Candidate const*> damage;
        for (size_t i : order)
        {
            Candidate const& c = candidates[i];
            if (c.role == Role::Tank && !tank)
                tank = &c;
            else if (c.role == Role::Healer && !healer)
                healer = &c;
            else if (c.role == Role::Damage && damage.size() < 3)
                damage.push_back(&c);
        }
        if (!tank || !healer || damage.size() < 3)
            return {};

        bool quest = tank->quests || healer->quests;
        for (Candidate const* c : damage)
            quest = quest || c->quests;
        if (needQuest && !quest)
            return {};

        std::vector<uint64_t> party { tank->id, healer->id };
        for (Candidate const* c : damage)
            party.push_back(c->id);
        return party;
    }

    bool Grey(int32_t questLevel, uint32_t greyLevel)
    {
        return questLevel > 0 && uint32_t(questLevel) <= greyLevel;
    }

    namespace
    {
        std::string Lower(std::string text)
        {
            for (char& c : text)
                c = char(std::tolower(static_cast<unsigned char>(c)));
            return text;
        }
    }

    bool SameWing(std::string const& lfgName, std::string const& wingName)
    {
        const size_t open = wingName.find('(');
        const size_t close = wingName.find(')', open);
        if (open == std::string::npos || close == std::string::npos || close <= open + 1)
            return false;
        const std::string part = Lower(wingName.substr(open + 1, close - open - 1));
        return Lower(lfgName).find(part) != std::string::npos;
    }

    std::vector<uint32_t> ToDrop(std::vector<LogEntry> const& log, uint32_t slots, uint32_t keepFree)
    {
        const uint32_t used = uint32_t(log.size());
        const uint32_t free = used < slots ? slots - used : 0;
        if (free >= keepFree)
            return {};
        uint32_t need = keepFree - free;

        // The order a player clears out a full log in.
        auto rank = [](LogEntry const& e) { return e.failed ? 0 : e.grey ? 1 : e.otherZone ? 2 : 3; };
        std::vector<LogEntry const*> open;
        for (LogEntry const& e : log)
            if (!e.keep)
                open.push_back(&e);
        std::stable_sort(open.begin(), open.end(), [&](LogEntry const* a, LogEntry const* b)
        {
            if (rank(*a) != rank(*b))
                return rank(*a) < rank(*b);
            return a->level < b->level;
        });

        std::vector<uint32_t> out;
        for (LogEntry const* e : open)
        {
            if (!need)
                break;
            out.push_back(e->quest);
            --need;
        }
        return out;
    }

    std::string Duration(uint32_t seconds)
    {
        char buf[32];
        if (seconds < 60)
            std::snprintf(buf, sizeof(buf), "%us", seconds);
        else if (seconds < 3600)
            std::snprintf(buf, sizeof(buf), "%um", seconds / 60);
        else
            std::snprintf(buf, sizeof(buf), "%uh %02um", seconds / 3600, (seconds % 3600) / 60);
        return buf;
    }

    namespace
    {
        // `text` cut at `sep`, each part trimmed; empty parts kept, so the
        // shares stay in step with the bands they are for.
        std::vector<std::string> Parts(std::string const& text, char sep)
        {
            std::vector<std::string> out;
            if (text.find_first_not_of(" \t") == std::string::npos)
                return out;
            size_t from = 0;
            for (;;)
            {
                size_t const at = text.find(sep, from);
                std::string part = text.substr(from, at == std::string::npos ? std::string::npos : at - from);
                size_t const a = part.find_first_not_of(" \t");
                size_t const b = part.find_last_not_of(" \t");
                out.push_back(a == std::string::npos ? std::string() : part.substr(a, b - a + 1));
                if (at == std::string::npos)
                    break;
                from = at + 1;
            }
            return out;
        }

        // "42" -> 42; anything but digits (or a number past a level) is not one.
        bool Number(std::string const& text, uint32_t& out)
        {
            if (text.empty() || text.size() > 6 ||
                !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
                return false;
            out = uint32_t(std::stoul(text));
            return true;
        }

        void Note(std::string& bad, std::string const& what)
        {
            bad += (bad.empty() ? "" : ", ") + what;
        }
    }

    std::vector<Band> ParseBands(std::string const& text, std::string& bad)
    {
        std::vector<Band> out;
        bad.clear();
        for (std::string const& part : Parts(text, ','))
        {
            if (part.empty())
                continue;
            std::vector<std::string> const ends = Parts(part, '-');
            Band band;
            bool ok = false;
            if (ends.size() == 1 && Number(ends[0], band.low))
            {
                band.high = band.low;
                ok = true;
            }
            else if (ends.size() == 2)
                ok = Number(ends[0], band.low) && Number(ends[1], band.high) && band.low <= band.high;
            if (ok)
                out.push_back(band);
            else
                Note(bad, part);
        }
        return out;
    }

    std::vector<uint32_t> ParseShares(std::string const& text, size_t bands, std::string& bad)
    {
        std::vector<uint32_t> out(bands, 1);
        bad.clear();
        std::vector<std::string> const parts = Parts(text, ',');
        for (size_t i = 0; i < parts.size() && i < bands; ++i)
        {
            uint32_t share = 0;
            if (Number(parts[i], share))
                out[i] = share;
            else if (!parts[i].empty())
                Note(bad, parts[i]);
        }
        return out;
    }

    int BandOf(std::vector<Band> const& bands, uint32_t level)
    {
        for (size_t i = 0; i < bands.size(); ++i)
            if (level >= bands[i].low && level <= bands[i].high)
                return int(i);
        return -1;
    }

    std::string BandName(Band const& band)
    {
        if (band.low == band.high)
            return std::to_string(band.low);
        return std::to_string(band.low) + "-" + std::to_string(band.high);
    }

    std::vector<size_t> BandOrder(std::vector<uint32_t> const& runs, std::vector<uint32_t> const& shares)
    {
        size_t const n = std::min(runs.size(), shares.size());
        std::vector<size_t> order(n);
        std::iota(order.begin(), order.end(), size_t(0));
        // 0: under its share, 1: at or over it, 2: no share at all.
        auto group = [&](size_t i) { return !shares[i] ? 2 : runs[i] < shares[i] ? 0 : 1; };
        std::stable_sort(order.begin(), order.end(), [&](size_t a, size_t b)
        {
            if (group(a) != group(b))
                return group(a) < group(b);
            if (group(a) == 2)
                return runs[a] < runs[b];
            // The fewest runs for its share: runs[a] / shares[a] < runs[b] / shares[b].
            uint64_t const x = uint64_t(runs[a]) * shares[b];
            uint64_t const y = uint64_t(runs[b]) * shares[a];
            if (x != y)
                return x < y;
            return shares[a] > shares[b];
        });
        return order;
    }
}
