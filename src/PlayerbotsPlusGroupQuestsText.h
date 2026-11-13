#ifndef PLAYERBOTS_PLUS_GROUP_QUESTS_TEXT_H
#define PLAYERBOTS_PLUS_GROUP_QUESTS_TEXT_H

// --------------------------------------------------------------------------
// Group quests' choices, with no game types in them so they can be tested
// without a server (tests/groupquests_test.cpp): how big a party a quest
// wants, which of its quests a bot asks for help with, what a party looking
// for people does once it has asked, and which quest a party does next.
//
// The game side is PlayerbotsPlusGroupQuests.
// --------------------------------------------------------------------------

#include <cstdint>
#include <vector>

namespace PbpGroupQuests
{
    // The party a quest wants: the players it suggests (2 to 5), or
    // `fallback` for one that says none (an elite quest of old).
    uint32_t PartySize(uint32_t suggested, uint32_t fallback);

    // One of a bot's group quests in the zone it is in, as the asker sees it.
    struct Held
    {
        uint32_t quest   = 0;
        int32_t  level   = 0;   // the quest's level for the bot
        uint32_t others  = 0;   // other bots in the zone who hold it and could come
    };

    // The one quest a bot asks for help with: the one most others in the zone
    // hold (the party fills soonest), then the lowest level (it goes grey
    // first), then the first. 0 for none.
    uint32_t QuestToAsk(std::vector<Held> const& held);

    // A party looking for people, after an ask.
    enum class Next : uint8_t
    {
        Wait,    // ask again later
        Meet,    // enough of them: they meet up and go
        Solo,    // nobody came in the time it asks for: it tries alone
    };

    // `members`: the asker and those who said they would come; `target`: the
    // party it wants (PartySize); `minutesAsking`: since its first ask;
    // `askForMinutes`: how long it asks for. Full, it meets; out of time, it
    // goes with whoever it has, alone if nobody came.
    Next AfterAsk(uint32_t members, uint32_t target, uint32_t minutesAsking, uint32_t askForMinutes);

    // A party's members' quests, for the next one it does together.
    struct Holding
    {
        uint64_t member = 0;
        std::vector<uint32_t> quests;   // group quests of the zone it holds, not done, not given up on
    };

    // The quest the most of them hold, at least two (between equals, the one
    // that comes first in `order`, the zone's quests by level), 0 for none.
    uint32_t NextTogether(std::vector<Holding> const& members, std::vector<uint32_t> const& order);
}

#endif // PLAYERBOTS_PLUS_GROUP_QUESTS_TEXT_H
