// Tests for what a bot looted or made itself
// (src/PlayerbotsPlusLootedRules.cpp), which has no game types in it. Not part of the server build: AzerothCore
// only compiles a module's src/. Run tests/build.sh.

#include "PlayerbotsPlusLootedRules.h"

#include <cstdio>

namespace
{
    int g_checks = 0;
    int g_fails  = 0;

    void Check(char const* what, bool ok)
    {
        ++g_checks;
        if (!ok)
        {
            ++g_fails;
            std::printf("FAIL %s\n", what);
        }
    }

    // The record is known, and is exactly this.
    void Is(char const* what, PbpLooted::After const& a, uint32_t owner, uint32_t count, bool away,
            bool made = false)
    {
        Check(what, a.known && a.stack == PbpLooted::Stack{ owner, count, away, made });
    }

    void Gone(char const* what, PbpLooted::After const& a)
    {
        Check(what, !a.known);
    }
}

int main()
{
    using namespace PbpLooted;
    constexpr uint32_t bot = 42, other = 7;

    // ---- stored --------------------------------------------------------------------------
    {
        Is("a new stack of loot is its loot", Stored(nullptr, bot, 3, 3, From::Loot), bot, 3, false);
        Gone("playerbots' free stack is not", Stored(nullptr, bot, 20, 20, From::Other));
        Gone("loot onto a stack that was not loot is not", Stored(nullptr, bot, 2, 22, From::Loot));
        Gone("loot over several stacks is not known whole", Stored(nullptr, bot, 25, 5, From::Loot));
        Gone("nothing stored", Stored(nullptr, bot, 0, 0, From::Loot));

        Stack const own{ bot, 5, false };
        Is("more loot onto its loot", Stored(&own, bot, 2, 7, From::Loot), bot, 7, false);
        Is("more loot onto its loot, some used meanwhile", Stored(&own, bot, 2, 4, From::Loot), bot, 4, false);
        Gone("more loot onto its loot after something joined it", Stored(&own, bot, 2, 9, From::Loot));
        Gone("playerbots topping its loot up", Stored(&own, bot, 15, 20, From::Other));
        Gone("a vendor's onto its loot", Stored(&own, bot, 1, 6, From::Other));

        Stack const theirs{ other, 5, false };
        Gone("loot onto a stack someone else looted", Stored(&theirs, bot, 2, 7, From::Loot));
        Stack const away{ bot, 5, true };
        Gone("a stack that is away cannot take loot", Stored(&away, bot, 2, 7, From::Loot));
    }

    // ---- away and back -------------------------------------------------------------------
    {
        Stack const own{ bot, 5, false };
        Is("up for auction: away", MovedOut(&own, bot), bot, 5, true);
        Gone("someone else's loot leaving it is not its", MovedOut(&own, other));
        Gone("not loot, leaving", MovedOut(nullptr, bot));

        Stack const away{ bot, 5, true };
        Is("back unsold from the auction house: still its loot", MovedIn(&away, bot), bot, 5, false);
        Gone("its loot gone to someone else is not theirs", MovedIn(&away, other));
        Gone("a trade's or the post's joining its loot", MovedIn(&own, bot));
        Gone("not loot, coming in", MovedIn(nullptr, bot));
    }

    // ---- seen in its bags ----------------------------------------------------------------
    {
        Stack const own{ bot, 5, false };
        Is("as it was", Seen(&own, bot, 5), bot, 5, false);
        Is("some used or sold: the rest still its loot", Seen(&own, bot, 2), bot, 2, false);
        Gone("more in it, and no loot: something joined it", Seen(&own, bot, 6));
        Gone("with someone else", Seen(&own, other, 5));
        Gone("not loot", Seen(nullptr, bot, 5));

        Stack const away{ bot, 5, true };
        Is("away, and in its bags after all: back", Seen(&away, bot, 5), bot, 5, false);
    }

    // ---- a run of them: looted, topped up, sold ------------------------------------------
    {
        After a = Stored(nullptr, bot, 4, 4, From::Loot);              // four looted
        a = Stored(&a.stack, bot, 1, 5, From::Loot);                     // one more
        Is("looted twice", a, bot, 5, false);
        a = Seen(&a.stack, bot, 3);                                // two used
        a = MovedOut(&a.stack, bot);                               // listed
        a = MovedIn(&a.stack, bot);                                // back unsold
        Is("used, listed and back", a, bot, 3, false);
        a = Stored(&a.stack, bot, 20, 23, From::Other);                  // playerbots tops it up
        Gone("topped up for nothing: not its to sell", a);
    }

    // ---- made by its own craft -------------------------------------------------------------
    {
        Is("a new stack it made is its own", Stored(nullptr, bot, 5, 5, From::Made), bot, 5, false, true);
        Gone("what it made onto a stack that was not its own is not", Stored(nullptr, bot, 1, 21, From::Made));

        Stack const made{ bot, 5, false, true };
        Is("more made onto what it made", Stored(&made, bot, 1, 6, From::Made), bot, 6, false, true);
        Gone("playerbots topping what it made up", Stored(&made, bot, 20, 25, From::Other));
        After const listed = MovedOut(&made, bot);
        Is("made, up for auction: away, still made", listed, bot, 5, true, true);
        Is("made, back from the auction house: still made", MovedIn(&listed.stack, bot), bot, 5, false, true);
        Is("made, some used: the rest still made", Seen(&made, bot, 2), bot, 2, false, true);

        Stack const looted{ bot, 3, false, false };
        Is("made onto its loot: its own, some of it made", Stored(&looted, bot, 1, 4, From::Made), bot, 4, false,
           true);
        Is("loot onto what it made: its own, still some of it made", Stored(&made, bot, 2, 7, From::Loot), bot, 7,
           false, true);
    }

    std::printf("looted: %d checks, %d failed\n", g_checks, g_fails);
    return g_fails ? 1 : 0;
}
