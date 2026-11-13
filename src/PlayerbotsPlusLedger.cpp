#include "PlayerbotsPlusLedger.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusLedgerText.h"

#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "LFGMgr.h"
#include "Log.h"
#include "LootMgr.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "World.h"
#include "WorldSession.h"

#include "PlayerbotAIConfig.h"
#include "RandomPlayerbotMgr.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <map>
#include <mutex>
#include <set>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// The ledger's own numbers for the item template's classes are the game's.
static_assert(PbpLedger::ItemClass::Consumable == ITEM_CLASS_CONSUMABLE);
static_assert(PbpLedger::ItemClass::Weapon == ITEM_CLASS_WEAPON);
static_assert(PbpLedger::ItemClass::Gem == ITEM_CLASS_GEM);
static_assert(PbpLedger::ItemClass::Armor == ITEM_CLASS_ARMOR);
static_assert(PbpLedger::ItemClass::TradeGoods == ITEM_CLASS_TRADE_GOODS);
static_assert(PbpLedger::TradeSub::Parts == ITEM_SUBCLASS_PARTS);
static_assert(PbpLedger::TradeSub::Jewelcrafting == ITEM_SUBCLASS_JEWELCRAFTING);
static_assert(PbpLedger::TradeSub::Cloth == ITEM_SUBCLASS_CLOTH);
static_assert(PbpLedger::TradeSub::Leather == ITEM_SUBCLASS_LEATHER);
static_assert(PbpLedger::TradeSub::MetalStone == ITEM_SUBCLASS_METAL_STONE);
static_assert(PbpLedger::TradeSub::Meat == ITEM_SUBCLASS_MEAT);
static_assert(PbpLedger::TradeSub::Herb == ITEM_SUBCLASS_HERB);
static_assert(PbpLedger::TradeSub::Elemental == ITEM_SUBCLASS_ELEMENTAL);
static_assert(PbpLedger::TradeSub::Enchanting == ITEM_SUBCLASS_ENCHANTING);
static_assert(PbpLedger::ConsumableSub::Potion == ITEM_SUBCLASS_POTION);
static_assert(PbpLedger::ConsumableSub::Elixir == ITEM_SUBCLASS_ELIXIR);
static_assert(PbpLedger::ConsumableSub::Flask == ITEM_SUBCLASS_FLASK);
static_assert(PbpLedger::ConsumableSub::Scroll == ITEM_SUBCLASS_SCROLL);
static_assert(PbpLedger::ConsumableSub::Food == ITEM_SUBCLASS_FOOD);
static_assert(PbpLedger::ConsumableSub::Enhancement == ITEM_SUBCLASS_ITEM_ENHANCEMENT);
static_assert(PbpLedger::ConsumableSub::Bandage == ITEM_SUBCLASS_BANDAGE);
static_assert(PbpLedger::Quality::Poor == ITEM_QUALITY_POOR);
static_assert(PbpLedger::Quality::Uncommon == ITEM_QUALITY_UNCOMMON);
static_assert(PbpLedger::Quality::Rare == ITEM_QUALITY_RARE);
static_assert(PbpLedger::Quality::Epic == ITEM_QUALITY_EPIC);
static_assert(PbpLedger::kProfessions[0].skill == SKILL_MINING);
static_assert(PbpLedger::kProfessions[1].skill == SKILL_HERBALISM);
static_assert(PbpLedger::kProfessions[2].skill == SKILL_SKINNING);
static_assert(PbpLedger::kProfessions[3].skill == SKILL_BLACKSMITHING);
static_assert(PbpLedger::kProfessions[4].skill == SKILL_ENGINEERING);
static_assert(PbpLedger::kProfessions[5].skill == SKILL_JEWELCRAFTING);
static_assert(PbpLedger::kProfessions[6].skill == SKILL_LEATHERWORKING);
static_assert(PbpLedger::kProfessions[7].skill == SKILL_ALCHEMY);
static_assert(PbpLedger::kProfessions[8].skill == SKILL_INSCRIPTION);
static_assert(PbpLedger::kProfessions[9].skill == SKILL_TAILORING);
static_assert(PbpLedger::kProfessions[10].skill == SKILL_ENCHANTING);

namespace
{
    using Clock = std::chrono::steady_clock;

    // ---- notes, from whatever thread a hook runs on ------------------------------------

    enum class NoteKind : uint8_t
    {
        Money,            // the game paid a bot, or took from it
        LootMoney,        // ...the gold on something it looted
        QuestMoney,       // ...a quest's reward
        Looted,           // something got: looted (or won on a roll)
        QuestItem,        // ...a quest reward
        Made,             // ...made: crafted or conjured
        Vendor,           // ...bought from a vendor
        Equip,            // a piece of gear put on
        Used,             // a consumable used
        AuctionAdded,     // anyone's auction put up
        AuctionSold,      // ...sold: the mail to its owner
        AuctionExpired,   // ...back unsold: the mail to its owner
    };

    struct Note
    {
        NoteKind kind    = NoteKind::Money;
        bool     node    = false;   // looted from a game object: a herb, an ore vein, a chest, fishing
        uint32_t account = 0;       // the player's; 0 for an auction
        uint32_t entry   = 0;       // the item
        uint64_t guid    = 0;       // the player; an auction's owner
        uint64_t item    = 0;       // the item's guid
        uint64_t other   = 0;       // an auction's buyer
        int64_t  amount  = 0;       // copper, or how many
        int64_t  extra   = 0;       // an auction's deposit, or the house's cut
    };

    std::mutex        g_noteMutex;
    std::vector<Note> g_notes;          // under g_noteMutex
    uint64_t          g_noted   = 0;    // under g_noteMutex
    uint64_t          g_dropped = 0;    // under g_noteMutex
    constexpr size_t  kMaxNotes = 200000;

    // Set by the world tick: the module and the ledger on, and the world
    // running (the auctions loaded at startup are not new ones). All the
    // hooks read.
    std::atomic<bool> g_on{ false };

    void Push(Note const& note)
    {
        std::lock_guard<std::mutex> lock(g_noteMutex);
        ++g_noted;
        if (g_notes.size() < kMaxNotes)
            g_notes.push_back(note);
        else
            ++g_dropped;
    }

    // A playerbots bot's session: a random bot's or someone's own, told apart
    // on the world tick by the account.
    bool BotSession(Player* player, uint32_t& account)
    {
        if (!g_on.load(std::memory_order_relaxed) || !player)
            return false;
        WorldSession* session = player->GetSession();
        if (!session || !session->IsHeadless())
            return false;
        account = session->GetAccountId();
        return true;
    }

    // For an item sure to be there: one being put on, or used to cast.
    void NoteItem(NoteKind kind, Player* player, Item* item, int64_t count, bool node = false)
    {
        uint32_t account = 0;
        if (!item || !BotSession(player, account))
            return;
        Note n;
        n.kind    = kind;
        n.account = account;
        n.guid    = player->GetGUID().GetRawValue();
        n.item    = item->GetGUID().GetRawValue();
        n.entry   = item->GetEntry();
        n.amount  = count;
        n.node    = node;
        Push(n);
    }

    // The item a "something got" hook is about, as StoreNewItem stored it, read
    // in OnPlayerStoreNewItem while it is sure to be there. The loot, roll,
    // quest-reward, crafting and vendor hooks come straight after on the same
    // thread, and must not follow their Item*: a module whose hook runs before
    // ours may have destroyed it (mod-junk-to-gold sells and destroys grey
    // loot in OnPlayerLootItem), and a new item destroyed is deleted on the
    // spot. They only compare its address with this one's.
    struct Stored
    {
        Player const* player = nullptr;
        Item const*   item   = nullptr;   // compared, never followed
        uint64_t      guid   = 0;
        uint32_t      entry  = 0;
    };
    thread_local Stored t_stored;

    // Whether `item` is the one just stored for `player` on this thread; the
    // record is used up either way.
    bool TakeStored(Player* player, Item* item, Stored& out)
    {
        const bool match = item && t_stored.item == item && t_stored.player == player;
        if (match)
            out = t_stored;
        t_stored = {};
        return match;
    }

    void NoteStored(NoteKind kind, Player* player, Stored const& stored, int64_t count, bool node = false)
    {
        uint32_t account = 0;
        if (!stored.entry || !BotSession(player, account))
            return;
        Note n;
        n.kind    = kind;
        n.account = account;
        n.guid    = player->GetGUID().GetRawValue();
        n.item    = stored.guid;
        n.entry   = stored.entry;
        n.amount  = count;
        n.node    = node;
        Push(n);
    }

    void NoteGold(NoteKind kind, Player* player, int64_t copper)
    {
        uint32_t account = 0;
        if (!copper || !BotSession(player, account))
            return;
        Note n;
        n.kind    = kind;
        n.account = account;
        n.guid    = player->GetGUID().GetRawValue();
        n.amount  = copper;
        Push(n);
    }

    void NoteAuction(NoteKind kind, AuctionEntry const* auction)
    {
        if (!auction || !g_on.load(std::memory_order_relaxed))
            return;
        Note n;
        n.kind  = kind;
        n.guid  = auction->owner.GetRawValue();
        n.other = auction->bidder.GetRawValue();
        n.item  = auction->item_guid.GetRawValue();
        n.entry = auction->item_template;
        switch (kind)
        {
            case NoteKind::AuctionAdded:
                n.amount = auction->buyout;
                n.extra  = auction->deposit;
                break;
            case NoteKind::AuctionSold:
                n.amount = auction->bid;
                n.extra  = auction->GetAuctionCut();
                break;
            default:
                break;
        }
        Push(n);
    }

    // ---- the world tick's own, from here on ----------------------------------------------

    enum class Who : uint8_t { Player, Bot, AuctionBot };
    enum class From : uint8_t { HandOut, Looted, Quest, Bought, Vendor, Made };

    const char* WhoKey(Who who)
    {
        switch (who)
        {
            case Who::Bot:        return "bot";
            case Who::AuctionBot: return "ahbot";
            default:              return "player";
        }
    }

    const char* FromKey(From from)
    {
        switch (from)
        {
            case From::Looted: return "looted";
            case From::Quest:  return "quest";
            case From::Bought: return "bought";
            case From::Vendor: return "vendor";
            case From::Made:   return "made";
            default:           return "handout";
        }
    }

    struct Origin
    {
        From              from = From::HandOut;
        Clock::time_point when{};
    };

    constexpr size_t kKeepAccounts = 100000;
    constexpr size_t kKeepOwners   = 50000;
    constexpr size_t kKeepOrigins  = 200000;
    constexpr size_t kKeepCounted  = 400000;
    constexpr auto   kOriginAge    = std::chrono::hours(6);   // gear got and not put on by then: never counted
    constexpr size_t kLookPerTick  = 200;                     // bots looked at a tick, during a look

    std::unordered_map<uint32_t, bool>    g_randomAccount;   // account -> a random bot's
    std::unordered_map<uint64_t, Who>     g_who;             // character -> who, for auctions
    std::unordered_map<uint64_t, Origin>  g_origin;          // a piece of gear's guid -> where it came from
    std::unordered_set<uint64_t>          g_counted;         // gear already counted as put on
    std::unordered_map<uint64_t, int64_t> g_paid;            // bot -> what the game paid it since the last look
    std::unordered_map<uint64_t, int64_t> g_had;             // bot -> its gold at the last look
    std::map<std::string, int64_t>        g_pending;         // figures not written yet, for g_day
    std::map<std::string, int64_t>        g_now;             // the latest look at the bots
    bool                                  g_nowWritten = true;
    std::string                           g_day;             // the day g_pending is for
    uint32_t                              g_flushTimer = 0;
    PbpLedgerStats                        g_stats;

    struct Look
    {
        bool                           running = false;
        uint32_t                       timer   = 0;
        std::time_t                    started = 0;
        std::vector<uint64_t>          queue;
        size_t                         at = 0;
        std::map<std::string, int64_t> values;
    };
    Look g_look;

    PbpLedgerSettings const& Cfg() { return g_Pbp.ledger; }

    std::time_t Now() { return std::time_t(GameTime::GetGameTime().count()); }

    void Add(std::string const& key, int64_t value)
    {
        if (value)
            g_pending[key] += value;
    }

    bool RandomAccount(uint32_t account)
    {
        if (!account)
            return false;
        auto it = g_randomAccount.find(account);
        if (it != g_randomAccount.end())
            return it->second;
        if (g_randomAccount.size() >= kKeepAccounts)
            g_randomAccount.clear();
        const bool random = sPlayerbotAIConfig.IsInRandomAccountList(account);
        g_randomAccount.emplace(account, random);
        return random;
    }

    Who WhoIs(ObjectGuid guid)
    {
        if (!guid)
            return Who::Player;
        auto it = g_who.find(guid.GetRawValue());
        if (it != g_who.end())
            return it->second;
        Who who = Who::Player;
        if (Cfg().auctionBots.contains(guid.GetCounter()))
            who = Who::AuctionBot;
        else if (RandomAccount(sCharacterCache->GetCharacterAccountIdByGuid(guid)))
            who = Who::Bot;
        if (g_who.size() >= kKeepOwners)
            g_who.clear();
        g_who.emplace(guid.GetRawValue(), who);
        return who;
    }

    void NoteOrigin(uint64_t item, From from)
    {
        if (g_origin.size() >= kKeepOrigins)
            g_origin.clear();
        g_origin[item] = { from, Clock::now() };
    }

    void PutOn(Note const& n)
    {
        if (!g_counted.insert(n.item).second)
            return;   // counted the first time it went on
        if (g_counted.size() > kKeepCounted)
        {
            g_counted.clear();
            g_counted.insert(n.item);
        }
        From from = From::HandOut;   // nothing seen of it before: playerbots made it
        auto it = g_origin.find(n.item);
        if (it != g_origin.end())
        {
            from = it->second.from;
            g_origin.erase(it);
        }
        const std::string key = std::string("gear.") + FromKey(from);
        Add(key, 1);
        if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(n.entry))
            Add(key + ".ilvl", proto->ItemLevel);
    }

    void Auction(Note const& n)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(n.entry);
        const Who owner = WhoIs(ObjectGuid(n.guid));
        switch (n.kind)
        {
            case NoteKind::AuctionAdded:
                if (owner != Who::Bot)
                    return;
                Add("ah.listed", 1);
                Add("ah.listed." + PbpLedger::AuctionKind(proto ? proto->Class : MAX_ITEM_CLASS), 1);
                Add("ah.listed.value", n.amount);
                Add("ah.deposits", n.extra);
                return;
            case NoteKind::AuctionExpired:
                if (owner == Who::Bot)
                    Add("ah.expired", 1);
                return;
            case NoteKind::AuctionSold:
            {
                const Who buyer = WhoIs(ObjectGuid(n.other));
                if (owner == Who::Bot)
                {
                    Add("ah.sold", 1);
                    Add("ah.sold.value", n.amount);
                    Add("ah.sold.cut", n.extra);
                    Add(std::string("ah.sold.") + WhoKey(buyer), 1);
                    Add(std::string("ah.sold.") + WhoKey(buyer) + ".value", n.amount);
                }
                if (buyer == Who::Bot)
                {
                    Add("ah.bought", 1);
                    Add("ah.bought.value", n.amount);
                    Add(std::string("ah.bought.") + WhoKey(owner), 1);
                    Add(std::string("ah.bought.") + WhoKey(owner) + ".value", n.amount);
                    if (proto && PbpLedger::IsGear(proto->Class))
                        NoteOrigin(n.item, From::Bought);
                }
                return;
            }
            default:
                return;
        }
    }

    void Process(Note const& n)
    {
        switch (n.kind)
        {
            case NoteKind::AuctionAdded:
            case NoteKind::AuctionSold:
            case NoteKind::AuctionExpired:
                Auction(n);
                return;
            default:
                break;
        }
        if (!RandomAccount(n.account))
            return;   // someone's own bots are not the random bots' economy

        switch (n.kind)
        {
            case NoteKind::Money:
                Add(n.amount > 0 ? "gold.in" : "gold.out", std::llabs(n.amount));
                g_paid[n.guid] += n.amount;
                return;
            case NoteKind::LootMoney:
                Add("gold.loot", n.amount);
                return;
            case NoteKind::QuestMoney:
                Add("gold.quests", n.amount);
                return;
            case NoteKind::Equip:
                PutOn(n);
                return;
            default:
                break;
        }

        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(n.entry);
        if (!proto)
            return;
        if (n.kind == NoteKind::Used)
        {
            const std::string kind = PbpLedger::UseKind(proto->Class, proto->SubClass);
            if (!kind.empty())
                Add("used." + kind, 1);
            return;
        }

        const std::string kind = PbpLedger::ThingKind(proto->Class, proto->SubClass, proto->Quality);
        const bool gear = PbpLedger::IsGear(proto->Class);
        switch (n.kind)
        {
            case NoteKind::Looted:
                Add("loot." + kind, n.amount);
                if (n.node)
                    Add("loot.nodes", n.amount);
                if (gear && n.item)
                    NoteOrigin(n.item, From::Looted);
                return;
            case NoteKind::QuestItem:
                if (gear && n.item)
                    NoteOrigin(n.item, From::Quest);
                return;
            case NoteKind::Made:
                Add("made." + kind, n.amount);
                if (gear && n.item)
                    NoteOrigin(n.item, From::Made);
                return;
            case NoteKind::Vendor:
                Add("vendor." + kind, n.amount);
                if (gear && n.item)
                    NoteOrigin(n.item, From::Vendor);
                return;
            default:
                return;
        }
    }

    void TakeNotes()
    {
        std::vector<Note> notes;
        {
            std::lock_guard<std::mutex> lock(g_noteMutex);
            notes.swap(g_notes);
            g_stats.notes   = g_noted;
            g_stats.dropped = g_dropped;
        }
        for (Note const& n : notes)
            Process(n);
    }

    // ---- a look at the bots ----------------------------------------------------------

    template <typename F>
    void ForEachBagItem(Player* bot, F&& fn)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                fn(item);
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = bot->GetBagByPos(bagSlot))
                for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                    if (Item* item = bag->GetItemByPos(uint8(i)))
                        fn(item);
    }

    uint32_t BankItems(Player* bot)
    {
        uint32_t count = 0;
        for (uint8 slot = BANK_SLOT_ITEM_START; slot < BANK_SLOT_ITEM_END; ++slot)
            if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                ++count;
        for (uint8 bagSlot = BANK_SLOT_BAG_START; bagSlot < BANK_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = bot->GetBagByPos(bagSlot))
                for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                    if (bag->GetItemByPos(uint8(i)))
                        ++count;
        return count;
    }

    void LookAt(Player* bot, std::map<std::string, int64_t>& v)
    {
        const uint64_t raw = bot->GetGUID().GetRawValue();
        const std::string b = std::to_string(PbpLedger::Band(bot->GetLevel()));
        const int64_t money = bot->GetMoney();

        // What it has, against what the game paid it since the last look: the
        // rest is playerbots' own doing (SetMoney, never ModifyMoney).
        int64_t paid = 0;
        if (auto it = g_paid.find(raw); it != g_paid.end())
        {
            paid = it->second;
            g_paid.erase(it);
        }
        if (auto had = g_had.find(raw); had != g_had.end())
        {
            const int64_t rest = money - had->second - paid;
            if (rest > 0)
                Add("gold.free", rest);
            else if (rest < 0)
                Add("gold.gone", -rest);
        }
        g_had[raw] = money;

        v["now.bots"] += 1;
        v["now.bots.b" + b] += 1;
        v["now.gold"] += money;
        v["now.gold.b" + b] += money;

        // The item level of what it has on, its shirt and tabard aside.
        int64_t levels = 0, pieces = 0;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            if (slot == EQUIPMENT_SLOT_BODY || slot == EQUIPMENT_SLOT_TABARD)
                continue;
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            {
                levels += item->GetTemplate()->ItemLevel;
                ++pieces;
            }
        }
        const int64_t ilvl = pieces ? levels / pieces : 0;
        if (pieces)
        {
            v["now.ilvl.b" + b] += ilvl;
            v["now.geared.b" + b] += 1;
        }

        bool gathering = false, crafting = false;
        for (PbpLedger::Profession const& p : PbpLedger::kProfessions)
        {
            if (!bot->HasSkill(p.skill))
                continue;
            v[std::string("now.prof.") + p.name] += 1;
            (p.gathering ? gathering : crafting) = true;
        }
        if (gathering)
            v["now.prof.gatherers"] += 1;
        if (crafting)
            v["now.prof.crafters"] += 1;
        if (!gathering && !crafting)
            v["now.prof.none"] += 1;

        if (sLFGMgr->GetState(bot->GetGUID()) == lfg::LFG_STATE_QUEUED)
        {
            v["now.lfg"] += 1;
            if (pieces)
            {
                v["now.lfg.ilvl"] += ilvl;
                v["now.lfg.geared"] += 1;
            }
        }

        // What it carries that someone could make something of, and what a
        // vendor would give for it; and what is in its bank, which playerbots'
        // refresh never empties.
        ForEachBagItem(bot, [&v](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto || (proto->Class != ITEM_CLASS_TRADE_GOODS && proto->Class != ITEM_CLASS_GEM))
                return;
            v["now.carried.goods"] += item->GetCount();
            v["now.carried.value"] += int64_t(proto->SellPrice) * item->GetCount();
        });
        v["now.bank.items"] += BankItems(bot);
    }

    void LookAtAuctions(std::map<std::string, int64_t>& v)
    {
        for (AuctionHouseId id : { AuctionHouseId::Alliance, AuctionHouseId::Horde, AuctionHouseId::Neutral })
        {
            AuctionHouseObject* house = sAuctionMgr->GetAuctionsMapByHouseId(id);
            if (!house)
                continue;
            for (auto const& [auctionId, auction] : house->GetAuctions())
            {
                if (!auction)
                    continue;
                const std::string key = std::string("now.ah.") + WhoKey(WhoIs(auction->owner));
                v[key] += 1;
                v[key + ".value"] += auction->buyout;
            }
        }
    }

    // A slice of the look a tick, so 3,000 bots never hold up one.
    void Looking(uint32_t diff)
    {
        if (!g_look.running)
        {
            g_look.timer += diff;
            if (g_look.timer < Cfg().censusMinutes * MINUTE * IN_MILLISECONDS)
                return;
            g_look.timer = 0;
            g_look.queue.clear();
            g_look.at = 0;
            for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
                if (it->second)
                    g_look.queue.push_back(it->second->GetGUID().GetRawValue());
            if (g_look.queue.empty())
                return;
            g_look.values.clear();
            for (std::string const& key : PbpLedger::NowKeys())
                g_look.values[key] = 0;
            g_look.started = Now();
            g_look.running = true;
        }

        const size_t end = std::min(g_look.queue.size(), g_look.at + kLookPerTick);
        for (; g_look.at < end; ++g_look.at)
        {
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(g_look.queue[g_look.at]));
            if (bot && bot->IsInWorld())
                LookAt(bot, g_look.values);
        }
        if (g_look.at < g_look.queue.size())
            return;

        g_look.running = false;
        g_look.queue.clear();
        if (!g_look.values["now.bots"])
            return;   // all gone again before they were looked at
        LookAtAuctions(g_look.values);
        g_look.values["now.time"] = g_look.started;
        g_now        = std::move(g_look.values);
        g_look.values = {};
        g_nowWritten = false;
        g_stats.looked   = uint32_t(g_now["now.bots"]);
        g_stats.lookedAt = PbpLedger::WhenText(g_look.started);
    }

    // ---- the database ----------------------------------------------------------------

    void Write(bool now)
    {
        const bool flows = !g_pending.empty() && !g_day.empty();
        const bool look = !g_nowWritten && !g_now.empty();
        if (!flows && !look)
            return;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        if (flows)
            for (auto const& [name, value] : g_pending)
                trans->Append("INSERT INTO playerbots_plus_ledger (day, name, value) VALUES ('{}', '{}', {}) "
                              "ON DUPLICATE KEY UPDATE value = value + {}",
                              g_day, name, value, value);
        if (look)
        {
            const std::string day = PbpLedger::DayOf(std::time_t(g_now["now.time"]));
            for (auto const& [name, value] : g_now)
                trans->Append("INSERT INTO playerbots_plus_ledger (day, name, value) VALUES ('{}', '{}', {}) "
                              "ON DUPLICATE KEY UPDATE value = {}",
                              day, name, value, value);
        }
        if (now)
            CharacterDatabase.DirectCommitTransaction(trans);
        else
            CharacterDatabase.CommitTransaction(trans);

        g_pending.clear();
        g_nowWritten = true;
        ++g_stats.writes;
    }

    // The figures for the days from..to, summed, the latest look among them,
    // and how many of the days have figures. With `memory`, what is not
    // written yet too.
    void Gather(std::string const& from, std::string const& to, bool memory, std::map<std::string, int64_t>& flows,
                std::map<std::string, int64_t>& now, uint32_t& withData)
    {
        std::set<std::string> days;
        std::map<std::string, std::map<std::string, int64_t>> looks;
        if (QueryResult r = CharacterDatabase.Query("SELECT CAST(day AS CHAR), name, value FROM playerbots_plus_ledger "
                                                    "WHERE day BETWEEN '{}' AND '{}'", from, to))
        {
            do
            {
                Field* f = r->Fetch();
                const std::string day  = f[0].Get<std::string>();
                const std::string name = f[1].Get<std::string>();
                const int64_t value    = f[2].Get<int64>();
                if (name.rfind("now.", 0) == 0)
                    looks[day][name] = value;
                else
                {
                    flows[name] += value;
                    days.insert(day);
                }
            } while (r->NextRow());
        }
        if (!looks.empty())
            now = looks.rbegin()->second;
        if (memory)
        {
            if (!g_pending.empty())
            {
                for (auto const& [name, value] : g_pending)
                    flows[name] += value;
                days.insert(g_day);
            }
            if (!g_now.empty())
                now = g_now;
        }
        withData = uint32_t(days.size());
    }

    void LogDay(std::string const& day)
    {
        std::map<std::string, int64_t> flows, now;
        uint32_t withData = 0;
        Gather(day, day, false, flows, now, withData);
        if (flows.empty())
            return;
        for (std::string const& line :
             PbpLedger::Report("The random bots' economy on " + day + ":", flows, now, withData))
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {}", line);
    }
}

// ---- the hooks ------------------------------------------------------------------------

PbpLedgerPlayerScript::PbpLedgerPlayerScript()
    : PlayerScript("PbpLedgerPlayerScript",
                   { PLAYERHOOK_ON_STORE_NEW_ITEM, PLAYERHOOK_ON_MONEY_CHANGED, PLAYERHOOK_ON_BEFORE_LOOT_MONEY,
                     PLAYERHOOK_ON_PLAYER_COMPLETE_QUEST, PLAYERHOOK_ON_LOOT_ITEM, PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM,
                     PLAYERHOOK_ON_QUEST_REWARD_ITEM, PLAYERHOOK_ON_CREATE_ITEM,
                     PLAYERHOOK_ON_AFTER_STORE_OR_EQUIP_NEW_ITEM, PLAYERHOOK_ON_EQUIP, PLAYERHOOK_ON_SPELL_CAST })
{
}

void PbpLedgerPlayerScript::OnPlayerStoreNewItem(Player* player, Item* item, uint32 /*count*/)
{
    // Inside StoreNewItem: the item is there. Read what the hook that follows
    // will need (see Stored).
    t_stored = {};
    if (!item || !player || !g_on.load(std::memory_order_relaxed))
        return;
    t_stored.player = player;
    t_stored.item   = item;
    t_stored.guid   = item->GetGUID().GetRawValue();
    t_stored.entry  = item->GetEntry();
}

void PbpLedgerPlayerScript::OnPlayerMoneyChanged(Player* player, int32& amount)
{
    NoteGold(NoteKind::Money, player, amount);
}

void PbpLedgerPlayerScript::OnPlayerBeforeLootMoney(Player* player, Loot* loot)
{
    if (loot)
        NoteGold(NoteKind::LootMoney, player, loot->gold);
}

void PbpLedgerPlayerScript::OnPlayerCompleteQuest(Player* player, Quest const* quest)
{
    if (!quest || !player)
        return;
    // What RewardQuest has just paid: the reward (more at the top level, in
    // place of the experience), never what a quest asks for.
    int64_t copper = std::max<int32>(0, quest->GetRewOrReqMoney(player->GetLevel()));
    if (player->GetLevel() >= sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        copper += quest->GetRewMoneyMaxLevel();
    NoteGold(NoteKind::QuestMoney, player, copper);
}

// The loot, roll, quest-reward and crafting hooks never follow `item`: it may
// be gone already (see Stored).
void PbpLedgerPlayerScript::OnPlayerLootItem(Player* player, Item* item, uint32 count, ObjectGuid lootguid)
{
    Stored stored;
    if (TakeStored(player, item, stored))
        NoteStored(NoteKind::Looted, player, stored, count, lootguid.IsGameObject());
}

void PbpLedgerPlayerScript::OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 count,
                                                        RollVote /*voteType*/, Roll* /*roll*/)
{
    Stored stored;
    if (TakeStored(player, item, stored))
        NoteStored(NoteKind::Looted, player, stored, count);
}

void PbpLedgerPlayerScript::OnPlayerQuestRewardItem(Player* player, Item* item, uint32 count)
{
    Stored stored;
    if (TakeStored(player, item, stored))
        NoteStored(NoteKind::QuestItem, player, stored, count);
}

void PbpLedgerPlayerScript::OnPlayerCreateItem(Player* player, Item* item, uint32 count)
{
    Stored stored;
    if (TakeStored(player, item, stored))
        NoteStored(NoteKind::Made, player, stored, count);
}

void PbpLedgerPlayerScript::OnPlayerAfterStoreOrEquipNewItem(Player* player, uint32 /*vendorslot*/, Item* item,
                                                             uint8 count, uint8 /*bag*/, uint8 /*slot*/,
                                                             ItemTemplate const* pProto, Creature* /*pVendor*/,
                                                             VendorItem const* /*crItem*/, bool bStore)
{
    // Bought into the bags, StoreNewItem stored it just before. Put straight
    // on, it never went through there: the template the game hands us says
    // what it was.
    Stored stored;
    if (!bStore || !TakeStored(player, item, stored))
    {
        t_stored = {};
        stored   = {};
        if (pProto)
            stored.entry = pProto->ItemId;
    }
    NoteStored(NoteKind::Vendor, player, stored, count);
}

void PbpLedgerPlayerScript::OnPlayerEquip(Player* player, Item* it, uint8 /*bag*/, uint8 /*slot*/, bool /*update*/)
{
    // Not while a character loads: everything it has on is put on again then.
    if (!player || !it || !player->IsInWorld())
        return;
    ItemTemplate const* proto = it->GetTemplate();
    if (proto && PbpLedger::IsGear(proto->Class))
        NoteItem(NoteKind::Equip, player, it, 1);
}

void PbpLedgerPlayerScript::OnPlayerSpellCast(Player* player, Spell* spell, bool /*skipCheck*/)
{
    Item* item = spell ? spell->m_CastItem : nullptr;
    if (!item)
        return;
    ItemTemplate const* proto = item->GetTemplate();
    if (proto && proto->Class == ITEM_CLASS_CONSUMABLE)
        NoteItem(NoteKind::Used, player, item, 1);
}

void PbpLedger_OnAuctionAdded(AuctionEntry const* auction)
{
    NoteAuction(NoteKind::AuctionAdded, auction);
}

void PbpLedger_OnSaleMailed(AuctionEntry const* auction)
{
    NoteAuction(NoteKind::AuctionSold, auction);
}

void PbpLedger_OnExpiryMailed(AuctionEntry const* auction)
{
    NoteAuction(NoteKind::AuctionExpired, auction);
}

// ---- the world thread -------------------------------------------------------------------

void PbpLedger_Load(bool reload)
{
    if (reload)
        return;
    // The SQL in data/sql makes it too; this is for a server whose updater is off.
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `playerbots_plus_ledger` ("
        "`day` DATE NOT NULL, "
        "`name` VARCHAR(64) NOT NULL, "
        "`value` BIGINT NOT NULL DEFAULT 0, "
        "PRIMARY KEY (`day`, `name`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-playerbots-plus: the random bots economy, by day'");
    const std::time_t keepFrom = Now() - std::time_t(Cfg().keepDays) * DAY;
    CharacterDatabase.Execute("DELETE FROM playerbots_plus_ledger WHERE day < '{}'", PbpLedger::DayOf(keepFrom));
}

void PbpLedger_Update(uint32_t diff)
{
    const bool on = g_Pbp.enable && Cfg().enable;
    g_on.store(on, std::memory_order_relaxed);
    if (!on)
    {
        std::lock_guard<std::mutex> lock(g_noteMutex);
        g_notes.clear();
        return;
    }

    // A new day: yesterday's last figures written, and logged.
    const std::string day = PbpLedger::DayOf(Now());
    if (g_day.empty())
        g_day = day;
    else if (day != g_day)
    {
        TakeNotes();
        Write(true);
        LogDay(g_day);
        g_day = day;
    }

    TakeNotes();
    Looking(diff);

    g_flushTimer += diff;
    if (g_flushTimer >= Cfg().flushMinutes * MINUTE * IN_MILLISECONDS)
    {
        g_flushTimer = 0;
        Write(false);
        const auto stale = Clock::now() - kOriginAge;
        for (auto it = g_origin.begin(); it != g_origin.end();)
            it = it->second.when < stale ? g_origin.erase(it) : std::next(it);
    }
}

void PbpLedger_Shutdown()
{
    if (!g_on.load(std::memory_order_relaxed))
        return;
    g_on.store(false, std::memory_order_relaxed);
    TakeNotes();
    Write(true);
}

void PbpLedger_MailMoney(uint64_t copper)
{
    if (g_on.load(std::memory_order_relaxed))
        Add("gold.mail", int64_t(copper));
}

std::vector<std::string> PbpLedger_Report(uint32_t days)
{
    if (!g_Pbp.enable || !Cfg().enable)
        return { "The economy ledger is off (PlayerbotsPlus.Ledger.Enable)." };
    days = std::clamp<uint32_t>(days, 1, Cfg().keepDays);
    const std::time_t now = Now();
    const std::string from = PbpLedger::DayOf(now - std::time_t(days - 1) * DAY);
    const std::string to   = PbpLedger::DayOf(now);

    std::map<std::string, int64_t> flows, look;
    uint32_t withData = 0;
    Gather(from, to, true, flows, look, withData);
    const std::string title = "The random bots' economy over the last " + std::to_string(days) + " day" +
                              (days == 1 ? "" : "s") + " (" + std::to_string(withData) + " with figures, today so " +
                              "far included):";
    return PbpLedger::Report(title, flows, look, withData);
}

PbpLedgerStats PbpLedger_GetStats()
{
    PbpLedgerStats s = g_stats;
    std::lock_guard<std::mutex> lock(g_noteMutex);
    s.notes   = g_noted;
    s.dropped = g_dropped;
    return s;
}
