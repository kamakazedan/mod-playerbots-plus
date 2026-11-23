#include "PlayerbotsPlusLootedRules.h"

namespace PbpLooted
{
    After Stored(Stack const* was, uint32_t owner, uint32_t count, uint32_t after, From from)
    {
        if (from == From::Other || !count || after < count)
            return {};   // not its own (or its own over several stacks): whatever it joined is not all its own now
        const bool made = from == From::Made;
        if (!was)
        {
            // Out of the loot window or its craft whole: a stack of its own.
            // Its own that joined a stack that was not is not.
            if (after != count)
                return {};
            return { true, { owner, after, false, made } };
        }
        // More of its own onto its own: still its own, unless something else
        // joined it first (more in it than when last seen, before this).
        if (was->owner != owner || was->away || after - count > was->count)
            return {};
        return { true, { owner, after, false, was->made || made } };
    }

    After MovedOut(Stack const* was, uint32_t owner)
    {
        if (!was || was->owner != owner)
            return {};
        return { true, { owner, was->count, true, was->made } };
    }

    After MovedIn(Stack const* was, uint32_t owner)
    {
        // Its own back from the post, the auction house or a trade, as it
        // went: still its own. Anything coming into one of its own that was
        // here all along, or someone else's: not.
        if (!was || was->owner != owner || !was->away)
            return {};
        return { true, { owner, was->count, false, was->made } };
    }

    After Seen(Stack const* was, uint32_t owner, uint32_t count)
    {
        // More in it than when last seen, and none of its own added: something
        // joined it. Fewer: some used, sold or split off, and the rest still
        // its own.
        if (!was || was->owner != owner || !count || count > was->count)
            return {};
        return { true, { owner, count, false, was->made } };
    }
}
