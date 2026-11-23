#ifndef PLAYERBOTS_PLUS_CRAFTING_H
#define PLAYERBOTS_PLUS_CRAFTING_H

// --------------------------------------------------------------------------
// Bots craft.
//
// No random bot ever crafted anything on its own: playerbots only crafts for
// someone who tells it to, and the auction house market bought bots
// materials they never used. Now a few bots at a time (Crafting.*) -- random
// bots on their own, out of a fight and out of any group, not hanging out in
// a city -- look at what their professions can make from what they carry,
// and make what is below. The crowd hanging out in a city does too, as part
// of city life, where someone sees it (CityLife.Craft: PbpCrafting_City*):
// at its place, or at a forge or an anvil of the city it walks over to.
//
//   * an upgrade for themselves -- once, and put on;
//   * else what may raise their skill (PbpCraft::PointChance), the cheapest
//     chance at a point first -- the grind of a profession, as a player does
//     it;
//   * else what is worth well more than what goes into it, to sell.
//
// What they make is theirs to sell, on the auction house and in Trade chat
// (PlayerbotsPlusLooted: made, like loot), whatever went into it. Their
// skill rises only from the game's own skill-ups (PlayerbotsPlusSkills).
//
// A recipe that wants a forge, an anvil or a fire (smelting, blacksmithing,
// cooking) is made at one: one in reach where the bot stands, as the game
// itself looks for it; or, in a town and with nobody about to see, the bot is
// put by one of the town's and back after (Crafting.StationVisits), the way
// playerbots puts a bot whose walk gets nowhere where it was going. One out
// of town with nothing else to make but what it has the makings of for a
// forge or an anvil is looked at again next time it is in town. A cook
// makes its own campfire (Crafting.Campfire) when it has what that takes. In
// a city or at an inn, thread, flux, vials and missing tools are bought from a
// vendor nearby that sells them, at its price (Crafting.BuyFromVendors).
//
// Every craft is the game's own: the spell cast, the reagents, tools and
// station it checks, the cast time stood through, the skill-up it rolls. The
// bot's playerbots AI is held still while it works, and let go for anything
// it has to answer: a fight, a trade, a group invite, a duel. Enchanting
// gear, disenchanting, prospecting and milling are not done yet.
//
// World thread.
// --------------------------------------------------------------------------

#include "PlayerbotsPlusReport.h"

#include <cstdint>
#include <string>

class Player;

void PbpCrafting_Update(uint32_t diff);   // world tick

// Is `bot` crafting just now, on its own (held at it)? Other parts leave
// it alone. A craft city life started is the hangout's, and not this.
bool PbpCrafting_Busy(Player* bot);

// Is `bot` at any craft just now, city life's too?
bool PbpCrafting_AtIt(Player* bot);

// ---- City life (PlayerbotsPlusHangout): the crowd hanging out crafts too,
// where someone sees it -- at its place, or at a forge or an anvil of the
// city it walks over to. World thread.

// Something `bot` could make now with what it carries and what the city's
// vendors sell: false if nothing. A forge, an anvil or a fire wanted is one
// within `walk` yards with a way there: `x, y, z` where to stand by it, `o`
// facing it; else `x, y, z` is where it stands. Kept for CityBegin, for two
// minutes.
bool PbpCrafting_CityPlan(Player* bot, float walk, float& x, float& y, float& z, float& o);

// Got there: buys what it is short of and starts. False: it could not (the
// plan is dropped). Its AI stays the hangout's to hold.
bool PbpCrafting_CityBegin(Player* bot);

// Stops a craft city life started (a cast is cut short), and drops a plan.
void PbpCrafting_CityCancel(Player* bot);

// .pbp craft [name]: what it would make now and why, and start it. False
// with `report` saying why not.
bool PbpCrafting_Now(Player* bot, std::string& report);

struct PbpCraftingStats
{
    uint64_t looked       = 0;   // bots looked at
    uint64_t nothing      = 0;   // ...nothing worth making
    uint64_t noRoom       = 0;   // ...something, but no room in their bags for it
    uint64_t waitTown     = 0;   // ...nothing, but something at a forge or an anvil: looked at again in town
    uint64_t sessions     = 0;   // went to work
    uint64_t cityLife     = 0;   // ...of them hanging out in a city, someone near to see
    uint64_t forUse       = 0;   // ...an upgrade for itself
    uint64_t forSkill     = 0;   // ...for a skill-up
    uint64_t forSale      = 0;   // ...to sell
    uint64_t casts        = 0;   // crafts begun
    uint64_t failed       = 0;   // crafts the game turned down (why, in the report)
    uint64_t atStation    = 0;   // at a forge, an anvil or a fire
    uint64_t putBy        = 0;   // ...put by one, nobody about
    uint64_t putBack      = 0;   // ...and back where it was after
    uint64_t campfires    = 0;   // campfires made
    uint64_t bought       = 0;   // things bought from vendors for it
    uint64_t boughtCopper = 0;
    uint64_t cutShort     = 0;   // stopped: a fight, a trade, an invite, moved off, died, logged out
    uint32_t recipes      = 0;   // recipes known to the module
    uint32_t working      = 0;   // at it now
};
PbpCraftingStats PbpCrafting_GetStats();

// .pbp status and the report.
PbpBrief PbpCrafting_Brief(bool full);

#endif // PLAYERBOTS_PLUS_CRAFTING_H
