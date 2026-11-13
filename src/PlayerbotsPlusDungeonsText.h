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

    // Whether one who does the damage could heal instead (a priest, druid,
    // shaman or paladin), and whether its gear is a caster's: a shadow
    // priest's, a balance druid's or an elemental shaman's has the stats a
    // healer wants, a retribution paladin's, an enhancement shaman's or a
    // cat's does not.
    enum class StepIn : uint8_t
    {
        No,
        Melee,
        Caster
    };

    // A bot the planner could send.
    struct Candidate
    {
        uint64_t id     = 0;      // its guid
        Role     role   = Role::Damage;
        uint32_t level  = 0;
        uint32_t quests = 0;      // unfinished quests it holds for this dungeon
        StepIn   stepIn = StepIn::No;
    };

    // A party for one dungeon: a tank, a healer and three to do the damage.
    // Those holding quests for it go first (the more the better), then
    // whoever's level is closest to `target`, then whoever came first. With
    // `stepIn` and nobody among them who heals, one who does the damage and
    // could heal does (BotDungeons.HealerStepIn): a caster before the rest,
    // then in the same order, and three others do the damage. Empty when a
    // role cannot be filled, or, with `needQuest`, when nobody in it holds a
    // quest for the dungeon. The tank comes first in what is returned, then
    // the healer.
    std::vector<uint64_t> PickParty(std::vector<Candidate> const& candidates, uint32_t target, bool needQuest,
                                    bool stepIn = false);

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

    // ---- level bands (BotDungeons.LevelBands) -------------------------------------------

    // The dungeons whose level is from `low` to `high`.
    struct Band
    {
        uint32_t low  = 0;
        uint32_t high = 0;
    };

    // "15-19, 20-29,70-80" -> those bands, in that order ("80" is 80-80). What
    // does not read as a band (not numbers, the high end below the low) is
    // left out and named in `bad`, comma-separated.
    std::vector<Band> ParseBands(std::string const& text, std::string& bad);

    // "1,1,1,1,1,1,4" -> a share for each of `bands` bands: the number given,
    // else 1, so every band has one unless it is given 0. What does not read
    // as a number counts as 1 and is named in `bad`.
    std::vector<uint32_t> ParseShares(std::string const& text, size_t bands, std::string& bad);

    // Which band `level` is in: the first that holds it, -1 for none.
    int BandOf(std::vector<Band> const& bands, uint32_t level);

    // "15-19"; "80" for a band of one level.
    std::string BandName(Band const& band);

    // The bands in the order a free place is offered to them, given the runs
    // each holds now and the share each is meant to hold: the one furthest
    // under its share first (the fewest runs for its share), between equals
    // the bigger share, then the lower band. A band at or over its share comes
    // after every band under its own, and one with no share after all of
    // them. So with as many places as the shares add up to, every band ends
    // up with its share; a band that cannot make a party just then is passed
    // over, and the next one in the order has its place until a run ends.
    std::vector<size_t> BandOrder(std::vector<uint32_t> const& runs, std::vector<uint32_t> const& shares);

    // ---- free memory (BotDungeons.MinFreeMemoryMB) -------------------------------------

    // The memory available for new work, in MB, from the text of
    // /proc/meminfo: its MemAvailable line, the kernel's own estimate, which
    // counts the file cache it can let go of as available. From a kernel too
    // old to have one (before 3.14), MemFree + Buffers + Cached. -1 when the
    // text has neither.
    int64_t MemAvailableMb(std::string const& meminfo);

    // "850 MB", "4 GB", "19.2 GB" (a GB is 1024 MB, as /proc/meminfo counts).
    std::string MemoryText(int64_t mb);
}

#endif // PLAYERBOTS_PLUS_DUNGEONS_TEXT_H
