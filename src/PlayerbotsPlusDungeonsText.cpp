#include "PlayerbotsPlusDungeonsText.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <numeric>

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
}
