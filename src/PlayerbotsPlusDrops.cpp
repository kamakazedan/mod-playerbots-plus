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
    std::unordered_map<uint32_t, uint32_t>       g_lootCreature;   // a creature loot id -> the commonest creature with it
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
    g_lootCreature.clear();
    g_dropStats = {};

    // How many of each creature the world has: a spawn that may be one of
    // two or three kinds counts a share to each (in sixths). And on which
    // maps.
    std::unordered_map<uint32_t, uint32_t> spawned;
    std::unordered_map<uint32_t, std::unordered_map<uint32_t, uint32_t>> spawnedOn;   // creature -> map -> how many
    for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
    {
        const uint32_t kinds = 1 + (data.id2 ? 1 : 0) + (data.id3 ? 1 : 0);
        for (uint32_t id : { data.id, data.id2, data.id3 })
            if (id)
            {
                spawned[id] += 6 / kinds;
                spawnedOn[id][data.mapid] += 6 / kinds;
            }
    }

    // The creatures' loot tables in use, with how many of those creatures
    // there are and what level they are; the chests' and fishing holes'.
    struct Use
    {
        uint32_t many      = 0;     // in sixths, as spawned
        double   levels    = 0.0;   // level x how many
        double   plain     = 0.0;   // level, of each kind: for one nobody spawns
        uint32_t kinds     = 0;
        uint32_t commonest = 0;     // the creature with the most of them
        uint32_t most      = 0;
        std::unordered_map<uint32_t, uint32_t> on;   // map -> how many of them are on it
    };
    std::unordered_map<uint32_t, Use> creatureLoot;   // loot id
    for (auto const& [entry, t] : *sObjectMgr->GetCreatureTemplates())
        if (t.lootid)
        {
            auto it = spawned.find(entry);
            const uint32_t many  = it == spawned.end() ? 0 : it->second;
            const double   level = (double(t.minlevel) + double(t.maxlevel)) / 2.0;
            Use& u = creatureLoot[t.lootid];
            u.many   += many;
            u.levels += level * many;
            u.plain  += level;
            ++u.kinds;
            if (auto m = spawnedOn.find(entry); m != spawnedOn.end())
                for (auto const& [map, count] : m->second)
                    u.on[map] += count;
            if (!u.commonest || many > u.most || (many == u.most && entry < u.commonest))
            {
                u.commonest = entry;
                u.most      = many;
            }
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
    {
        Use const& u = creatureLoot[loot];
        PbpDrops::Source source;
        source.table = &table;
        source.many  = u.many;
        source.id    = loot;
        source.level = float(u.many ? u.levels / u.many : (u.kinds ? u.plain / u.kinds : 0.0));
        for (auto const& [map, count] : u.on)
            if (source.map == PbpDrops::kNoMap || count > u.on.at(source.map) ||
                (count == u.on.at(source.map) && map < source.map))
                source.map = map;
        sources.push_back(source);
        g_lootCreature[loot] = u.commonest;
    }
    for (auto const& [loot, table] : chests)
    {
        PbpDrops::Source source;
        source.table = &table;
        source.id    = loot;
        source.chest = true;
        sources.push_back(source);
    }

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

bool PbpDrops_Sources(uint32_t itemId, PbpDropsFrom& out)
{
    out = {};
    auto it = g_drops.find(itemId);
    if (it == g_drops.end())
        return false;
    PbpDrops::Drop const& d = it->second;
    out.kinds   = d.from.kinds;
    out.spawns  = (d.from.many + 3) / 6;   // spawned counts sixths
    out.chests  = d.from.chests;
    out.bestPct = double(d.best) * 100.0;
    out.onlyMap = (d.from.map == PbpDrops::kNoMap || d.from.map == PbpDrops::kMapsMany) ? -1 : int32_t(d.from.map);
    PbpDrops::Levels(d.from, out.minLevel, out.maxLevel);
    size_t n = 0;
    for (size_t i = 0; i < 3 && d.from.top[i]; ++i)
    {
        auto creature = g_lootCreature.find(d.from.top[i]);
        if (creature == g_lootCreature.end() || !creature->second)
            continue;
        out.creature[n]  = creature->second;
        out.chancePct[n] = double(d.from.topChance[i]) * 100.0;
        ++n;
    }
    return true;
}

PbpDropsStats PbpDrops_GetStats()
{
    return g_dropStats;
}
