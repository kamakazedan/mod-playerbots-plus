#include "PlayerbotsPlusLooted.h"
#include "PlayerbotsPlusLootedRules.h"

#include "DatabaseEnv.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "ObjectGuid.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include <fmt/core.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    // ---- notes, from whatever thread a hook runs on ------------------------------------

    enum class Kind : uint8_t
    {
        Stored,   // something stored for a bot (StoreNewItem): loot or not
        Looted,   // ...it was loot (the loot or roll hook straight after)
        Out,      // a stack left its bags whole
        In,       // something came into its bags from the post, a trade or a guild bank
    };

    struct Note
    {
        Kind     kind  = Kind::Stored;
        uint64_t id    = 0;   // a store, and the loot that came with it
        uint32_t owner = 0;   // the bot's character guid
        uint32_t stack = 0;   // the stack's item guid
        uint32_t entry = 0;
        uint32_t count = 0;   // how many were stored
        uint32_t after = 0;   // ...and how many are in the stack they went to
    };

    std::mutex            g_noteMutex;
    std::vector<Note>     g_notes;           // under g_noteMutex
    uint64_t              g_dropped = 0;     // under g_noteMutex
    constexpr size_t      kMaxNotes = 200000;
    std::atomic<uint64_t> g_sequence{ 0 };

    void Push(Note const& note)
    {
        std::lock_guard<std::mutex> lock(g_noteMutex);
        if (g_notes.size() < kMaxNotes)
            g_notes.push_back(note);
        else
            ++g_dropped;
    }

    // A playerbots bot, a random one or someone's own: nobody at its session.
    bool IsBot(Player* player)
    {
        WorldSession* session = player ? player->GetSession() : nullptr;
        return session && session->IsHeadless();
    }

    // The store a loot or roll hook comes straight after, on the same thread.
    // Those hooks must not follow their Item*: a module whose hook runs first
    // may have destroyed it (mod-junk-to-gold destroys grey loot in its own
    // OnPlayerLootItem), and a new item destroyed is deleted on the spot. They
    // only compare its address with this one's.
    struct Pending
    {
        Player const* player = nullptr;
        Item const*   item   = nullptr;   // compared, never followed
        uint64_t      id     = 0;
    };
    thread_local Pending t_pending;

    void Took(Player* player, Item* item)
    {
        const bool match = item && t_pending.item == item && t_pending.player == player;
        const uint64_t id = t_pending.id;
        t_pending = {};
        if (!match)
            return;
        Note n;
        n.kind = Kind::Looted;
        n.id   = id;
        Push(n);
    }

    // ---- the world tick's own, from here on ----------------------------------------------

    constexpr uint32_t kWriteMs      = 1 * MINUTE * IN_MILLISECONDS;   // a crash loses at most this much
    constexpr uint32_t kSweepMs      = 10 * MINUTE * IN_MILLISECONDS;
    constexpr size_t   kSweepPerTick = 500;
    constexpr size_t   kRowsPerQuery = 500;

    std::unordered_map<uint32_t, PbpLooted::Stack> g_stacks;    // a stack's item guid -> its record
    std::unordered_set<uint32_t>                   g_changed;   // written next time; gone ones deleted
    std::vector<uint32_t>                          g_sweep;     // the stacks being looked over
    size_t                                         g_sweepAt    = 0;
    uint32_t                                       g_sweepTimer = 0;
    uint32_t                                       g_writeTimer = 0;
    bool                                           g_loaded     = false;
    PbpLootedStats                                 g_stats;

    // Could ever be sold to someone: common or better, worth something to a
    // vendor, able to change hands. Nothing else is kept.
    bool Sellable(uint32_t entry)
    {
        ItemTemplate const* p = sObjectMgr->GetItemTemplate(entry);
        return p && p->Quality >= ITEM_QUALITY_NORMAL && p->SellPrice && p->Class != ITEM_CLASS_QUEST &&
               p->Bonding != BIND_WHEN_PICKED_UP && p->Bonding != BIND_QUEST_ITEM && p->Bonding != BIND_QUEST_ITEM1 &&
               !p->HasFlag(ITEM_FLAG_IS_BOUND_TO_ACCOUNT) && !p->HasFlag(ITEM_FLAG_CONJURED);
    }

    PbpLooted::Stack const* Find(uint32_t stack)
    {
        auto it = g_stacks.find(stack);
        return it == g_stacks.end() ? nullptr : &it->second;
    }

    void Set(uint32_t stack, PbpLooted::After const& after)
    {
        auto it = g_stacks.find(stack);
        if (after.known)
        {
            if (it == g_stacks.end())
                g_stacks.emplace(stack, after.stack);
            else if (it->second == after.stack)
                return;
            else
                it->second = after.stack;
            g_changed.insert(stack);
        }
        else if (it != g_stacks.end())
        {
            g_stacks.erase(it);
            g_changed.insert(stack);
        }
    }

    // What became of a stack it knew as someone's loot that is not now.
    void CountLost(PbpLooted::Stack const& was, uint32_t owner)
    {
        if (was.owner != owner)
            ++g_stats.moved;
        else
            ++g_stats.joined;
    }

    // The notes so far, in the order they came. A store and the loot that
    // came with it are both in one batch: the hooks run one after the other
    // inside StoreLootItem or the roll, never across a world tick.
    void Drain()
    {
        std::vector<Note> notes;
        {
            std::lock_guard<std::mutex> lock(g_noteMutex);
            notes.swap(g_notes);
            g_stats.dropped = g_dropped;
        }
        if (notes.empty())
            return;

        std::unordered_set<uint64_t> looted;
        for (Note const& n : notes)
            if (n.kind == Kind::Looted)
                looted.insert(n.id);

        for (Note const& n : notes)
        {
            if (n.kind == Kind::Looted)
                continue;   // read with its store
            PbpLooted::Stack const* found = Find(n.stack);
            switch (n.kind)
            {
                case Kind::Stored:
                {
                    ++g_stats.stored;
                    const bool loot = looted.contains(n.id);
                    if (loot)
                        ++g_stats.looted;
                    if (!found && (!loot || !Sellable(n.entry)))
                        break;   // nothing it knew, and nothing to know
                    const PbpLooted::After after = PbpLooted::Stored(found, n.owner, n.count, n.after, loot);
                    if (!found && after.known)
                        ++g_stats.marked;
                    else if (found && !after.known)
                        CountLost(*found, n.owner);
                    Set(n.stack, after);
                    break;
                }
                case Kind::Out:
                    if (found)
                    {
                        const PbpLooted::After after = PbpLooted::MovedOut(found, n.owner);
                        if (!after.known)
                            CountLost(*found, n.owner);
                        Set(n.stack, after);
                    }
                    break;
                case Kind::In:
                    if (found)
                    {
                        const PbpLooted::After after = PbpLooted::MovedIn(found, n.owner);
                        if (!after.known)
                            CountLost(*found, n.owner);
                        Set(n.stack, after);
                    }
                    break;
                default:
                    break;
            }
        }
    }

    // Now and then, every stack it knows of a bot that is about: used up,
    // sold to a vendor or destroyed is forgotten, and one with more in it
    // than loot put there is not its loot any more. One away (up for
    // auction, in the post) is left until it comes back.
    void Sweep(uint32_t diff)
    {
        if (g_sweepAt >= g_sweep.size())
        {
            g_sweepTimer += diff;
            if (g_sweepTimer < kSweepMs)
                return;
            g_sweepTimer = 0;
            g_sweep.clear();
            g_sweepAt = 0;
            g_sweep.reserve(g_stacks.size());
            for (auto const& [stack, s] : g_stacks)
                if (!s.away)
                    g_sweep.push_back(stack);
        }

        const size_t end = std::min(g_sweep.size(), g_sweepAt + kSweepPerTick);
        for (; g_sweepAt < end; ++g_sweepAt)
        {
            const uint32_t stack = g_sweep[g_sweepAt];
            PbpLooted::Stack const* found = Find(stack);
            if (!found || found->away)
                continue;
            Player* bot = ObjectAccessor::FindPlayerByLowGUID(found->owner);
            if (!bot || !bot->IsInWorld())
                continue;   // not about: looked at once it is
            Item* item = bot->GetItemByGuid(ObjectGuid::Create<HighGuid::Item>(stack));
            if (!item)
            {
                ++g_stats.gone;
                Set(stack, {});
                continue;
            }
            const PbpLooted::After after = PbpLooted::Seen(found, found->owner, item->GetCount());
            if (!after.known)
                CountLost(*found, found->owner);
            Set(stack, after);
        }
    }

    void Write(bool now)
    {
        if (g_changed.empty())
            return;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        std::string kept, gone;
        size_t keptRows = 0, goneRows = 0;
        auto writeKept = [&]()
        {
            if (keptRows)
                trans->Append("REPLACE INTO playerbots_plus_looted (item, owner, `count`, away) VALUES " + kept);
            kept.clear();
            keptRows = 0;
        };
        auto writeGone = [&]()
        {
            if (goneRows)
                trans->Append("DELETE FROM playerbots_plus_looted WHERE item IN (" + gone + ")");
            gone.clear();
            goneRows = 0;
        };
        for (uint32_t stack : g_changed)
        {
            if (PbpLooted::Stack const* s = Find(stack))
            {
                kept += fmt::format("{}({},{},{},{})", keptRows ? "," : "", stack, s->owner, s->count,
                                    s->away ? 1 : 0);
                if (++keptRows >= kRowsPerQuery)
                    writeKept();
            }
            else
            {
                gone += fmt::format("{}{}", goneRows ? "," : "", stack);
                if (++goneRows >= kRowsPerQuery)
                    writeGone();
            }
        }
        writeKept();
        writeGone();
        if (now)
            CharacterDatabase.DirectCommitTransaction(trans);
        else
            CharacterDatabase.CommitTransaction(trans);
        g_changed.clear();
        ++g_stats.writes;
    }
}

// ---- the hooks ------------------------------------------------------------------------

PbpLootedPlayerScript::PbpLootedPlayerScript()
    : PlayerScript("PbpLootedPlayerScript",
                   { PLAYERHOOK_ON_STORE_NEW_ITEM, PLAYERHOOK_ON_LOOT_ITEM, PLAYERHOOK_ON_GROUP_ROLL_REWARD_ITEM,
                     PLAYERHOOK_ON_AFTER_MOVE_ITEM_FROM_INVENTORY, PLAYERHOOK_ON_AFTER_MOVE_ITEM_TO_INVENTORY })
{
}

void PbpLootedPlayerScript::OnPlayerStoreNewItem(Player* player, Item* item, uint32 count)
{
    // Inside StoreNewItem: the stack it went to is there to read.
    t_pending = {};
    if (!item || !count || !IsBot(player))
        return;
    Note n;
    n.kind  = Kind::Stored;
    n.id    = ++g_sequence;
    n.owner = player->GetGUID().GetCounter();
    n.stack = item->GetGUID().GetCounter();
    n.entry = item->GetEntry();
    n.count = count;
    n.after = item->GetCount();
    t_pending = { player, item, n.id };
    Push(n);
}

void PbpLootedPlayerScript::OnPlayerLootItem(Player* player, Item* item, uint32 /*count*/, ObjectGuid /*lootguid*/)
{
    Took(player, item);
}

void PbpLootedPlayerScript::OnPlayerGroupRollRewardItem(Player* player, Item* item, uint32 /*count*/,
                                                        RollVote /*voteType*/, Roll* /*roll*/)
{
    Took(player, item);
}

void PbpLootedPlayerScript::OnPlayerAfterMoveItemFromInventory(Player* player, Item* it, uint8 /*bag*/,
                                                               uint8 /*slot*/, bool /*update*/)
{
    if (!it || !IsBot(player))
        return;
    Note n;
    n.kind  = Kind::Out;
    n.owner = player->GetGUID().GetCounter();
    n.stack = it->GetGUID().GetCounter();
    Push(n);
}

void PbpLootedPlayerScript::OnPlayerAfterMoveItemToInventory(Player* player, Item* it, bool /*update*/)
{
    // `it` is the stack it ended up in: the one that came, or one of its own
    // it joined.
    if (!it || !IsBot(player))
        return;
    Note n;
    n.kind  = Kind::In;
    n.owner = player->GetGUID().GetCounter();
    n.stack = it->GetGUID().GetCounter();
    Push(n);
}

// ---- the world thread -------------------------------------------------------------------

bool PbpLooted_IsLooted(Player* bot, Item* item)
{
    if (!bot || !item || !g_loaded)
        return false;
    Drain();
    ++g_stats.asked;
    const uint32_t stack = item->GetGUID().GetCounter();
    PbpLooted::Stack const* found = Find(stack);
    if (!found || item->GetOwnerGUID() != bot->GetGUID())
    {
        ++g_stats.refused;
        return false;
    }
    const uint32_t owner = bot->GetGUID().GetCounter();
    const PbpLooted::After after = PbpLooted::Seen(found, owner, item->GetCount());
    if (!after.known)
    {
        CountLost(*found, owner);
        ++g_stats.refused;
    }
    Set(stack, after);
    return after.known;
}

void PbpLooted_Load()
{
    // The SQL in data/sql makes it too; this is for a server whose updater is off.
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `playerbots_plus_looted` ("
        "`item` INT UNSIGNED NOT NULL, "
        "`owner` INT UNSIGNED NOT NULL, "
        "`count` INT UNSIGNED NOT NULL DEFAULT 0, "
        "`away` TINYINT UNSIGNED NOT NULL DEFAULT 0, "
        "PRIMARY KEY (`item`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-playerbots-plus: stacks bots looted themselves, the only "
        "things they sell'");
    // Gone since, or with someone else now: forgotten.
    CharacterDatabase.DirectExecute(
        "DELETE l FROM playerbots_plus_looted AS l LEFT JOIN item_instance AS i ON i.guid = l.item "
        "WHERE i.guid IS NULL OR i.owner_guid <> l.owner");

    g_stacks.clear();
    g_changed.clear();
    if (QueryResult result = CharacterDatabase.Query("SELECT item, owner, `count`, away FROM playerbots_plus_looted"))
    {
        do
        {
            Field* f = result->Fetch();
            PbpLooted::Stack s;
            s.owner = f[1].Get<uint32>();
            s.count = f[2].Get<uint32>();
            s.away  = f[3].Get<uint8>() != 0;
            g_stacks[f[0].Get<uint32>()] = s;
        } while (result->NextRow());
    }
    g_loaded = true;
    LOG_INFO("server.loading", "[Playerbots Plus] Bots sell only their own loot: {} stacks they looted themselves "
             "known.", g_stacks.size());
}

void PbpLooted_Update(uint32_t diff)
{
    if (!g_loaded)
        return;
    Drain();
    Sweep(diff);
    g_writeTimer += diff;
    if (g_writeTimer >= kWriteMs)
    {
        g_writeTimer = 0;
        Write(false);
    }
}

void PbpLooted_Shutdown()
{
    if (!g_loaded)
        return;
    Drain();
    Write(true);
}

PbpLootedStats PbpLooted_GetStats()
{
    PbpLootedStats s = g_stats;
    s.known = g_stacks.size();
    std::lock_guard<std::mutex> lock(g_noteMutex);
    s.dropped = g_dropped;
    return s;
}
