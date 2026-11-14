#include "PlayerbotsPlusDrops.h"

#include "PlayerbotsPlusDropChance.h"

#include "DatabaseEnv.h"
#include "Field.h"
#include "GameObject.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "ObjectMgr.h"
#include "QueryResult.h"
#include "SharedDefines.h"
#include "Timer.h"

#include <cstdlib>
#include <unordered_map>
#include <unordered_set>

namespace
{
    std::unordered_map<uint32_t, PbpDrops::Drop> g_drops;
    PbpDropsStats                                g_dropStats;

    // A loot table's rows, by entry, as the core reads them
    // (LootStore::LoadLootTable): only those for `wanted` entries (all, with
    // none), leaving out what drops only for someone on a quest and what only
    // a hard mode gives.
    PbpDrops::Tables Read(char const* table, std::unordered_set<uint32_t> const* wanted)
    {
        PbpDrops::Tables out;
        QueryResult result = WorldDatabase.Query(
            "SELECT Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId, MaxCount FROM {} "
            "ORDER BY Entry, Item",
            table);
        if (!result)
            return out;
        do
        {
            Field* f = result->Fetch();
            const uint32 entry = f[0].Get<uint32>();
            if (wanted && !wanted->contains(entry))
                continue;
            if (f[4].Get<bool>() || !(f[5].Get<uint16>() & LOOT_MODE_DEFAULT))
                continue;
            PbpDrops::Row row;
            row.item      = f[1].Get<uint32>();
            row.reference = uint32(std::abs(f[2].Get<int32>()));
            row.chance    = f[3].Get<float>();
            row.group     = f[6].Get<uint8>();
            row.maxCount  = f[7].Get<uint8>();
            out[entry].push_back(row);
        } while (result->NextRow());
        return out;
    }
}

void PbpDrops_Load()
{
    const uint32 start = getMSTime();
    g_drops.clear();
    g_dropStats = {};

    // How many of each creature the world has: a spawn that may be one of
    // two or three kinds counts a share to each (in sixths).
    std::unordered_map<uint32_t, uint32_t> spawned;
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        const uint32_t kinds = 1 + (data.id2 ? 1 : 0) + (data.id3 ? 1 : 0);
        for (uint32_t id : { data.id, data.id2, data.id3 })
            if (id)
                spawned[id] += 6 / kinds;
    }

    // The creatures' loot tables in use, with how many of those creatures
    // there are; the chests' and fishing holes'.
    std::unordered_map<uint32_t, uint32_t> creatureLoot;   // loot id: how many
    for (auto const& [entry, t] : *sObjectMgr->GetCreatureTemplates())
        if (t.lootid)
        {
            auto it = spawned.find(entry);
            creatureLoot[t.lootid] += it == spawned.end() ? 0 : it->second;
        }
    std::unordered_set<uint32_t> chestLoot;
    for (auto const& [entry, t] : *sObjectMgr->GetGameObjectTemplates())
        if (const uint32 loot = t.GetLootId())
            chestLoot.insert(loot);

    std::unordered_set<uint32_t> creatureIds;
    for (auto const& [loot, many] : creatureLoot)
        creatureIds.insert(loot);

    const PbpDrops::Tables references = Read("reference_loot_template", nullptr);
    const PbpDrops::Tables creatures  = Read("creature_loot_template", &creatureIds);
    const PbpDrops::Tables chests     = Read("gameobject_loot_template", &chestLoot);
    const PbpDrops::Tables containers = Read("item_loot_template", nullptr);

    std::vector<PbpDrops::Source> sources;
    sources.reserve(creatures.size() + chests.size());
    for (auto const& [loot, table] : creatures)
        sources.push_back({ &table, creatureLoot[loot] });
    for (auto const& [loot, table] : chests)
        sources.push_back({ &table, 0 });

    g_drops = PbpDrops::Drops(sources, references, containers);

    g_dropStats.loaded  = true;
    g_dropStats.items   = uint32_t(g_drops.size());
    g_dropStats.sources = uint32_t(sources.size());
    g_dropStats.ms      = GetMSTimeDiffToNow(start);
    LOG_INFO("server.loading", ">> Playerbots Plus: how likely {} things are to drop, from {} loot tables, in {} ms",
             g_dropStats.items, g_dropStats.sources, g_dropStats.ms);
}

double PbpDrops_ChancePct(uint32_t itemId)
{
    auto it = g_drops.find(itemId);
    return it == g_drops.end() ? 0.0 : PbpDrops::Rarity(it->second) * 100.0;
}

double PbpDrops_BestPct(uint32_t itemId)
{
    auto it = g_drops.find(itemId);
    return it == g_drops.end() ? 0.0 : double(it->second.best) * 100.0;
}

PbpDropsStats PbpDrops_GetStats()
{
    return g_dropStats;
}
