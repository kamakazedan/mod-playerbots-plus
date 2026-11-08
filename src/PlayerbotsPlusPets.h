#ifndef PLAYERBOTS_PLUS_PETS_H
#define PLAYERBOTS_PLUS_PETS_H

#include <cstdint>

// --------------------------------------------------------------------------
// Pets the way players keep them.
//
// Hunter pets get names. Playerbots tames a beast and leaves it called what
// it is, "Boar"; players rename theirs, mostly after something they
// love or for a laugh: a cat called Aslan, a boar called Porkchop, a worm
// called Graboid. A random bot's hunter names its pet the same way, from
// PlayerbotsPlus.Pets.Names.<Family> or else .Any, exactly as the game does
// when a player renames one (the same checks, the same one rename, the name
// saved). Some never bother: Pets.NameChance of them do.
//
// Companions. Players carry a few non-combat pets and some have one trotting
// along: a cat from the Elwynn pet seller, a snake from Orgrimmar, a parrot
// from Booty Bay, something rare that took a week to drop, a snowman at
// Winter Veil. A random bot near a real player learns some of
// PlayerbotsPlus.Companions.Alliance / .Horde / .Both: only what suits its
// side and what is had at its level (most own a common one or two, a few
// collect the rare ones; in a holiday, its own are on offer). About
// Companions.OutChance of the bots near someone have one out, decided again
// every half hour or so, and one that has not puts its companion away. A
// holiday's own come out far more in their season. Once in a stretch with
// nothing on, Companions.SwitchChance of those with one out swap it for
// another they own. Bots nobody can see get nothing, and nothing is ever
// summoned in a dungeon or a battleground.
//
// World thread only: the world tick.
// --------------------------------------------------------------------------

void PbpPets_Update(uint32_t diff);

// Settings changed (.pbp reload): what was worked out from them is again.
void PbpPets_Reset();

struct PbpPetsStats
{
    uint64_t named       = 0;   // hunter pets given a name
    uint64_t keptName    = 0;   // ...and left as they were tamed, by choice
    uint64_t learned     = 0;   // companions learned
    uint64_t summoned    = 0;   // companions brought out
    uint64_t switched    = 0;   // ...swapped for another
    uint64_t putAway     = 0;   // ...and put away
    uint32_t guildWait   = 0;   // bots near someone that wait for their first while their guild has someone real on
    uint32_t companions  = 0;   // in the list that could be read
    uint32_t badEntries  = 0;   // ...and ones that could not
};
PbpPetsStats PbpPets_GetStats();

#endif // PLAYERBOTS_PLUS_PETS_H
