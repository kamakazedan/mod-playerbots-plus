#ifndef PLAYERBOTS_PLUS_LEDGER_H
#define PLAYERBOTS_PLUS_LEDGER_H

// --------------------------------------------------------------------------
// The economy ledger: what the random bots' economy does, day by day. It
// changes nothing; it is the numbers to go by before anything about the
// economy changes (professions that craft, gear that has to be earned,
// selling in trade chat).
//
//   - gold: what comes in (looted, quest rewards, auction house mail, the
//     rest) and goes out (auction buys and deposits, the rest), and what
//     playerbots gives or takes outside the game's own payments (the top-up
//     whenever it refreshes a bot, level resets) -- the difference between
//     what a bot has and what the game paid it, at each look;
//   - the auction house: what bots list, and who buys it (the AH bot, a
//     player, another bot); what bots buy, and from whom; what comes back;
//   - what they loot, by kind (herbs, ore, leather, cloth, gems, gear...),
//     make, and buy from vendors;
//   - where the gear they put on came from: handed out by playerbots,
//     looted, a quest reward, the auction house, a vendor, made;
//   - what they use up: food, potions, elixirs, flasks, bandages, oils;
//   - every Ledger.CensusMinutes, a look at all of them: gold held, item
//     level by level band, professions, who is in the dungeon queue, what
//     they carry and have in the bank, and who has what up for auction.
//
// Kept in the characters database (playerbots_plus_ledger), a row per day
// and figure, written every Ledger.FlushMinutes and at shutdown, kept for
// Ledger.KeepDays. .pbp economy [days] shows it, and each day's is logged
// once the day is over.
//
// The player and auction hooks only note what happened, under a mutex: most
// run on map threads (looting, quests, spells, putting gear on). The world
// tick sorts the notes out, and only it touches anything else.
//
// The loot, roll, quest-reward, crafting and vendor hooks never follow their
// Item*: a module whose hook runs first may have destroyed it, and a new item
// destroyed is deleted there and then (mod-junk-to-gold sells and destroys a
// bot's grey loot in its own OnPlayerLootItem, which runs before ours). What
// they need is read in OnPlayerStoreNewItem, inside StoreNewItem, where the
// item is sure to be there, and matched by its address on the same thread.
// --------------------------------------------------------------------------

#include "ScriptMgr.h"

#include <cstdint>
#include <string>
#include <vector>

class Item;
class Player;
class Spell;
struct AuctionEntry;
struct Loot;

class PbpLedgerPlayerScript : public PlayerScript
{
public:
    PbpLedgerPlayerScript();

    void OnPlayerStoreNewItem(Player* player, Item* item, uint32 count) override;
    void OnPlayerMoneyChanged(Player* player, int32& amount) override;
    void OnPlayerBeforeLootMoney(Player* player, Loot* loot) override;
    void OnPlayerCompleteQuest(Player* player, Quest const* quest) override;
    void OnPlayerLootItem(Player* player, Item* item, uint32 count, ObjectGuid lootguid) override;
    void OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 count, RollVote voteType,
                                     Roll* roll) override;
    void OnPlayerQuestRewardItem(Player* player, Item* item, uint32 count) override;
    void OnPlayerCreateItem(Player* player, Item* item, uint32 count) override;
    void OnPlayerAfterStoreOrEquipNewItem(Player* player, uint32 vendorslot, Item* item, uint8 count, uint8 bag,
                                          uint8 slot, ItemTemplate const* pProto, Creature* pVendor,
                                          VendorItem const* crItem, bool bStore) override;
    void OnPlayerEquip(Player* player, Item* it, uint8 bag, uint8 slot, bool update) override;
    void OnPlayerSpellCast(Player* player, Spell* spell, bool skipCheck) override;
};

// From the auction house hooks (PlayerbotsPlusScripts): an auction put up,
// and the mail that tells its owner it sold or came back -- the one moment the
// game says so whoever bought it, the AH bot's buyouts included. Any thread.
void PbpLedger_OnAuctionAdded(AuctionEntry const* auction);
void PbpLedger_OnSaleMailed(AuctionEntry const* auction);
void PbpLedger_OnExpiryMailed(AuctionEntry const* auction);

// After the config is read: the table made if it is not there yet (at
// startup), and days older than Ledger.KeepDays let go.
void PbpLedger_Load(bool reload);

// World tick.
void PbpLedger_Update(uint32_t diff);

// At shutdown: what is noted and not written yet, written now.
void PbpLedger_Shutdown();

// The market took `copper` out of a random bot's auction house mail.
void PbpLedger_MailMoney(uint64_t copper);

// .pbp economy [days]: the lines to print.
std::vector<std::string> PbpLedger_Report(uint32_t days);

struct PbpLedgerStats
{
    uint64_t    notes    = 0;   // things noted since startup
    uint64_t    dropped  = 0;   // ...and dropped, too many at once
    uint64_t    writes   = 0;   // times written to the database
    uint32_t    looked   = 0;   // bots at the last look
    std::string lookedAt;       // when, "11 Nov 01:30"; empty if not yet
};
PbpLedgerStats PbpLedger_GetStats();

#endif // PLAYERBOTS_PLUS_LEDGER_H
