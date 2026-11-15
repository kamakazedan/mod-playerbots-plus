#include "PlayerbotsPlusLootedRules.h"

namespace PbpLooted
{
    After Stored(Stack const* was, uint32_t owner, uint32_t count, uint32_t after, bool loot)
    {
        if (!loot || !count || after < count)
            return {};   // not loot (or loot over several stacks): whatever it joined is not all loot now
        if (!was)
        {
            // Out of the loot window whole: a stack of its own loot. Loot
            // that joined a stack that was not is not.
            if (after != count)
                return {};
            return { true, { owner, after, false } };
        }
        // More loot onto its own loot: still its loot, unless something else
        // joined it first (more in it than when last seen, before this).
        if (was->owner != owner || was->away || after - count > was->count)
            return {};
        return { true, { owner, after, false } };
    }

    After MovedOut(Stack const* was, uint32_t owner)
    {
        if (!was || was->owner != owner)
            return {};
        return { true, { owner, was->count, true } };
    }

    After MovedIn(Stack const* was, uint32_t owner)
    {
        // Its own loot back from the post, the auction house or a trade, as
        // it went: still its loot. Anything coming into one of its own that
        // was here all along, or someone else's loot: not.
        if (!was || was->owner != owner || !was->away)
            return {};
        return { true, { owner, was->count, false } };
    }

    After Seen(Stack const* was, uint32_t owner, uint32_t count)
    {
        // More in it than when last seen, and no loot: something joined it.
        // Fewer: some used, sold or split off, and the rest still its loot.
        if (!was || was->owner != owner || !count || count > was->count)
            return {};
        return { true, { owner, count, false } };
    }
}
