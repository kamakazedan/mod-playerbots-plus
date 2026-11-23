#ifndef PLAYERBOTS_PLUS_LOOTED_RULES_H
#define PLAYERBOTS_PLUS_LOOTED_RULES_H

// --------------------------------------------------------------------------
// A bot sells only what it looted or made itself: how the record of one
// stack changes with each thing that happens to it, as sums with no game
// types in them so they can be tested without a server
// (tests/looted_test.cpp).
//
// A stack is a bot's own when it came out of a loot window, a roll or its
// own craft whole, and stays so while nothing but more of its own joins it.
// Anything else that joins it -- playerbots topping it up, a vendor's, a
// trade's, the post's -- and it is not its own any more, however little of
// it that was: it can only ever err the safe way, some of its own no longer
// counted, never a free thing sold.
//
// The game side is PlayerbotsPlusLooted.
// --------------------------------------------------------------------------

#include <cstdint>

namespace PbpLooted
{
    // A stack known to be a bot's own: whose, how many were in it when last
    // seen, whether it is away -- up for auction, in the post, in a trade or
    // a guild bank -- and whether any of it was made (a craft's) rather than
    // looted.
    struct Stack
    {
        uint32_t owner = 0;   // the bot, by its character's guid
        uint32_t count = 0;
        bool     away  = false;
        bool     made  = false;

        bool operator==(Stack const&) const = default;
    };

    // What the record is afterwards: none (not its own, or not any more), or
    // the stack as it is now.
    struct After
    {
        bool  known = false;
        Stack stack;
    };

    // Where what was stored came from.
    enum class From : uint8_t
    {
        Other,   // anything but the two below: playerbots, a vendor, a quest...
        Loot,    // a loot window or a roll
        Made     // a craft of its own
    };

    // `count` were stored for `owner` (StoreNewItem), leaving `after` in the
    // stack they went to, from `from`. `was` is the stack's record, if it has
    // one.
    After Stored(Stack const* was, uint32_t owner, uint32_t count, uint32_t after, From from);

    // The stack left `owner`'s bags whole: up for auction, in the post, in a
    // trade, in a guild bank.
    After MovedOut(Stack const* was, uint32_t owner);

    // What came into `owner`'s bags out of the post, a trade or a guild bank
    // ended up in this stack: the one that came, or one of its own it joined.
    After MovedIn(Stack const* was, uint32_t owner);

    // Seen in `owner`'s bags just now, with `count` in it.
    After Seen(Stack const* was, uint32_t owner, uint32_t count);
}

#endif // PLAYERBOTS_PLUS_LOOTED_RULES_H
