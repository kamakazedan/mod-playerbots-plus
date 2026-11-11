#ifndef PLAYERBOTS_PLUS_DUNGEONS_TEXT_H
#define PLAYERBOTS_PLUS_DUNGEONS_TEXT_H

// --------------------------------------------------------------------------
// Bot dungeons' choices and words, with no game types in them so they can be
// tested without a server (tests/dungeons_test.cpp): who goes in a party,
// when a quest has gone grey, which wing of a split dungeon a dungeon finder
// entry is, what a full quest log lets go of first, and how long things took.
//
// The game side is PlayerbotsPlusDungeons (the runs) and
// PlayerbotsPlusDungeonQuests (the quests).
// --------------------------------------------------------------------------

#include <cstdint>
#include <string>
#include <vector>

namespace PbpDungeons
{
    enum class Role : uint8_t
    {
        Tank,
        Healer,
        Damage
    };

    char const* RoleName(Role role);   // "tank", "healer", "damage"

    // A bot the planner could send.
    struct Candidate
    {
        uint64_t id     = 0;      // its guid
        Role     role   = Role::Damage;
        uint32_t level  = 0;
        uint32_t quests = 0;      // unfinished quests it holds for this dungeon
    };

    // A party for one dungeon: a tank, a healer and three to do the damage.
    // Those holding quests for it go first (the more the better), then
    // whoever's level is closest to `target`, then whoever came first. Empty
    // when a role cannot be filled, or, with `needQuest`, when nobody in it
    // holds a quest for the dungeon. The tank comes first in what is returned,
    // then the healer.
    std::vector<uint64_t> PickParty(std::vector<Candidate> const& candidates, uint32_t target, bool needQuest);

    // A quest of `questLevel` (-1 for one that is always the player's own
    // level) has gone grey for a character whose grey level is `greyLevel`
    // (the core's Acore::XP::GetGrayLevel of its level: a quest or mob at or
    // below it is grey).
    bool Grey(int32_t questLevel, uint32_t greyLevel);

    // Whether the dungeon finder's entry `lfgName` ("Scarlet Monastery -
    // Library") is the wing `wingName` ("Scarlet Monastery (Library)"):
    // what the wing's name has in brackets is in the entry's name, ignoring
    // case. A wing name with nothing in brackets matches nothing.
    bool SameWing(std::string const& lfgName, std::string const& wingName);

    // One quest in a full quest log, as the log keeper sees it.
    struct LogEntry
    {
        uint32_t quest     = 0;
        bool     keep      = false;   // never let go: a dungeon quest it is doing, one done and
                                      // waiting to be handed in, the quest it is out doing now
        bool     failed    = false;
        bool     grey      = false;
        bool     otherZone = false;   // for a zone it is not in
        int32_t  level     = 0;
    };

    // What to let go of so that at least `keepFree` of the log's `slots` are
    // free: failed quests first, then grey ones, then ones for other zones,
    // then the lowest level; never one to keep. As few as it takes, and
    // nothing when there is room already (or nothing that may go).
    std::vector<uint32_t> ToDrop(std::vector<LogEntry> const& log, uint32_t slots, uint32_t keepFree);

    // "45s", "12m", "1h 05m".
    std::string Duration(uint32_t seconds);
}

#endif // PLAYERBOTS_PLUS_DUNGEONS_TEXT_H
