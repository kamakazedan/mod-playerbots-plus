#ifndef PLAYERBOTS_PLUS_LOOTED_H
#define PLAYERBOTS_PLUS_LOOTED_H

// --------------------------------------------------------------------------
// What a bot looted or made itself: the only things it may ever sell.
//
// Every bot, wherever it sells -- the auction house (the market here), Trade
// chat (mod-ollama-chat, through PlayerbotsPlus::LootedByBot) -- sells only
// what it looted, from a kill, a chest, a herb or an ore vein, a skinned
// beast, fishing, or won on a roll, and what it made itself with its
// professions (PlayerbotsPlusCrafting), whatever went into it. Never what
// playerbots hands it for nothing (gear, ammo, food and drink, potions,
// reagents, sharpening stones and weightstones, oils, poisons, bags, again at
// every refresh), nor anything it bought, was given or got from a quest.
// "selling [Adamantite Weightstone] x20, 3g 30s!" went out in Trade:
// playerbots' own free weightstones, which no list of free things had caught.
//
// It is kept by the stack (PbpLooted's rules): a stack that came out of a
// loot window, a roll or its own craft whole is the bot's own, and stays so
// while nothing but more of its own joins it -- used up, sold from, put up for
// auction and back, it still is; topped up by playerbots, joined by a
// vendor's, a trade's or the post's, it is not any more. Only what could ever
// be sold to someone is kept -- of common quality or better, worth something,
// and able to change hands -- and gear an enchanter could break down, bound
// or not: it disenchants only its own (PlayerbotsPlusCrafting). What a
// Disenchant roll gives its winner is loot too.
//
// The hooks only note what happened, under a mutex: most run on map threads.
// The world tick sorts the notes out (and so does a question, first), looks
// over the stacks it knows now and then for the ones used up, and keeps them
// in the characters database (playerbots_plus_looted), so they are still
// known after a restart. Whatever a bot had before this came in is not known
// to be its own, so it is never sold.
// --------------------------------------------------------------------------

#include "ScriptMgr.h"

#include <cstddef>
#include <cstdint>

class Item;
class Player;

class PbpLootedPlayerScript : public PlayerScript
{
public:
    PbpLootedPlayerScript();

    void OnPlayerStoreNewItem(Player* player, Item* item, uint32 count) override;
    void OnPlayerLootItem(Player* player, Item* item, uint32 count, ObjectGuid lootguid) override;
    void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 count, RollVote voteType,
                                     Roll* roll) override;
    void OnPlayerCreateItem(Player* player, Item* item, uint32 count) override;
    void OnPlayerAfterMoveItemFromInventory(Player* player, Item* it, uint8 bag, uint8 slot, bool update) override;
    void OnPlayerAfterMoveItemToInventory(Player* player, Item* it, bool update) override;
};

// Did `bot` loot or make this stack itself, with nothing but more of its own
// added to it since? `item` is one in its bags. Only such a stack is ever
// sold. `made` (if given): some of it was made, not looted -- the market lists
// a potion or a bandage the bot made, never one it looted (playerbots hands
// those out). World thread.
bool PbpLooted_IsOwn(Player* bot, Item* item, bool* made = nullptr);

// A stack of a bot's own that it made (some of it), wherever it is now: in
// its bags, or away -- up for auction, in the post. By the item's guid
// (its low part). World thread.
bool PbpLooted_KnownMade(uint32_t itemGuidLow);

// `player` has just won a Disenchant roll, on this thread: what the game
// stores for it straight after is loot (it is given with no loot hook). From
// the packet hook that sees the win (PlayerbotsPlusRolls); notes only.
void PbpLooted_DisenchantWon(Player const* player);

void PbpLooted_Load();                  // startup: the stacks known, the gone ones forgotten
void PbpLooted_Update(uint32_t diff);   // world tick
void PbpLooted_Shutdown();              // what changed, written

struct PbpLootedStats
{
    uint64_t stored  = 0;   // things stored for bots, noted
    uint64_t looted  = 0;   // ...that were loot
    uint64_t made    = 0;   // ...that they made
    uint64_t marked  = 0;   // stacks that came out of a loot window or a craft whole: theirs to sell
    uint64_t joined  = 0;   // their own, something else added to it: not theirs to sell any more
    uint64_t moved   = 0;   // their own, found with someone else
    uint64_t gone    = 0;   // their own, used up, sold to a vendor or destroyed
    uint64_t asked   = 0;   // stacks asked about before being sold (the market, Trade chat)
    uint64_t refused = 0;   // ...not their own: not sold
    uint64_t dropped = 0;   // notes dropped, too many at once (those stacks are not known loot)
    uint64_t writes  = 0;   // times written to the database
    size_t   known   = 0;   // stacks of their own known now
    size_t   knownMade = 0; // ...of which some was made
};
PbpLootedStats PbpLooted_GetStats();

#endif // PLAYERBOTS_PLUS_LOOTED_H
