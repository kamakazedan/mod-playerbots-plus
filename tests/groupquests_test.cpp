// Tests for group quests' choices (src/PlayerbotsPlusGroupQuestsText.cpp),
// which have no game types in them: how big a party a quest wants, which
// quest a bot asks for help with, what a party looking for people does
// after an ask, and what a party does next. Not part of the server build:
// run tests/build.sh.

#include "PlayerbotsPlusGroupQuestsText.h"

#include <cstdio>
#include <string>
#include <vector>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    void Eq(std::string const& what, uint64_t got, uint64_t want)
    {
        ++g_checks;
        if (got != want)
        {
            ++g_fails;
            std::printf("FAIL %s: got %llu, want %llu\n", what.c_str(), (unsigned long long)got,
                        (unsigned long long)want);
        }
    }
}

int main()
{
    using namespace PbpGroupQuests;

    // ---- how big a party ---------------------------------------------------------------
    Eq("suggests three", PartySize(3, 3), 3);
    Eq("suggests five", PartySize(5, 3), 5);
    Eq("says none: the fallback", PartySize(0, 3), 3);
    Eq("one is no party: the fallback", PartySize(1, 4), 4);
    Eq("more than a party: five", PartySize(10, 3), 5);
    Eq("a fallback of one: two", PartySize(0, 1), 2);

    // ---- which quest to ask for ----------------------------------------------------------
    Eq("nothing held", QuestToAsk({}), 0);
    Eq("the one most others hold", QuestToAsk({ { 10, 20, 1 }, { 11, 22, 3 }, { 12, 18, 0 } }), 11);
    Eq("between equals, the lowest level", QuestToAsk({ { 10, 24, 2 }, { 11, 21, 2 }, { 12, 23, 2 } }), 11);
    Eq("nobody else holds any: the lowest", QuestToAsk({ { 10, 24, 0 }, { 11, 21, 0 } }), 11);

    // ---- after an ask ----------------------------------------------------------------------
    Eq("full: meet", uint64_t(AfterAsk(3, 3, 5, 60)), uint64_t(Next::Meet));
    Eq("short, still time: wait", uint64_t(AfterAsk(2, 3, 30, 60)), uint64_t(Next::Wait));
    Eq("short, out of time: go with who came", uint64_t(AfterAsk(2, 3, 60, 60)), uint64_t(Next::Meet));
    Eq("nobody came, out of time: alone", uint64_t(AfterAsk(1, 3, 61, 60)), uint64_t(Next::Solo));
    Eq("nobody yet, still time: wait", uint64_t(AfterAsk(1, 3, 0, 60)), uint64_t(Next::Wait));

    // ---- what next together -----------------------------------------------------------------
    std::vector<uint32_t> const order = { 30, 31, 32, 33 };
    Eq("the one the most hold", NextTogether({ { 1, { 30, 31 } }, { 2, { 31 } }, { 3, { 31, 32 } } }, order), 31);
    Eq("between equals, the first by level", NextTogether({ { 1, { 32, 30 } }, { 2, { 30, 32 } } }, order), 30);
    Eq("only one holds each: none", NextTogether({ { 1, { 30 } }, { 2, { 31 } } }, order), 0);
    Eq("one held twice by the same member counts once", NextTogether({ { 1, { 30, 30 } }, { 2, { 31 } } }, order),
       0);
    Eq("not one of the zone's: none", NextTogether({ { 1, { 99 } }, { 2, { 99 } } }, order), 0);

    std::printf("%d/%d passed\n", g_checks - g_fails, g_checks);
    return g_fails ? 1 : 0;
}
