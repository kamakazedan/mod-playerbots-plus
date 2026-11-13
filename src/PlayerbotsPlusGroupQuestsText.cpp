#include "PlayerbotsPlusGroupQuestsText.h"

#include <algorithm>
#include <unordered_map>

namespace PbpGroupQuests
{
    uint32_t PartySize(uint32_t suggested, uint32_t fallback)
    {
        return std::clamp<uint32_t>(suggested >= 2 ? suggested : fallback, 2, 5);
    }

    uint32_t QuestToAsk(std::vector<Held> const& held)
    {
        Held const* best = nullptr;
        for (Held const& h : held)
        {
            if (!h.quest)
                continue;
            if (!best || h.others > best->others || (h.others == best->others && h.level < best->level))
                best = &h;
        }
        return best ? best->quest : 0;
    }

    Next AfterAsk(uint32_t members, uint32_t target, uint32_t minutesAsking, uint32_t askForMinutes)
    {
        if (members >= target)
            return Next::Meet;
        if (minutesAsking < askForMinutes)
            return Next::Wait;
        return members >= 2 ? Next::Meet : Next::Solo;
    }

    uint32_t NextTogether(std::vector<Holding> const& members, std::vector<uint32_t> const& order)
    {
        std::unordered_map<uint32_t, uint32_t> count;
        for (Holding const& m : members)
        {
            std::vector<uint32_t> quests = m.quests;
            std::sort(quests.begin(), quests.end());
            quests.erase(std::unique(quests.begin(), quests.end()), quests.end());
            for (uint32_t q : quests)
                ++count[q];
        }
        uint32_t best = 0, bestCount = 1;
        for (uint32_t q : order)
            if (auto it = count.find(q); it != count.end() && it->second > bestCount)
            {
                best = q;
                bestCount = it->second;
            }
        return best;
    }
}
