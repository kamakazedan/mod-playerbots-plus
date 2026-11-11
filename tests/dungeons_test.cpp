// Tests for bot dungeons' choices and words (src/PlayerbotsPlusDungeonsText.cpp),
// which have no game types in them: who goes in a party, when a quest is grey,
// which wing a dungeon finder entry is, what a full quest log lets go of, and
// how long things took. Not part of the server build: run tests/build.sh.

#include "PlayerbotsPlusDungeonsText.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    void True(std::string const& what, bool ok)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_fails;
            std::printf("FAIL %s\n", what.c_str());
        }
    }

    void EqS(std::string const& what, std::string const& got, std::string const& want)
    {
        ++g_checks;
        if (got != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got '%s', want '%s'\n", what.c_str(), got.c_str(), want.c_str());
        }
    }

    std::string Ids(std::vector<uint64_t> const& ids)
    {
        std::string out;
        for (uint64_t id : ids)
            out += (out.empty() ? "" : ",") + std::to_string(id);
        return out;
    }

    std::string Ids32(std::vector<uint32_t> const& ids)
    {
        std::string out;
        for (uint32_t id : ids)
            out += (out.empty() ? "" : ",") + std::to_string(id);
        return out;
    }
}

int main()
{
    using namespace PbpDungeons;
    using C = Candidate;

    // ---- who goes ----------------------------------------------------------------------
    {
        std::vector<C> pool = {
            { 1, Role::Damage, 20, 0 }, { 2, Role::Damage, 21, 0 }, { 3, Role::Tank, 20, 0 },
            { 4, Role::Healer, 22, 0 }, { 5, Role::Damage, 19, 0 },
        };
        EqS("tank, healer, then damage in order", Ids(PickParty(pool, 20, false)), "3,4,1,2,5");
        True("no quest, none needed: still a party", !PickParty(pool, 20, false).empty());
        True("no quest, one needed: no party", PickParty(pool, 20, true).empty());

        pool[4].quests = 1;   // a damage dealer holds a quest
        EqS("a holder makes it a quest party", Ids(PickParty(pool, 20, true)), "3,4,5,1,2");

        std::vector<C> noHealer = { { 1, Role::Damage, 20, 1 }, { 2, Role::Damage, 20, 0 },
                                    { 3, Role::Tank, 20, 0 }, { 5, Role::Damage, 20, 0 } };
        True("no healer: no party", PickParty(noHealer, 20, false).empty());

        std::vector<C> twoDps = { { 1, Role::Damage, 20, 0 }, { 2, Role::Damage, 20, 0 },
                                  { 3, Role::Tank, 20, 0 }, { 4, Role::Healer, 20, 0 } };
        True("two damage dealers: no party", PickParty(twoDps, 20, false).empty());
    }
    {
        // Holders first, the more quests the better; then the nearest level.
        std::vector<C> pool = {
            { 10, Role::Tank, 30, 0 },   // far off the level
            { 11, Role::Tank, 21, 0 },   // near it
            { 12, Role::Tank, 25, 2 },   // holds two quests
            { 20, Role::Healer, 20, 0 },
            { 21, Role::Healer, 23, 1 },
            { 30, Role::Damage, 26, 0 }, { 31, Role::Damage, 20, 0 }, { 32, Role::Damage, 21, 1 },
            { 33, Role::Damage, 19, 3 }, { 34, Role::Damage, 22, 0 },
        };
        EqS("holders first, then the level", Ids(PickParty(pool, 20, true)), "12,21,33,32,31");
        pool[2].quests = 0;
        pool[4].quests = 0;
        EqS("nearest level when nobody holds one", Ids(PickParty(pool, 20, true)), "11,20,33,32,31");
        EqS("ties go to whoever came first", Ids(PickParty({ { 1, Role::Tank, 20, 0 }, { 2, Role::Tank, 20, 0 },
                                                             { 3, Role::Healer, 20, 0 }, { 4, Role::Damage, 21, 0 },
                                                             { 5, Role::Damage, 19, 0 }, { 6, Role::Damage, 20, 0 } },
                                                           20, false)),
            "1,3,6,4,5");
    }
    EqS("role names", std::string(RoleName(Role::Tank)) + RoleName(Role::Healer) + RoleName(Role::Damage),
        "tankhealerdamage");

    // ---- grey ----------------------------------------------------------------------------
    True("a quest of its own level is never grey", !Grey(-1, 70));
    True("at the grey level: grey", Grey(13, 13));
    True("below: grey", Grey(10, 13));
    True("above: not", !Grey(14, 13));
    True("level 0 quests are never grey", !Grey(0, 13));

    // ---- wings -----------------------------------------------------------------------------
    True("library", SameWing("Scarlet Monastery - Library", "Scarlet Monastery (Library)"));
    True("not the armory", !SameWing("Scarlet Monastery - Library", "Scarlet Monastery (Armory)"));
    True("any case", SameWing("DIRE MAUL - EAST", "Dire Maul (East)"));
    True("no brackets matches nothing", !SameWing("Maraudon", "Maraudon"));
    True("empty brackets match nothing", !SameWing("Maraudon - Purple", "Maraudon ()"));
    True("upper city", SameWing("Blackrock Depths - Upper City", "Blackrock Depths (Upper City)"));

    // ---- a full quest log --------------------------------------------------------------------
    {
        auto entry = [](uint32_t quest, bool keep, bool failed, bool grey, bool otherZone, int32_t level)
        {
            LogEntry e;
            e.quest = quest;
            e.keep = keep;
            e.failed = failed;
            e.grey = grey;
            e.otherZone = otherZone;
            e.level = level;
            return e;
        };
        std::vector<LogEntry> log;
        for (uint32_t q = 1; q <= 20; ++q)
            log.push_back(entry(q, false, false, false, false, 20));
        True("five free, three wanted: nothing", ToDrop(log, 25, 3).empty());

        log.push_back(entry(21, false, false, false, true, 25));    // another zone
        log.push_back(entry(22, false, false, true, false, 30));    // grey
        log.push_back(entry(23, true, true, false, false, 18));     // failed, but kept
        log.push_back(entry(24, false, true, false, false, 19));    // failed
        // 24 used, one free: two to go
        EqS("failed first, then grey", Ids32(ToDrop(log, 25, 3)), "24,22");
        log.push_back(entry(25, false, false, false, false, 12));   // the lowest
        // full: three to go
        EqS("then another zone", Ids32(ToDrop(log, 25, 3)), "24,22,21");
        log.erase(log.begin() + 20);   // the other zone's gone
        log.push_back(entry(26, false, false, false, false, 40));
        EqS("then the lowest", Ids32(ToDrop(log, 25, 3)), "24,22,25");

        std::vector<LogEntry> kept;
        for (uint32_t q = 1; q <= 25; ++q)
            kept.push_back(entry(q, q != 7, false, false, false, 20));
        EqS("only what may go goes", Ids32(ToDrop(kept, 25, 3)), "7");
    }

    // ---- how long ----------------------------------------------------------------------------
    EqS("seconds", Duration(45), "45s");
    EqS("minutes", Duration(12 * 60 + 30), "12m");
    EqS("hours", Duration(3600 + 5 * 60), "1h 05m");
    EqS("zero", Duration(0), "0s");

    std::printf("%d/%d passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
