#include "PlayerbotsPlusMarket.h"
#include "PlayerbotsPlus.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusMarketPrice.h"

#include "AuctionHouseMgr.h"
#include "Bag.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "Group.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Mail.h"
#include "MailMgr.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "QuestDef.h"
#include "ScriptMgr.h"
#include "SharedDefines.h"
#include "World.h"
#include "WorldSession.h"

#include "AiObjectContext.h"
#include "Event.h"
#include "ItemUsageValue.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"

#include <fmt/core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr auto     kIndexFresh  = std::chrono::seconds(60);   // a house looked through again after this
    constexpr auto     kForgetAfter = std::chrono::hours(24);     // a bot not seen in a city for this long
    constexpr uint32_t kPruneMs     = 10 * MINUTE * IN_MILLISECONDS;
    constexpr size_t   kOwnersKept  = 50000;                      // owner -> is-a-bot answers remembered

    struct TraderState
    {
        bool                                  scheduled = false;
        Clock::time_point                     nextVisit{};
        Clock::time_point                     lastSeen{};
        std::unordered_map<uint32_t, uint8_t> expiries;   // item entry -> times its auction came back unsold
    };

    // The cheapest of one thing on offer.
    struct PriceRef
    {
        uint64_t minUnit  = 0;
        uint64_t minOwner = 0;   // raw guid of who asks it
        uint32_t listings = 0;
    };

    struct HouseIndex
    {
        bool                                   valid = false;
        Clock::time_point                      built{};
        std::unordered_map<uint64_t, PriceRef> byItem;     // (entry << 32) | random property
        std::unordered_map<uint32_t, PriceRef> byEntry;
        std::unordered_map<uint64_t, uint32_t> perOwner;   // raw guid -> auctions up
        uint32_t                               botAuctions = 0;
        uint32_t                               total       = 0;
    };

    // What one visit did, for the log and .pbp market visit.
    struct VisitReport
    {
        uint32_t                 mails  = 0;
        uint64_t                 money  = 0;
        uint32_t                 items  = 0;
        std::vector<std::string> listed;
        std::vector<std::string> bought;
        std::vector<std::string> notes;
        uint32_t                 checks = 0;
    };

    std::unordered_map<uint64_t, TraderState>   g_bots;     // by raw guid
    std::array<HouseIndex, 3>                   g_houses;   // alliance, horde, neutral
    std::unordered_map<uint64_t, bool>          g_ownerIsBot;
    std::vector<PlayerbotsPlus::MarketListener> g_listeners;
    PbpMarketStats                                 g_stats;
    uint32_t                                    g_timer      = 0;
    uint32_t                                    g_pruneTimer = 0;
    std::mt19937                                g_rng{ std::random_device{}() };

    PbpMarketSettings const& Cfg() { return g_Pbp.market; }

    double Roll()
    {
        return std::uniform_real_distribution<double>(0.0, 1.0)(g_rng);
    }

    uint32_t Between(uint32_t lo, uint32_t hi)
    {
        if (hi <= lo)
            return lo;
        return std::uniform_int_distribution<uint32_t>(lo, hi)(g_rng);
    }

    PlayerbotAI* AiOf(Player* player)
    {
        return player ? PlayerbotsMgr::instance().GetPlayerbotAI(player) : nullptr;
    }

    std::string ItemName(uint32_t entry)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(entry);
        return proto ? "[" + proto->Name1 + "]" : fmt::format("item {}", entry);
    }

    std::string StackText(uint32_t entry, uint32_t count)
    {
        return count > 1 ? fmt::format("{} x{}", ItemName(entry), count) : ItemName(entry);
    }

    void Emit(PlayerbotsPlus::MarketEvent const& event)
    {
        for (PlayerbotsPlus::MarketListener const& listener : g_listeners)
            if (listener)
                listener(event);
    }

    bool IsRandomBotAccount(uint32_t account)
    {
        return account && sPlayerbotAIConfig.IsInRandomAccountList(account);
    }

    bool OwnerIsBot(ObjectGuid owner)
    {
        auto it = g_ownerIsBot.find(owner.GetRawValue());
        if (it != g_ownerIsBot.end())
            return it->second;
        if (g_ownerIsBot.size() >= kOwnersKept)
            g_ownerIsBot.clear();
        const bool bot = IsRandomBotAccount(sCharacterCache->GetCharacterAccountIdByGuid(owner));
        g_ownerIsBot.emplace(owner.GetRawValue(), bot);
        return bot;
    }

    AuctionHouseId HouseOf(Player* bot)
    {
        if (sWorld->getBoolConfig(CONFIG_ALLOW_TWO_SIDE_INTERACTION_AUCTION))
            return AuctionHouseId::Neutral;
        return bot->GetTeamId() == TEAM_ALLIANCE ? AuctionHouseId::Alliance : AuctionHouseId::Horde;
    }

    size_t SlotOf(AuctionHouseId id)
    {
        switch (id)
        {
            case AuctionHouseId::Alliance: return 0;
            case AuctionHouseId::Horde:    return 1;
            default:                       return 2;
        }
    }

    uint64_t Key(uint32_t entry, int32_t suffix)
    {
        return (uint64_t(entry) << 32) | uint32_t(suffix);
    }

    int32_t SuffixOf(AuctionEntry const* auction)
    {
        Item* item = sAuctionMgr->GetAItem(auction->item_guid);
        return item ? item->GetItemRandomPropertyId() : 0;
    }

    void NotePrice(PriceRef& ref, uint64_t unit, uint64_t owner)
    {
        ++ref.listings;
        if (!ref.minUnit || unit < ref.minUnit)
        {
            ref.minUnit  = unit;
            ref.minOwner = owner;
        }
    }

    HouseIndex& IndexOf(AuctionHouseId id)
    {
        HouseIndex& idx = g_houses[SlotOf(id)];
        const auto now = Clock::now();
        if (idx.valid && now - idx.built < kIndexFresh)
            return idx;

        idx = HouseIndex{};
        idx.valid = true;
        idx.built = now;

        AuctionHouseObject* house = sAuctionMgr->GetAuctionsMapByHouseId(id);
        if (!house)
            return idx;

        for (auto const& [auctionId, auction] : house->GetAuctions())
        {
            if (!auction)
                continue;
            ++idx.total;
            ++idx.perOwner[auction->owner.GetRawValue()];
            if (OwnerIsBot(auction->owner))
                ++idx.botAuctions;
            if (!auction->buyout || !auction->itemCount)
                continue;
            const uint64_t unit = auction->buyout / auction->itemCount;
            NotePrice(idx.byItem[Key(auction->item_template, SuffixOf(auction))], unit, auction->owner.GetRawValue());
            NotePrice(idx.byEntry[auction->item_template], unit, auction->owner.GetRawValue());
        }

        uint32_t bots = 0, all = 0;
        for (HouseIndex const& h : g_houses)
        {
            bots += h.botAuctions;
            all  += h.total;
        }
        g_stats.botAuctions = bots;
        g_stats.allAuctions = all;
        return idx;
    }

    PriceRef Lookup(HouseIndex const& idx, uint32_t entry, int32_t suffix)
    {
        auto it = idx.byItem.find(Key(entry, suffix));
        if (it != idx.byItem.end())
            return it->second;
        auto e = idx.byEntry.find(entry);
        return e != idx.byEntry.end() ? e->second : PriceRef{};
    }

    PbpPrice::ItemFacts FactsOf(ItemTemplate const* proto)
    {
        PbpPrice::ItemFacts f;
        f.vendor        = proto->SellPrice;
        f.quality       = proto->Quality;
        f.itemLevel     = proto->ItemLevel;
        f.requiredLevel = proto->RequiredLevel;
        f.gear          = proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR;
        f.maxStack      = std::max<uint32_t>(proto->GetMaxStackSize(), 1);
        return f;
    }

    bool InTradingCity(Player* bot)
    {
        if (bot->HasRestFlag(REST_FLAG_IN_CITY))
            return true;
        auto const& zones = Cfg().extraZones;
        return !zones.empty() && (zones.count(bot->GetZoneId()) || zones.count(bot->GetAreaId()));
    }

    bool WithRealPlayer(Player* bot)
    {
        Group* group = bot->GetGroup();
        if (!group)
            return false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (member && member != bot)
            {
                PlayerbotAI* ai = AiOf(member);
                if (!ai || !ai->IsBotAI())
                    return true;
            }
        }
        return false;
    }

    // A random bot, on its own, free to go: nothing it is in the middle of.
    // `anywhere`: .pbp market visit, which does not need it in a city.
    bool CanTrade(Player* bot, PlayerbotAI*& ai, bool anywhere, std::string* why = nullptr)
    {
        auto no = [why](char const* reason)
        {
            if (why)
                *why = reason;
            return false;
        };

        if (!bot || !bot->IsInWorld())
            return no("is not in the world");
        if (!bot->IsAlive())
            return no("is dead");
        if (bot->IsInCombat())
            return no("is fighting");
        if (bot->IsBeingTeleported() || bot->IsInFlight())
            return no("is travelling");
        if (bot->InBattleground() || bot->InArena())
            return no("is in a battleground");
        if (bot->GetTradeData())
            return no("is trading with someone");
        Map* map = bot->GetMap();
        if (!map || map->Instanceable())
            return no("is in a dungeon");
        if (bot->GetLevel() < Cfg().minLevel)
            return no("is below Market.MinLevel");
        if (!anywhere && !InTradingCity(bot))
            return no("is not in a city");

        ai = AiOf(bot);
        if (!ai || !ai->IsBotAI())
            return no("is not a bot");
        if (ai->GetMaster())
            return no("has a master");
        if (!sRandomPlayerbotMgr.IsRandomBot(bot))
            return no("is not a random bot");
        if (WithRealPlayer(bot))
            return no("is grouped with a real player");
        return true;
    }

    template <typename F>
    void ForEachBagItem(Player* bot, F&& fn)
    {
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                fn(item);

        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
        {
            Bag* bag = bot->GetBagByPos(bagSlot);
            if (!bag)
                continue;
            for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                if (Item* item = bag->GetItemByPos(uint8(i)))
                    fn(item);
        }
    }

    bool NeededForQuest(Player* bot, uint32_t entry)
    {
        for (auto const& [questId, status] : bot->getQuestStatusMap())
        {
            if (status.Status != QUEST_STATUS_INCOMPLETE)
                continue;
            Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
            if (!quest)
                continue;
            for (uint8 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                if (quest->RequiredItemId[i] == entry)
                    return true;
            for (uint8 i = 0; i < QUEST_SOURCE_ITEM_IDS_COUNT; ++i)
                if (quest->ItemDrop[i] == entry)
                    return true;
        }
        return false;
    }

    // Playerbots' own judgement of an item for this bot: what it loots, keeps
    // and sells by. Worked out on a value of its own rather than the one in
    // the bot's context, which would keep one for every auction ever looked at.
    ItemUsage UsageOf(PlayerbotAI* ai, uint32_t entry, int32_t suffix)
    {
        if (!ai->GetAiObjectContext())
            return ITEM_USAGE_KEEP;
        ItemUsageValue value(ai);
        value.Qualify(suffix ? fmt::format("{},{}", entry, suffix) : std::to_string(entry));
        return value.Calculate();
    }

    // Could go on the auction house at all, before playerbots is asked.
    bool Listable(Player* bot, Item* item, ItemTemplate const* proto, TraderState const& state)
    {
        auto const& m = Cfg();
        if (!proto || proto->Quality < m.sellMinQuality || proto->Quality > m.sellMaxQuality)
            return false;
        if (!proto->SellPrice)
            return false;   // worthless to anyone
        if (proto->Bonding == BIND_WHEN_PICKED_UP || proto->Bonding == BIND_QUEST_ITEM)
            return false;
        if (proto->Class == ITEM_CLASS_QUEST || proto->Class == ITEM_CLASS_KEY || proto->StartQuest)
            return false;
        if (proto->HasFlag(ITEM_FLAG_CONJURED) || proto->HasFlag(ITEM_FLAG_HAS_LOOT) || proto->LockID)
            return false;
        if (item->IsSoulBound() || item->IsInTrade() || item->IsNotEmptyBag() ||
            item->GetUInt32Value(ITEM_FIELD_DURATION))
            return false;
        if (!item->CanBeTraded() || sAuctionMgr->GetAItem(item->GetGUID()))
            return false;

        auto it = state.expiries.find(proto->ItemId);
        if (it != state.expiries.end() && it->second > m.maxRelists)
            return false;   // nobody wants it: it goes to a vendor in the end

        return !NeededForQuest(bot, proto->ItemId);
    }

    uint32_t PickHours()
    {
        auto const& w = Cfg().durationWeights;
        const uint32_t total = w[0] + w[1] + w[2];
        if (!total)
            return 24;
        uint32_t roll = Between(1, total);
        if (roll <= w[0])
            return 12;
        roll -= w[0];
        return roll <= w[1] ? 24 : 48;
    }

    // The game's own CMSG_AUCTION_SELL_ITEM, for one whole stack, without the
    // auctioneer: deposit, the item moved off the bot, saved in one go.
    bool CreateAuction(Player* bot, Item* item, AuctionHouseId houseId, AuctionHouseEntry const* houseEntry,
                       AuctionHouseObject* house, uint32_t bid, uint32_t buyout, uint32_t seconds)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || sAuctionMgr->GetAItem(item->GetGUID()) || !item->CanBeTraded() || item->IsNotEmptyBag() ||
            proto->HasFlag(ITEM_FLAG_CONJURED) || item->GetUInt32Value(ITEM_FIELD_DURATION))
            return false;
        if (!bid || !buyout || bid > buyout || buyout > MAX_MONEY_AMOUNT)
            return false;

        const uint32_t count   = item->GetCount();
        const uint32_t deposit = AuctionHouseMgr::GetAuctionDeposit(houseEntry, seconds, item, count);
        if (!bot->HasEnoughMoney(deposit))
            return false;

        bot->ModifyMoney(-int32(deposit));

        AuctionEntry* auction      = new AuctionEntry;
        auction->Id                = sObjectMgr->GenerateAuctionID();
        auction->houseId           = houseId;
        auction->item_guid         = item->GetGUID();
        auction->item_template     = item->GetEntry();
        auction->itemCount         = count;
        auction->owner             = bot->GetGUID();
        auction->startbid          = bid;
        auction->bidder            = ObjectGuid::Empty;
        auction->bid               = 0;
        auction->buyout            = buyout;
        auction->expire_time       = GameTime::GetGameTime().count() +
                                     uint32(seconds * sWorld->getRate(RATE_AUCTION_TIME));
        auction->deposit           = deposit;
        auction->auctionHouseEntry = houseEntry;

        sAuctionMgr->AddAItem(item);
        house->AddAuction(auction);

        bot->MoveItemFromInventory(item->GetBagSlot(), item->GetSlot(), true);

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        item->DeleteFromInventoryDB(trans);
        item->SaveToDB(trans);
        auction->SaveToDB(trans);
        bot->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);

        bot->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_CREATE_AUCTION, 1);
        return true;
    }

    // The buyout half of the game's CMSG_AUCTION_PLACE_BID.
    bool BuyOut(Player* bot, AuctionHouseObject* house, AuctionEntry* auction)
    {
        if (!auction->buyout || auction->owner == bot->GetGUID())
            return false;
        if (!sScriptMgr->OnPlayerCanPlaceAuctionBid(bot, auction))
            return false;
        // Not from another character on its own account.
        if (sCharacterCache->GetCharacterAccountIdByGuid(auction->owner) == bot->GetSession()->GetAccountId())
            return false;

        const uint32_t price = auction->buyout;
        if (price <= auction->bid || !bot->HasEnoughMoney(price))
            return false;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

        if (auction->bidder == bot->GetGUID())
            bot->ModifyMoney(-int32(price - auction->bid));
        else
        {
            bot->ModifyMoney(-int32(price));
            if (auction->bidder)
                sAuctionMgr->SendAuctionOutbiddedMail(auction, price, bot, trans);
        }
        auction->bidder = bot->GetGUID();
        auction->bid    = price;
        bot->UpdateAchievementCriteria(ACHIEVEMENT_CRITERIA_TYPE_HIGHEST_AUCTION_BID, price);

        // Mails under the same transaction, as the game does it.
        sAuctionMgr->SendAuctionSalePendingMail(auction, trans);
        sAuctionMgr->SendAuctionSuccessfulMail(auction, trans);
        sAuctionMgr->SendAuctionWonMail(auction, trans);
        sScriptMgr->OnAuctionSuccessful(house, auction);

        auction->DeleteFromDB(trans);
        sAuctionMgr->RemoveAItem(auction->item_guid);
        house->RemoveAuction(auction);   // deletes it

        bot->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);
        return true;
    }

    // The bot's delivered auction mail: money and items into its bags, then
    // the mail gone. What does not fit stays for next time.
    void CollectMail(Player* bot, VisitReport& r)
    {
        const time_t now = GameTime::GetGameTime().count();

        std::vector<Mail*> mails;
        for (Mail* mail : bot->GetMails())
            if (mail && mail->messageType == MAIL_AUCTION && mail->state != MAIL_STATE_DELETED &&
                mail->deliver_time <= now && !mail->COD)
                mails.push_back(mail);
        if (mails.empty())
            return;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        std::vector<Mail*> emptied;
        bool full = false;

        for (Mail* mail : mails)
        {
            bool tookMoney = false;
            if (mail->money)
            {
                if (!bot->ModifyMoney(int32(std::min<uint32_t>(mail->money, uint32_t(INT32_MAX))), false))
                    continue;   // at the gold cap: leave it
                r.money += mail->money;
                g_stats.mailMoney += mail->money;
                mail->money = 0;
                tookMoney = true;
            }

            std::vector<uint32_t> taken;
            for (MailItemInfo const& info : std::vector<MailItemInfo>(mail->items))
            {
                Item* item = bot->GetMItem(info.item_guid);
                if (!item)
                    continue;
                ItemPosCountVec dest;
                if (bot->CanStoreItem(NULL_BAG, NULL_SLOT, dest, item, false) != EQUIP_ERR_OK)
                {
                    full = true;
                    break;
                }
                mail->RemoveItem(info.item_guid);
                bot->RemoveMItem(item->GetGUID().GetCounter());
                item->SetState(ITEM_UNCHANGED);   // as the game does: so it can be removed later
                bot->MoveItemToInventory(dest, item, true);
                taken.push_back(info.item_guid);
                ++r.items;
                ++g_stats.mailItems;
            }

            if (!tookMoney && taken.empty() && !mail->items.empty())
            {
                if (full)
                    break;
                continue;   // nothing came out of it: left as it is
            }
            ++r.mails;
            ++g_stats.mails;

            if (mail->items.empty())
            {
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_BY_ID);
                stmt->SetData(0, mail->messageID);
                trans->Append(stmt);
                stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_ITEM_BY_ID);
                stmt->SetData(0, mail->messageID);
                trans->Append(stmt);
                emptied.push_back(mail);
            }
            else
            {
                CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_MAIL);
                stmt->SetData(0, uint8(1));
                stmt->SetData(1, uint32(mail->expire_time));
                stmt->SetData(2, uint32(mail->deliver_time));
                stmt->SetData(3, mail->money);
                stmt->SetData(4, mail->COD);
                stmt->SetData(5, uint8(mail->checked));
                stmt->SetData(6, mail->messageID);
                trans->Append(stmt);
                for (uint32_t guid : taken)
                {
                    stmt = CharacterDatabase.GetPreparedStatement(CHAR_DEL_MAIL_ITEM);
                    stmt->SetData(0, guid);
                    trans->Append(stmt);
                }
            }

            if (full)
                break;
        }

        bot->SaveInventoryAndGoldToDB(trans);
        CharacterDatabase.CommitTransaction(trans);

        for (Mail* mail : emptied)
        {
            const uint32_t receiver = mail->receiver;
            bot->RemoveMail(mail->messageID);
            delete mail;
            sMailMgr->OnMailDeleted(receiver);
        }

        if (full)
        {
            ++g_stats.bagsFull;
            r.notes.push_back("its bags are full, so some mail is still waiting");
        }
    }

    void Sell(Player* bot, PlayerbotAI* ai, TraderState& state, AuctionHouseId houseId, AuctionHouseObject* house,
              AuctionHouseEntry const* houseEntry, HouseIndex& idx, VisitReport& r)
    {
        auto const& m = Cfg();
        const uint64_t self = bot->GetGUID().GetRawValue();

        const uint32_t up = idx.perOwner.count(self) ? idx.perOwner[self] : 0;
        if (up >= m.maxAuctionsPerBot)
        {
            r.notes.push_back(fmt::format("has {} auctions up already", up));
            return;
        }
        if (idx.botAuctions >= m.maxBotAuctionsPerHouse)
        {
            r.notes.push_back("the bots have Market.Sell.MaxBotAuctionsPerHouse up already");
            return;
        }
        const uint32_t room = std::min(m.maxListingsPerVisit, m.maxAuctionsPerBot - up);
        if (!room)
            return;

        // Never under what a vendor pays, after this house's cut: 5% in the
        // faction houses, 15% in the neutral ones.
        PbpPrice::Settings price = m.price;
        const uint32_t cut = std::min<uint32_t>(houseEntry->cutPercent, 50);
        price.floorPct = std::max<uint32_t>(price.floorPct, 100 * 100 / (100 - cut) + 2);

        struct Candidate
        {
            Item*               item;
            ItemTemplate const* proto;
            uint64_t            worth;
        };
        std::vector<Candidate> candidates;
        ForEachBagItem(bot, [&](Item* item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (Listable(bot, item, proto, state))
                candidates.push_back({ item, proto, PbpPrice::Value(price, FactsOf(proto)) * item->GetCount() });
        });
        // The most valuable first: that is what is worth the trip.
        std::sort(candidates.begin(), candidates.end(),
                  [](Candidate const& a, Candidate const& b) { return a.worth > b.worth; });

        uint32_t listed = 0;
        for (Candidate const& c : candidates)
        {
            if (listed >= room || r.checks >= m.maxUsageChecks)
                break;

            const int32_t suffix = c.item->GetItemRandomPropertyId();
            ++r.checks;
            if (UsageOf(ai, c.proto->ItemId, suffix) != ITEM_USAGE_AH)
                continue;   // playerbots keeps it, uses it, or vendors it

            const PbpPrice::ItemFacts facts = FactsOf(c.proto);
            const PriceRef ref = Lookup(idx, c.proto->ItemId, suffix);
            auto e = state.expiries.find(c.proto->ItemId);
            const uint32_t expiries = e != state.expiries.end() ? e->second : 0;

            uint64_t unit = 0;
            if (ref.minUnit && ref.minOwner == self)
                unit = ref.minUnit;   // its own is the cheapest: the same again
            else
                unit = PbpPrice::ListUnit(price, facts, ref.minUnit, expiries, Roll(), Roll());
            if (!unit)
            {
                ++g_stats.tooCheap;
                continue;
            }

            const uint32_t count  = c.item->GetCount();
            const uint64_t cap    = std::min<uint64_t>(uint64_t(m.maxBuyoutGold) * GOLD, MAX_MONEY_AMOUNT);
            const uint64_t buyout = std::min<uint64_t>(unit * count, cap);
            const uint64_t bid    = std::max<uint64_t>(buyout * Between(80, 95) / 100, 1);
            const uint32_t hours  = PickHours();
            const uint32_t entry  = c.proto->ItemId;

            if (!CreateAuction(bot, c.item, houseId, houseEntry, house, uint32_t(bid), uint32_t(buyout), hours * HOUR))
                continue;

            ++listed;
            ++g_stats.listed;
            g_stats.listedMoney += buyout;
            ++idx.perOwner[self];
            ++idx.botAuctions;
            ++idx.total;
            NotePrice(idx.byItem[Key(entry, suffix)], unit, self);
            NotePrice(idx.byEntry[entry], unit, self);
            r.listed.push_back(fmt::format("{} for {} ({}h){}", StackText(entry, count), PbpPrice::MoneyText(buyout),
                                           hours, ref.minUnit ? "" : ", none up yet"));

            PlayerbotsPlus::MarketEvent event;
            event.type   = PlayerbotsPlus::MarketEventType::Listed;
            event.bot    = bot->GetGUID();
            event.itemId = entry;
            event.count  = count;
            event.money  = buyout;
            Emit(event);
        }
    }

    bool IsWearable(ItemTemplate const* proto)
    {
        return proto->Class == ITEM_CLASS_WEAPON || proto->Class == ITEM_CLASS_ARMOR ||
               (proto->Class == ITEM_CLASS_CONTAINER && proto->SubClass == ITEM_SUBCLASS_CONTAINER);
    }

    // Where it goes on: one of each a visit. Weapons, shields and off-hands
    // share one, so a bot does not come home with a sword and an axe.
    uint32_t WornWhere(ItemTemplate const* proto)
    {
        if (proto->Class == ITEM_CLASS_WEAPON || proto->InventoryType == INVTYPE_SHIELD ||
            proto->InventoryType == INVTYPE_HOLDABLE)
            return 1000;
        if (proto->InventoryType == INVTYPE_ROBE)
            return INVTYPE_CHEST;
        return proto->InventoryType;
    }

    // Item level beats what it wears there now: worth asking playerbots about.
    bool BetterThanWorn(Player* bot, ItemTemplate const* proto)
    {
        if (proto->Class == ITEM_CLASS_CONTAINER)
            return true;   // playerbots compares bag sizes itself
        const uint8 slot = bot->FindEquipSlot(proto, NULL_SLOT, true);
        if (slot == NULL_SLOT)
            return false;
        Item* worn = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        return !worn || !worn->GetTemplate() || proto->ItemLevel > worn->GetTemplate()->ItemLevel;
    }

    // Which trade goods its professions use.
    bool ForItsTrade(Player* bot, uint32_t subClass)
    {
        auto has = [bot](uint32 skill) { return bot->HasSkill(skill); };
        switch (subClass)
        {
            case ITEM_SUBCLASS_CLOTH:         return has(SKILL_TAILORING);
            case ITEM_SUBCLASS_LEATHER:       return has(SKILL_LEATHERWORKING);
            case ITEM_SUBCLASS_METAL_STONE:
                return has(SKILL_BLACKSMITHING) || has(SKILL_ENGINEERING) || has(SKILL_JEWELCRAFTING);
            case ITEM_SUBCLASS_MEAT:          return has(SKILL_COOKING);
            case ITEM_SUBCLASS_HERB:          return has(SKILL_ALCHEMY) || has(SKILL_INSCRIPTION);
            case ITEM_SUBCLASS_ENCHANTING:    return has(SKILL_ENCHANTING);
            case ITEM_SUBCLASS_JEWELCRAFTING: return has(SKILL_JEWELCRAFTING);
            case ITEM_SUBCLASS_PARTS:
            case ITEM_SUBCLASS_EXPLOSIVES:
            case ITEM_SUBCLASS_DEVICES:       return has(SKILL_ENGINEERING);
            default:                          return false;
        }
    }

    // Upgrades first, the best of them first; then what its trade uses, cheapest first.
    bool Buy(Player* bot, PlayerbotAI* ai, AuctionHouseObject* house, VisitReport& r, bool& gotGear)
    {
        auto const& m = Cfg();
        if (!m.buy || !m.collectMail || (!m.buyUpgrades && !m.buyMaterials))
            return false;

        const uint32_t level   = bot->GetLevel();
        const uint64_t money   = bot->GetMoney();
        const uint64_t reserve = uint64_t(m.reservePerLevel) * level;
        if (money <= reserve)
            return false;
        const uint64_t spendable = (money - reserve) * m.buySharePct / 100;
        if (!spendable)
            return false;

        struct Candidate
        {
            uint32_t auctionId;
            bool     gear;
            uint32_t itemLevel;
            uint64_t price;
        };
        std::vector<Candidate> candidates;
        const ObjectGuid self = bot->GetGUID();

        for (auto const& [auctionId, auction] : house->GetAuctions())
        {
            if (!auction || !auction->buyout || auction->buyout > spendable)
                continue;
            if (auction->owner == self || auction->bidder == self || !auction->itemCount)
                continue;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(auction->item_template);
            if (!proto)
                continue;

            const PbpPrice::ItemFacts facts = FactsOf(proto);
            if (m.buyUpgrades && IsWearable(proto))
            {
                if (proto->Quality < m.upgradeMinQuality || proto->Quality > ITEM_QUALITY_EPIC)
                    continue;
                const uint32_t forLevel = PbpPrice::LevelOf(facts);
                if (forLevel > level || forLevel + m.upgradeLevelWindow < level)
                    continue;
                if (bot->CanUseItem(proto) != EQUIP_ERR_OK || !BetterThanWorn(bot, proto))
                    continue;
                if (auction->buyout > PbpPrice::GearBudget(m.price, facts, level, spendable))
                    continue;
                candidates.push_back({ auctionId, true, proto->ItemLevel, auction->buyout });
            }
            else if (m.buyMaterials && proto->Class == ITEM_CLASS_TRADE_GOODS && proto->Quality <= ITEM_QUALITY_RARE &&
                     ForItsTrade(bot, proto->SubClass))
            {
                const uint64_t unit = auction->buyout / auction->itemCount;
                if (unit > PbpPrice::GoodsBudgetUnit(m.price, facts, m.materialsOverValuePct) ||
                    auction->buyout > spendable / 2)
                    continue;
                candidates.push_back({ auctionId, false, 0, auction->buyout });
            }
        }
        if (candidates.empty())
            return false;

        std::sort(candidates.begin(), candidates.end(), [](Candidate const& a, Candidate const& b)
        {
            if (a.gear != b.gear)
                return a.gear;
            if (a.itemLevel != b.itemLevel)
                return a.itemLevel > b.itemLevel;
            return a.price < b.price;
        });

        uint32_t bought = 0;
        std::unordered_map<uint64_t, bool> asked;   // (entry, suffix) -> wanted
        std::unordered_set<uint32_t> slotsFilled;   // WornWhere of what it bought
        for (Candidate const& c : candidates)
        {
            if (bought >= m.maxBuysPerVisit)
                break;
            AuctionEntry* auction = house->GetAuction(c.auctionId);
            if (!auction)
                continue;
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(auction->item_template);
            if (!proto || (c.gear && slotsFilled.count(WornWhere(proto))))
                continue;

            const uint32_t entry  = auction->item_template;
            const int32_t  suffix = SuffixOf(auction);
            const uint64_t key    = Key(entry, suffix);
            auto known = asked.find(key);
            bool wanted = false;
            if (known != asked.end())
                wanted = known->second;
            else
            {
                if (r.checks >= m.maxUsageChecks)
                    break;
                ++r.checks;
                const ItemUsage usage = UsageOf(ai, entry, suffix);
                wanted = c.gear ? (usage == ITEM_USAGE_EQUIP || usage == ITEM_USAGE_REPLACE)
                                : usage == ITEM_USAGE_SKILL;
                asked.emplace(key, wanted);
            }
            if (!wanted)
                continue;

            if (uint64_t(bot->GetMoney()) < auction->buyout + uint64_t(m.reservePerLevel) * level)
                continue;

            const uint32_t   count  = auction->itemCount;
            const uint32_t   price  = auction->buyout;
            const ObjectGuid seller = auction->owner;
            if (!BuyOut(bot, house, auction))
                continue;

            ++bought;
            ++g_stats.bought;
            g_stats.boughtMoney += price;
            // One of a thing a visit: an upgrade for a slot, a stack of something.
            asked[key] = false;
            if (c.gear)
            {
                gotGear = true;
                slotsFilled.insert(WornWhere(proto));
            }
            r.bought.push_back(fmt::format("{} for {}", StackText(entry, count), PbpPrice::MoneyText(price)));

            PlayerbotsPlus::MarketEvent event;
            event.type   = PlayerbotsPlus::MarketEventType::Bought;
            event.bot    = self;
            event.itemId = entry;
            event.count  = count;
            event.money  = price;
            event.other  = seller;
            Emit(event);
        }
        return bought > 0;
    }

    bool Visit(Player* bot, PlayerbotAI* ai, VisitReport& r)
    {
        const AuctionHouseId houseId = HouseOf(bot);
        AuctionHouseObject* house = sAuctionMgr->GetAuctionsMapByHouseId(houseId);
        AuctionHouseEntry const* houseEntry = AuctionHouseMgr::GetAuctionHouseEntryFromHouse(houseId);
        if (!house || !houseEntry)
        {
            r.notes.push_back("there is no auction house for it");
            return false;
        }

        auto const& m = Cfg();
        TraderState& state = g_bots[bot->GetGUID().GetRawValue()];
        ++g_stats.visits;

        if (m.collectMail)
            CollectMail(bot, r);

        HouseIndex& idx = IndexOf(houseId);
        if (m.sell)
            Sell(bot, ai, state, houseId, house, houseEntry, idx, r);

        bool gotGear = false;
        if (m.buy && Buy(bot, ai, house, r, gotGear))
            CollectMail(bot, r);   // what it won is in the post already

        if (gotGear && m.equipBought)
        {
            ai->DoSpecificAction("equip upgrade", Event(), true);
            ++g_stats.equipped;
        }
        return true;
    }

    std::string Describe(Player* bot, VisitReport const& r)
    {
        std::string out = bot->GetName() + ":";
        if (r.mails)
            out += fmt::format(" collected {} auction mail{} ({}, {} item{}).", r.mails, r.mails == 1 ? "" : "s",
                               PbpPrice::MoneyText(r.money), r.items, r.items == 1 ? "" : "s");
        for (std::string const& s : r.listed)
            out += " Listed " + s + ".";
        for (std::string const& s : r.bought)
            out += " Bought " + s + ".";
        for (std::string const& s : r.notes)
            out += " It " + s + ".";
        if (!r.mails && r.listed.empty() && r.bought.empty() && r.notes.empty())
            out += " nothing to sell, nothing it wanted.";
        return out;
    }

    void Prune()
    {
        const auto now = Clock::now();
        for (auto it = g_bots.begin(); it != g_bots.end();)
        {
            if (now - it->second.lastSeen > kForgetAfter)
                it = g_bots.erase(it);
            else
                ++it;
        }
    }
}

namespace PlayerbotsPlus
{
    void AddMarketListener(MarketListener listener)
    {
        if (listener)
            g_listeners.push_back(std::move(listener));
    }
}

void PbpMarket_Update(uint32_t diff)
{
    auto const& m = Cfg();
    if (!g_Pbp.enable || !m.enable)
        return;

    g_pruneTimer += diff;
    if (g_pruneTimer >= kPruneMs)
    {
        g_pruneTimer = 0;
        Prune();
    }

    g_timer += diff;
    if (g_timer < m.intervalMs)
        return;
    g_timer = 0;

    const auto now = Clock::now();
    std::vector<std::pair<Player*, PlayerbotAI*>> due;
    uint32_t inCities = 0;

    for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* bot = it->second;
        if (!bot || !bot->IsInWorld() || !InTradingCity(bot))
            continue;
        ++inCities;

        TraderState& state = g_bots[bot->GetGUID().GetRawValue()];
        state.lastSeen = now;
        if (!state.scheduled)
        {
            // Seen in a city for the first time: some time in the next while,
            // not all of them on the first tick.
            state.scheduled = true;
            state.nextVisit = now + std::chrono::minutes(Between(0, m.cooldownMaxMinutes));
            continue;
        }
        if (state.nextVisit > now)
            continue;

        PlayerbotAI* ai = nullptr;
        if (CanTrade(bot, ai, false))
            due.emplace_back(bot, ai);
    }
    g_stats.inCities = inCities;

    std::shuffle(due.begin(), due.end(), g_rng);
    if (due.size() > m.visitsPerInterval)
        due.resize(m.visitsPerInterval);

    for (auto const& [bot, ai] : due)
    {
        TraderState& state = g_bots[bot->GetGUID().GetRawValue()];
        state.nextVisit = now + std::chrono::minutes(Between(m.cooldownMinMinutes, m.cooldownMaxMinutes));

        VisitReport r;
        Visit(bot, ai, r);
        if (m.logTrades && (r.mails || !r.listed.empty() || !r.bought.empty()))
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Auction house: {}", Describe(bot, r));
    }
}

void PbpMarket_OnSaleMailed(AuctionEntry const* auction, uint32_t ownerAccount)
{
    if (!auction || !IsRandomBotAccount(ownerAccount))
        return;

    ++g_stats.sold;
    g_stats.soldMoney += auction->bid;
    if (Cfg().logTrades)
    {
        std::string seller, buyer;
        sCharacterCache->GetCharacterNameByGuid(auction->owner, seller);
        sCharacterCache->GetCharacterNameByGuid(auction->bidder, buyer);
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Auction house: {} sold {} for {} to {}",
                 seller.empty() ? auction->owner.ToString() : seller,
                 StackText(auction->item_template, auction->itemCount), PbpPrice::MoneyText(auction->bid),
                 buyer.empty() ? std::string("someone") : buyer);
    }

    PlayerbotsPlus::MarketEvent event;
    event.type   = PlayerbotsPlus::MarketEventType::Sold;
    event.bot    = auction->owner;
    event.itemId = auction->item_template;
    event.count  = auction->itemCount;
    event.money  = auction->bid;
    event.other  = auction->bidder;
    Emit(event);
}

void PbpMarket_OnExpiryMailed(AuctionEntry const* auction, uint32_t ownerAccount)
{
    if (!auction || !IsRandomBotAccount(ownerAccount))
        return;

    ++g_stats.expired;
    uint8_t& times = g_bots[auction->owner.GetRawValue()].expiries[auction->item_template];
    if (times < 255)
        ++times;

    PlayerbotsPlus::MarketEvent event;
    event.type   = PlayerbotsPlus::MarketEventType::Expired;
    event.bot    = auction->owner;
    event.itemId = auction->item_template;
    event.count  = auction->itemCount;
    event.money  = auction->buyout;
    Emit(event);
}

bool PbpMarket_VisitNow(Player* bot, std::string& report)
{
    PlayerbotAI* ai = nullptr;
    std::string why;
    if (!CanTrade(bot, ai, true, &why))
    {
        report = fmt::format("{} cannot trade now: it {}.", bot ? bot->GetName() : std::string("Nobody"), why);
        return false;
    }

    VisitReport r;
    const bool ok = Visit(bot, ai, r);
    TraderState& state = g_bots[bot->GetGUID().GetRawValue()];
    state.scheduled = true;
    state.lastSeen  = Clock::now();
    state.nextVisit = state.lastSeen +
                      std::chrono::minutes(Between(Cfg().cooldownMinMinutes, Cfg().cooldownMaxMinutes));
    report = Describe(bot, r);
    return ok;
}

void PbpMarket_Reset()
{
    for (HouseIndex& idx : g_houses)
        idx.valid = false;
    g_ownerIsBot.clear();
    g_timer = 0;
}

PbpMarketStats PbpMarket_GetStats()
{
    return g_stats;
}
