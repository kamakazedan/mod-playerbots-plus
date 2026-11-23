#include "PlayerbotsPlusCrafting.h"
#include "PlayerbotsPlus.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusCraftText.h"
#include "PlayerbotsPlusDungeonQuests.h"
#include "PlayerbotsPlusDungeons.h"
#include "PlayerbotsPlusGroupQuests.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusInactive.h"
#include "PlayerbotsPlusLedger.h"
#include "PlayerbotsPlusSpace.h"
#include "PlayerbotsPlusUtil.h"

#include "Bag.h"
#include "CellImpl.h"
#include "DBCStores.h"
#include "GameObject.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Item.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "MotionMaster.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "PathGenerator.h"
#include "Player.h"
#include "SharedDefines.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"

#include "Event.h"
#include "ItemUsageValue.h"
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"

#include <fmt/core.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <map>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;

    constexpr uint32_t kHoldMs        = 5000;     // its AI held this long, again every tick at it
    constexpr uint32_t kCampfireSpell = 818;      // Basic Campfire
    constexpr uint32_t kCookingFire   = 4;        // SpellFocusObject.dbc: Cooking Fire
    constexpr float    kShopYards     = 300.0f;   // a vendor this near, in a town, is the town's
    constexpr float    kStationYards  = 300.0f;   // ...and a forge or an anvil
    constexpr float    kTwoPi         = 6.2831853f;
    constexpr auto     kLandWithin    = std::chrono::seconds(10);
    constexpr auto     kLongest       = std::chrono::minutes(5);

    PbpCraftSettings const& Cfg() { return g_Pbp.crafting; }

    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    uint32_t Between(uint32_t lo, uint32_t hi)
    {
        return hi <= lo ? lo : std::uniform_int_distribution<uint32_t>(lo, hi)(Rng());
    }

    PbpCraftingStats                g_stats;
    std::map<std::string, uint64_t> g_failReasons;   // why the game turned crafts down

    // ---- the recipes, once --------------------------------------------------------------------

    // A craft: reagents in, a thing out. Read from the skill lines' spells.
    struct Recipe
    {
        uint32_t spell   = 0;
        uint32_t skill   = 0;
        uint32_t learnAt = 0;   // MinSkillLineRank: made from this skill on
        uint32_t yellow  = 0;   // TrivialSkillLineRankLow: a point is certain up to here
        uint32_t grey    = 0;   // TrivialSkillLineRankHigh: none from here
        uint32_t product = 0;
        uint32_t makes   = 1;   // how many a craft makes, on average
        std::vector<std::pair<uint32_t, uint32_t>> reagents;   // item, how many
        std::vector<uint32_t> tools;      // items it needs to have
        std::vector<uint32_t> toolKinds;  // kinds of tool it needs one of (TotemCategory.dbc)
        uint32_t focus = 0;               // a forge, an anvil, a fire it is made at (SpellFocusObject.dbc)
        uint32_t gapMs = 0;               // from one craft to the next: its cast time, or its global cooldown
    };

    // The skills that make things. Gathering, fishing and riding make nothing.
    constexpr std::array<uint32_t, 11> kCraftSkills = {
        PbpCraft::Skill::Alchemy,     PbpCraft::Skill::Blacksmithing, PbpCraft::Skill::Cooking,
        PbpCraft::Skill::Enchanting,  PbpCraft::Skill::Engineering,   PbpCraft::Skill::FirstAid,
        PbpCraft::Skill::Inscription, PbpCraft::Skill::Jewelcrafting, PbpCraft::Skill::Leatherworking,
        PbpCraft::Skill::Mining,      PbpCraft::Skill::Tailoring };

    std::unordered_map<uint32_t, std::vector<Recipe>>   g_recipes;      // by skill line
    std::unordered_map<uint32_t, std::vector<uint32_t>> g_toolsOfKind;  // TotemCategory -> items that are one
    bool                                                g_recipesRead = false;

    void ReadRecipes()
    {
        if (g_recipesRead)
            return;
        g_recipesRead = true;
        g_recipes.clear();
        g_toolsOfKind.clear();

        std::unordered_set<uint32_t> seen;
        uint32_t count = 0;
        for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
        {
            SkillLineAbilityEntry const* a = sSkillLineAbilityStore.LookupEntry(i);
            if (!a || std::find(kCraftSkills.begin(), kCraftSkills.end(), a->SkillLine) == kCraftSkills.end())
                continue;
            if (!seen.insert(a->Spell).second)
                continue;
            SpellInfo const* info = sSpellMgr->GetSpellInfo(a->Spell);
            // Made again only after a wait (transmutes, mooncloth): not for a grind.
            if (!info || info->RecoveryTime || info->CategoryRecoveryTime)
                continue;
            Recipe r;
            for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                if (info->Effects[e].Effect == SPELL_EFFECT_CREATE_ITEM && info->Effects[e].ItemType)
                {
                    r.product = info->Effects[e].ItemType;
                    // BasePoints + 1 to BasePoints + DieSides, as the game rolls it.
                    const int32 low  = info->Effects[e].BasePoints + 1;
                    const int32 high = info->Effects[e].BasePoints + std::max<int32>(1, info->Effects[e].DieSides);
                    r.makes = uint32_t(std::max<int32>(1, (low + high) / 2));
                    break;
                }
            if (!r.product || !sObjectMgr->GetItemTemplate(r.product))
                continue;
            for (uint8 k = 0; k < MAX_SPELL_REAGENTS; ++k)
                if (info->Reagent[k] > 0 && info->ReagentCount[k] > 0)
                    r.reagents.emplace_back(uint32_t(info->Reagent[k]), info->ReagentCount[k]);
            if (r.reagents.empty())
                continue;   // nothing goes into it: nothing to grind with
            for (uint32 tool : info->Totem)
                if (tool)
                    r.tools.push_back(tool);
            for (uint32 kind : info->TotemCategory)
                if (kind)
                    r.toolKinds.push_back(kind);
            r.spell   = a->Spell;
            r.skill   = a->SkillLine;
            r.learnAt = a->MinSkillLineRank;
            r.yellow  = a->TrivialSkillLineRankLow;
            r.grey    = a->TrivialSkillLineRankHigh;
            r.focus   = info->RequiresSpellFocus;
            r.gapMs   = std::max(info->CalcCastTime(), info->StartRecoveryTime) + 300;
            g_recipes[r.skill].push_back(std::move(r));
            ++count;
        }
        // Which items are a kind of tool (a blacksmith hammer, a runed rod), to buy one.
        for (auto const& [entry, proto] : *sObjectMgr->GetItemTemplateStore())
            if (proto.TotemCategory)
                g_toolsOfKind[proto.TotemCategory].push_back(entry);
        g_stats.recipes = count;
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Crafting: {} recipes read from {} professions.", count,
                 g_recipes.size());
    }

    // ---- vendors, and where people work, by map, once ----------------------------------------

    // A vendor's offer: what a lot costs, and how many come in one.
    struct Offer
    {
        uint32_t price = 0;
        uint32_t lot   = 1;
    };

    struct VendorSpawn
    {
        float    x = 0.0f, y = 0.0f, z = 0.0f;
        uint32_t entry   = 0;
        uint32_t faction = 0;
    };

    struct Station
    {
        uint32_t map   = 0;
        float    x = 0.0f, y = 0.0f, z = 0.0f;
        uint32_t focus = 0;
        float    reach = 1.0f;   // its SpellFocus distance: the game's reach, besides both sizes
    };

    std::unordered_map<uint32_t, std::vector<VendorSpawn>>                g_vendors;      // by map
    std::unordered_map<uint32_t, std::vector<std::pair<uint32_t, Offer>>> g_vendorItems;  // by creature entry
    std::unordered_set<uint32_t>                                          g_vendorSold;   // anything a vendor sells
    std::unordered_map<uint32_t, std::vector<Station>>                    g_stations;     // by map
    bool                                                                  g_placesRead = false;

    bool OnOpenMap(uint32_t mapId)
    {
        MapEntry const* me = sMapStore.LookupEntry(mapId);
        return me && !me->Instanceable();
    }

    void ReadPlaces()
    {
        if (g_placesRead)
            return;
        g_placesRead = true;
        g_vendors.clear();
        g_vendorItems.clear();
        g_vendorSold.clear();
        g_stations.clear();

        size_t vendors = 0, stations = 0;
        for (auto const& [spawnId, data] : sObjectMgr->GetAllCreatureData())
        {
            CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(data.id);
            if (!info || !((info->npcflag | data.npcflag) & UNIT_NPC_FLAG_VENDOR))
                continue;
            VendorItemData const* items = sObjectMgr->GetNpcVendorItemList(data.id);
            if (!items || items->Empty())
                continue;
            if (!g_vendorItems.contains(data.id))
            {
                // What a bot would buy there: what is always in stock, for
                // gold, with no standing asked.
                std::vector<std::pair<uint32_t, Offer>>& offers = g_vendorItems[data.id];
                for (VendorItem const* v : items->m_items)
                {
                    if (!v)
                        continue;
                    g_vendorSold.insert(v->item);
                    if (v->maxcount || v->ExtendedCost)
                        continue;
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(v->item);
                    if (!proto || proto->BuyPrice <= 0 || proto->RequiredReputationFaction)
                        continue;
                    offers.emplace_back(v->item,
                                        Offer{ uint32_t(proto->BuyPrice), std::max<uint32_t>(1, proto->BuyCount) });
                }
            }
            if (!(data.phaseMask & PHASEMASK_NORMAL) || !OnOpenMap(data.mapid))
                continue;
            g_vendors[data.mapid].push_back({ data.posX, data.posY, data.posZ, data.id, info->faction });
            ++vendors;
        }
        for (auto const& [spawnId, data] : sObjectMgr->GetAllGOData())
        {
            if (!(data.phaseMask & PHASEMASK_NORMAL) || !OnOpenMap(data.mapid))
                continue;
            GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(data.id);
            if (!info || info->type != GAMEOBJECT_TYPE_SPELL_FOCUS || !info->spellFocus.focusId ||
                info->spellFocus.questID)
                continue;
            Station s;
            s.map   = data.mapid;
            s.x     = data.posX;
            s.y     = data.posY;
            s.z     = data.posZ;
            s.focus = info->spellFocus.focusId;
            s.reach = std::max(1.0f, float(info->spellFocus.dist));
            g_stations[data.mapid].push_back(s);
            ++stations;
        }
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Crafting: {} vendors and {} forges, anvils, fires and the "
                 "like out in the world.",
                 vendors, stations);
    }

    // ---- the bot ---------------------------------------------------------------------------------

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

    // A random bot on its own, out of a fight and out of any group, with
    // nothing of this module's or a real player's on its hands. `still`:
    // standing, not walking off somewhere.
    bool Free(Player* bot, PlayerbotAI*& ai, bool still, std::string* why)
    {
        auto no = [why](char const* reason)
        {
            if (why)
                *why = reason;
            return false;
        };
        if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            return no("is not in the world");
        if (!bot->IsAlive())
            return no("is dead");
        if (bot->IsInCombat() || bot->duel)
            return no("is fighting");
        if (bot->IsInFlight() || bot->GetVehicle() || bot->GetTransport())
            return no("is travelling");
        if (bot->InBattleground() || bot->InArena() || !OnOpenMap(bot->GetMapId()))
            return no("is in a dungeon or a battleground");
        if (bot->GetTradeData())
            return no("is trading");
        if (bot->GetGroup() || bot->GetGroupInvite())
            return no("is in a group");
        if (bot->GetLevel() < Cfg().minLevel)
            return no("is below Crafting.MinLevel");
        if (bot->IsNonMeleeSpellCast(false))
            return no("is casting");
        ai = PbpAiOf(bot);
        if (!ai || !ai->IsBotAI())
            return no("is not a bot");
        if (ai->GetMaster())
            return no("has a master");
        if (!sRandomPlayerbotMgr.IsRandomBot(bot))
            return no("is not a random bot");
        if (PbpDungeons_Busy(bot) || PbpGroupQuests_Busy(bot) || PbpDungeonQuests_Walking(bot))
            return no("is busy with a dungeon or a group quest");
        if (PbpHangout_Has(bot))
            return no("is hanging out in a city");
        if (still && bot->isMoving())
            return no("is on the move");
        return true;
    }

    // Playerbots' own judgement of an item for this bot (as the market asks it).
    ItemUsage UsageOf(PlayerbotAI* ai, uint32_t entry)
    {
        if (!ai->GetAiObjectContext())
            return ITEM_USAGE_KEEP;
        ItemUsageValue value(ai);
        value.Qualify(std::to_string(entry));
        return value.Calculate();
    }

    // Could ever be sold to someone, and is not a vendor's anyway.
    bool CouldSell(ItemTemplate const* p)
    {
        return p && p->Quality >= ITEM_QUALITY_NORMAL && p->SellPrice && p->Class != ITEM_CLASS_QUEST &&
               p->Bonding != BIND_WHEN_PICKED_UP && p->Bonding != BIND_QUEST_ITEM && p->Bonding != BIND_QUEST_ITEM1 &&
               !p->HasFlag(ITEM_FLAG_IS_BOUND_TO_ACCOUNT) && !p->HasFlag(ITEM_FLAG_CONJURED) &&
               !g_vendorSold.contains(p->ItemId);
    }

    bool Wearable(ItemTemplate const* p)
    {
        return p && (p->Class == ITEM_CLASS_WEAPON || p->Class == ITEM_CLASS_ARMOR) && p->InventoryType;
    }

    bool InTown(Player* bot)
    {
        return bot->HasRestFlag(REST_FLAG_IN_CITY) || bot->HasRestFlag(REST_FLAG_IN_TAVERN);
    }

    // What it could buy where it is: what the town's vendors that are not
    // the other side's sell, the cheapest of each.
    using Shop = std::unordered_map<uint32_t, Offer>;

    uint64_t UnitPrice(Offer const& o)
    {
        return (uint64_t(o.price) + o.lot - 1) / o.lot;
    }

    // Copper for `count` at a vendor: whole lots.
    uint64_t LotsPrice(Offer const& o, uint32_t count)
    {
        return uint64_t(o.price) * ((uint64_t(count) + o.lot - 1) / o.lot);
    }

    Shop ShopFor(Player* bot)
    {
        Shop shop;
        if (!Cfg().buyFromVendors || !InTown(bot))
            return shop;
        auto map = g_vendors.find(bot->GetMapId());
        if (map == g_vendors.end())
            return shop;
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        for (VendorSpawn const& v : map->second)
        {
            if (bot->GetExactDist2dSq(v.x, v.y) > kShopYards * kShopYards ||
                std::fabs(bot->GetPositionZ() - v.z) > 80.0f)
                continue;
            FactionTemplateEntry const* theirs = sFactionTemplateStore.LookupEntry(v.faction);
            if (mine && theirs && (mine->IsHostileTo(*theirs) || theirs->IsHostileTo(*mine)))
                continue;
            auto items = g_vendorItems.find(v.entry);
            if (items == g_vendorItems.end())
                continue;
            for (auto const& [item, offer] : items->second)
            {
                auto [at, fresh] = shop.emplace(item, offer);
                if (!fresh && UnitPrice(offer) < UnitPrice(at->second))
                    at->second = offer;
            }
        }
        return shop;
    }

    // A forge, an anvil or a fire for `focus` in reach where it stands, as the
    // game itself looks for one when the craft is cast (Spell::CheckSpellFocus).
    bool FocusInReach(Player* bot, uint32_t focus)
    {
        GameObject* found = nullptr;
        Acore::GameObjectFocusCheck check(bot, focus);
        Acore::GameObjectSearcher<Acore::GameObjectFocusCheck> searcher(bot, found, check);
        Cell::VisitObjects(bot, searcher, 40.0f);
        return found != nullptr;
    }

    // One of its town's to be put by, nobody about to see it go or get there.
    Station const* StationToPutBy(Player* bot, uint32_t focus)
    {
        if (!Cfg().stationVisits || !InTown(bot))
            return nullptr;
        auto it = g_stations.find(bot->GetMapId());
        if (it == g_stations.end())
            return nullptr;
        Station const* best = nullptr;
        float bestDist = 0.0f;
        for (Station const& s : it->second)
        {
            if (s.focus != focus)
                continue;
            const float d = bot->GetExactDist(s.x, s.y, s.z);
            if (d > kStationYards || (best && d >= bestDist) || !PbpUnseen(bot, s.x, s.y, s.z))
                continue;
            best = &s;
            bestDist = d;
        }
        return best;
    }

    // One of its town's it could walk to from where it stands (city life:
    // the crowd is seen), within `walk` yards and with a way there: the
    // nearest of the few nearest that has one.
    Station const* StationToWalkTo(Player* bot, uint32_t focus, float walk)
    {
        auto it = g_stations.find(bot->GetMapId());
        if (it == g_stations.end())
            return nullptr;
        std::vector<std::pair<float, Station const*>> near;
        for (Station const& s : it->second)
            if (s.focus == focus)
                if (const float d = bot->GetExactDist(s.x, s.y, s.z); d <= walk)
                    near.emplace_back(d, &s);
        std::sort(near.begin(), near.end(), [](auto const& a, auto const& b) { return a.first < b.first; });
        for (size_t i = 0; i < near.size() && i < 3; ++i)
        {
            Station const* s = near[i].second;
            PathGenerator path(bot);
            path.CalculatePath(s->x, s->y, s->z, false);
            if ((path.GetPathType() & PATHFIND_NORMAL) &&
                !(path.GetPathType() & (PATHFIND_NOPATH | PATHFIND_INCOMPLETE)))
                return s;
        }
        return nullptr;
    }

    // Everything a spell takes from its caster's bags: its reagents, its tools.
    bool HasWhatItTakes(Player* bot, SpellInfo const* info)
    {
        if (!info)
            return false;
        for (uint8 k = 0; k < MAX_SPELL_REAGENTS; ++k)
            if (info->Reagent[k] > 0 && info->ReagentCount[k] > 0 &&
                !bot->HasItemCount(uint32(info->Reagent[k]), info->ReagentCount[k]))
                return false;
        for (uint32 tool : info->Totem)
            if (tool && !bot->HasItemCount(tool, 1))
                return false;
        for (uint32 kind : info->TotemCategory)
            if (kind && !bot->HasItemTotemCategory(kind))
                return false;
        return true;
    }

    // A cook out of town makes its own fire: it knows how, and has what it
    // takes. Not in a city's streets or an inn: the town's fires are for that.
    bool CanMakeFire(Player* bot)
    {
        return Cfg().campfire && !InTown(bot) && bot->HasSpell(kCampfireSpell) && !bot->IsInWater() &&
               HasWhatItTakes(bot, sSpellMgr->GetSpellInfo(kCampfireSpell));
    }

    // ---- what it makes ---------------------------------------------------------------------------

    // Where it can do a recipe that needs a station.
    struct Access
    {
        bool           inReach = false;
        Station const* putBy   = nullptr;   // put by it, nobody about (its own crafting)
        Station const* walkTo  = nullptr;   // walked over to (city life)
        bool           fire    = false;     // a cooking fire: it makes one
    };

    struct Plan
    {
        Recipe const*  recipe = nullptr;
        PbpCraft::Why  why    = PbpCraft::Why::None;
        uint32_t       casts  = 0;
        uint32_t       skill  = 0;    // its skill in the recipe's profession
        uint32_t       chance = 0;    // percent: a point from one
        std::vector<std::pair<uint32_t, uint32_t>> buy;   // item, how many
        uint64_t       buyCopper = 0;
        Access         access;
        bool           wantsTown = false;   // has what it takes for one it can only make at a town's forge or anvil
        std::string    note;                // for .pbp craft
    };

    // Enough in its bags for one craft, with nothing bought.
    bool CarriesOne(Recipe const& r, std::unordered_map<uint32_t, uint32_t> const& have)
    {
        for (auto const& [item, need] : r.reagents)
        {
            auto h = have.find(item);
            if (h == have.end() || h->second < need)
                return false;
        }
        return true;
    }

    // Everything it carries, by item.
    std::unordered_map<uint32_t, uint32_t> Carried(Player* bot)
    {
        std::unordered_map<uint32_t, uint32_t> have;
        ForEachBagItem(bot, [&have](Item* item)
                       {
                           if (!item->IsInTrade())
                               have[item->GetEntry()] += item->GetCount();
                       });
        return have;
    }

    // What `casts` of a recipe would have it buy: reagents it is short of,
    // and a missing tool. False: something it is short of is not sold here.
    bool ToBuy(Player* bot, Recipe const& r, uint32_t casts, std::unordered_map<uint32_t, uint32_t> const& have,
               Shop const& shop, std::vector<std::pair<uint32_t, uint32_t>>& buy, uint64_t& copper)
    {
        buy.clear();
        copper = 0;
        for (auto const& [item, need] : r.reagents)
        {
            auto h = have.find(item);
            const uint64_t got = h == have.end() ? 0 : h->second;
            const uint64_t want = uint64_t(need) * casts;
            if (got >= want)
                continue;
            auto o = shop.find(item);
            if (o == shop.end())
                return false;
            const uint32_t count = uint32_t(want - got);
            buy.emplace_back(item, count);
            copper += LotsPrice(o->second, count);
        }
        for (uint32_t tool : r.tools)
        {
            if (have.contains(tool))
                continue;
            auto o = shop.find(tool);
            if (o == shop.end())
                return false;
            buy.emplace_back(tool, 1);
            copper += LotsPrice(o->second, 1);
        }
        for (uint32_t kind : r.toolKinds)
        {
            if (bot->HasItemTotemCategory(kind))
                continue;
            bool found = false;
            auto kinds = g_toolsOfKind.find(kind);
            if (kinds != g_toolsOfKind.end())
                for (uint32_t tool : kinds->second)
                    if (auto o = shop.find(tool); o != shop.end())
                    {
                        buy.emplace_back(tool, 1);
                        copper += LotsPrice(o->second, 1);
                        found = true;
                        break;
                    }
            if (!found)
                return false;
        }
        return true;
    }

    // What it can spend on this at one go: a share of its gold, never what
    // it keeps for repairs and training.
    uint64_t Allowance(Player* bot)
    {
        const uint64_t money   = bot->GetMoney();
        const uint64_t reserve = uint64_t(Cfg().reservePerLevel) * bot->GetLevel();
        if (money <= reserve)
            return 0;
        return std::min<uint64_t>(money - reserve, money * Cfg().spendSharePct / 100);
    }

    // `walk`: for city life, a forge or an anvil is one it walks over to
    // within this far (it is seen), never one it is put by; 0 for its own.
    bool MakePlan(Player* bot, PlayerbotAI* ai, Plan& plan, float walk = 0.0f)
    {
        ReadRecipes();
        ReadPlaces();
        PbpCraftSettings const& c = Cfg();
        const std::unordered_map<uint32_t, uint32_t> have = Carried(bot);
        const Shop shop = ShopFor(bot);
        const uint64_t allowance = Allowance(bot);

        std::vector<PbpCraft::Option> options;
        std::vector<Recipe const*>    recipes;
        std::vector<Access>           access;
        std::unordered_map<uint32_t, Access> byFocus;   // worked out once each
        uint32_t usageChecks = 0;

        for (uint32_t skill : kCraftSkills)
        {
            const uint32_t value = bot->GetPureSkillValue(skill);
            if (!value)
                continue;
            auto list = g_recipes.find(skill);
            if (list == g_recipes.end())
                continue;
            for (Recipe const& r : list->second)
            {
                if (r.learnAt > value || !bot->HasSpell(r.spell))
                    continue;

                // Where it can make it.
                Access a;
                if (r.focus)
                {
                    auto known = byFocus.find(r.focus);
                    if (known == byFocus.end())
                    {
                        Access fresh;
                        fresh.inReach = FocusInReach(bot, r.focus);
                        if (!fresh.inReach && walk > 0.0f)
                            fresh.walkTo = StationToWalkTo(bot, r.focus, walk);
                        else if (!fresh.inReach)
                            fresh.putBy = StationToPutBy(bot, r.focus);
                        if (!fresh.inReach && !fresh.putBy && !fresh.walkTo && r.focus == kCookingFire)
                            fresh.fire = CanMakeFire(bot);
                        known = byFocus.emplace(r.focus, fresh).first;
                    }
                    a = known->second;
                    if (!a.inReach && !a.putBy && !a.walkTo && !a.fire)
                    {
                        if (!InTown(bot) && CarriesOne(r, have))
                            plan.wantsTown = true;
                        continue;
                    }
                }

                // How many it can make: what it has, and what it can buy here
                // with what it would spend.
                uint32_t most = c.maxCasts;
                for (auto const& [item, need] : r.reagents)
                    if (!shop.contains(item))
                    {
                        auto h = have.find(item);
                        most = std::min<uint32_t>(most, h == have.end() ? 0 : h->second / need);
                    }
                std::vector<std::pair<uint32_t, uint32_t>> buy;
                uint64_t copper = 0;
                while (most && (!ToBuy(bot, r, most, have, shop, buy, copper) || copper > allowance))
                    --most;
                if (!most)
                    continue;

                PbpCraft::Option o;
                o.spell       = r.spell;
                o.pointChance = PbpCraft::PointChance(value, r.yellow, r.grey);
                o.canMake     = most;
                for (auto const& [item, need] : r.reagents)
                {
                    // Bought here: its price. Its own: what it would fetch.
                    auto offer = shop.find(item);
                    const uint64_t each =
                        offer != shop.end() ? UnitPrice(offer->second) : PlayerbotsPlus::ItemWorth(item);
                    o.costEach += each * need;
                }
                ItemTemplate const* proto = sObjectMgr->GetItemTemplate(r.product);
                o.sellable  = CouldSell(proto);
                o.worthEach = o.sellable ? PlayerbotsPlus::ItemWorth(r.product) * r.makes : 0;
                if (Wearable(proto) && proto->RequiredLevel <= bot->GetLevel() && usageChecks < c.usageChecks)
                {
                    ++usageChecks;
                    const ItemUsage u = UsageOf(ai, r.product);
                    o.upgrade = u == ITEM_USAGE_EQUIP || u == ITEM_USAGE_REPLACE;
                }
                options.push_back(o);
                recipes.push_back(&r);
                access.push_back(a);
            }
        }

        PbpCraft::ChoiceSettings cs;
        cs.maxCasts      = c.maxCasts;
        cs.minChance     = c.minChance;
        cs.sellMarginPct = c.sellMarginPct;
        const PbpCraft::Choice choice = PbpCraft::Choose(options, cs);
        plan.note = fmt::format("{} recipe{} it could make here and now{}", options.size(),
                                options.size() == 1 ? "" : "s",
                                plan.wantsTown ? "; more with a forge or an anvil, in town" : "");
        if (choice.why == PbpCraft::Why::None)
            return false;

        plan.recipe = recipes[choice.index];
        plan.why    = choice.why;
        plan.casts  = choice.casts;
        plan.access = access[choice.index];
        plan.skill  = bot->GetPureSkillValue(plan.recipe->skill);
        plan.chance = options[choice.index].pointChance;
        ToBuy(bot, *plan.recipe, plan.casts, have, shop, plan.buy, plan.buyCopper);
        return true;
    }

    // Room in its bags for what one craft makes (the game turns the craft
    // down without it).
    bool RoomFor(Player* bot, Recipe const& r)
    {
        ItemPosCountVec dest;
        return bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, r.product, r.makes) == EQUIP_ERR_OK;
    }

    // ---- at it --------------------------------------------------------------------------------------

    enum class Step : uint8_t
    {
        Landing,   // put by a station: not there yet
        Fire,      // making a campfire first
        Working
    };

    struct Session
    {
        uint32_t          spell = 0;
        uint32_t          left  = 0;
        uint32_t          gapMs = 0;
        uint32_t          focus = 0;
        PbpCraft::Why     why   = PbpCraft::Why::None;
        Step              step  = Step::Working;
        Clock::time_point started{};
        Clock::time_point nextCast{};
        float             ax = 0.0f, ay = 0.0f, az = 0.0f;   // where it works
        float             fx = 0.0f, fy = 0.0f;              // ...facing this
        bool              moved = false;                     // put there: back after, nobody about
        uint32_t          map = 0;
        float             ox = 0.0f, oy = 0.0f, oz = 0.0f, oo = 0.0f;
        bool              fireCast = false;
        bool              cityLife = false;   // city life's: its AI is the hangout's to hold and let go
    };
    std::unordered_map<uint64_t, Session> g_sessions;

    // City life's: what a bot hanging out is to make, while it walks over.
    struct CityPlan
    {
        Plan              plan;
        float             x = 0.0f, y = 0.0f, z = 0.0f, o = 0.0f;   // where it works, facing what
        bool              station = false;                           // ...a forge or an anvil walked to
        Clock::time_point until{};
    };
    std::unordered_map<uint64_t, CityPlan> g_cityPlans;
    constexpr auto kCityWalkWithin = std::chrono::minutes(2);

    struct Due
    {
        bool              scheduled = false;
        Clock::time_point next{};
        Clock::time_point townUntil{};   // waits for its next time in town, till then: a forge or an anvil wanted
    };
    std::unordered_map<uint64_t, Due> g_bots;
    uint32_t g_timer = 0;

    // Off its mount, out of a form it cannot craft in, on its feet, still:
    // as anyone settles down to craft.
    void ReadyToWork(Player* bot, uint32_t spell)
    {
        bot->RemoveAurasByType(SPELL_AURA_MOUNTED);
        if (SpellInfo const* info = sSpellMgr->GetSpellInfo(spell))
            if (bot->GetShapeshiftForm() != FORM_NONE &&
                info->CheckShapeshift(bot->GetShapeshiftForm()) != SPELL_CAST_OK)
                bot->RemoveAurasByType(SPELL_AURA_MOD_SHAPESHIFT);
        if (bot->IsSitState())
            bot->SetStandState(UNIT_STAND_STATE_STAND);
        bot->GetMotionMaster()->Clear();
        bot->StopMoving();
    }

    // Bought from a vendor of the town, at its price: as if it had been over.
    bool Buy(Player* bot, Shop const& shop, uint32_t item, uint32_t count)
    {
        auto o = shop.find(item);
        if (o == shop.end() || !count)
            return false;
        const uint32_t lots  = (count + o->second.lot - 1) / o->second.lot;
        const uint32_t units = lots * o->second.lot;
        const uint64_t cost  = uint64_t(o->second.price) * lots;
        if (bot->GetMoney() < cost || cost > uint64_t(std::numeric_limits<int32>::max()))
            return false;
        ItemPosCountVec dest;
        if (bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, item, units) != EQUIP_ERR_OK)
            return false;
        bot->ModifyMoney(-int32(cost));
        if (Item* stored = bot->StoreNewItem(dest, item, true))
            PbpLedger_VendorBought(bot, stored, units);
        g_stats.bought += units;
        g_stats.boughtCopper += cost;
        return true;
    }

    // A place to stand at a station: on its floor, a few steps from it, in
    // sight of it, out of the water and out of anyone's way.
    bool SpotBy(Player* bot, Station const& s, float& x, float& y, float& z)
    {
        Map* map = bot->GetMap();
        const float far = std::clamp(s.reach * 0.8f, 1.5f, 4.0f);
        for (int tries = 0; tries < 12; ++tries)
        {
            const float a = std::uniform_real_distribution<float>(0.0f, kTwoPi)(Rng());
            const float d = std::uniform_real_distribution<float>(std::min(1.5f, far), far)(Rng());
            x = s.x + d * std::cos(a);
            y = s.y + d * std::sin(a);
            z = map->GetHeight(bot->GetPhaseMask(), x, y, s.z + 2.0f, true, 4.0f);
            if (z <= INVALID_HEIGHT || std::fabs(z - s.z) > 2.0f ||
                map->IsInWater(bot->GetPhaseMask(), x, y, z, bot->GetCollisionHeight()))
                continue;
            if (!map->isInLineOfSight(s.x, s.y, s.z + 1.0f, x, y, z + 1.5f, bot->GetPhaseMask(),
                                      LINEOFSIGHT_ALL_CHECKS, VMAP::ModelIgnoreFlags::Nothing))
                continue;
            if (PbpSpace_Crowded(map, bot, x, y, z, 1.0f, bot))
                continue;
            return true;
        }
        return false;
    }

    void Release(Player* bot, PlayerbotAI* ai, Session const& s)
    {
        if (ai && !s.cityLife)
            ai->SetNextCheckDelay(0);
        if (!bot->IsInWorld())
            return;
        // Put there with nobody about: back where it was, nobody about still.
        if (s.moved && bot->IsAlive() && !bot->IsInCombat() && bot->GetMapId() == s.map &&
            PbpUnseen(bot, s.ox, s.oy, s.oz))
        {
            bot->NearTeleportTo(s.ox, s.oy, s.oz, s.oo);
            ++g_stats.putBack;
        }
        if (s.why == PbpCraft::Why::Use && ai && bot->IsAlive() && !bot->IsInCombat())
            ai->DoSpecificAction("equip upgrade", Event(), true);
    }

    bool Start(Player* bot, PlayerbotAI* ai, Plan const& plan, std::string* report, bool cityLife = false)
    {
        auto fail = [report](char const* why)
        {
            if (report)
                *report = why;
            return false;
        };
        Recipe const& r = *plan.recipe;
        if (!RoomFor(bot, r))
        {
            ++g_stats.noRoom;
            return fail("has no room in its bags for what it would make");
        }

        // Where it works first: nothing is bought for a station it cannot stand at.
        Station const* putBy = r.focus && !plan.access.inReach ? plan.access.putBy : nullptr;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        if (putBy && !SpotBy(bot, *putBy, x, y, z))
            return fail("found nowhere to stand at the station");

        if (!plan.buy.empty())
        {
            const Shop shop = ShopFor(bot);
            for (auto const& [item, count] : plan.buy)
                if (!Buy(bot, shop, item, count))
                    return fail("could not buy what it was short of");
        }

        Session s;
        s.spell    = r.spell;
        s.left     = plan.casts;
        s.gapMs    = r.gapMs;
        s.focus    = r.focus;
        s.why      = plan.why;
        s.started  = Clock::now();
        s.cityLife = cityLife;

        ReadyToWork(bot, r.spell);
        if (putBy)
        {
            s.moved = true;
            s.map   = bot->GetMapId();
            s.ox    = bot->GetPositionX();
            s.oy    = bot->GetPositionY();
            s.oz    = bot->GetPositionZ();
            s.oo    = bot->GetOrientation();
            s.ax    = x;
            s.ay    = y;
            s.az    = z;
            s.fx    = putBy->x;
            s.fy    = putBy->y;
            s.step  = Step::Landing;
            bot->NearTeleportTo(x, y, z, std::atan2(putBy->y - y, putBy->x - x));
            ++g_stats.putBy;
            ++g_stats.atStation;
        }
        else
        {
            s.ax = bot->GetPositionX();
            s.ay = bot->GetPositionY();
            s.az = bot->GetPositionZ();
            if (r.focus && plan.access.fire && !plan.access.inReach)
                s.step = Step::Fire;
            else if (r.focus)
                ++g_stats.atStation;
        }
        s.nextCast = Clock::now();
        if (ai)
            ai->SetNextCheckDelay(kHoldMs);
        g_sessions[bot->GetGUID().GetRawValue()] = s;

        ++g_stats.sessions;
        if (cityLife)
            ++g_stats.cityLife;
        switch (plan.why)
        {
            case PbpCraft::Why::Use:   ++g_stats.forUse;   break;
            case PbpCraft::Why::Skill: ++g_stats.forSkill; break;
            case PbpCraft::Why::Sell:  ++g_stats.forSale;  break;
            default: break;
        }
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(r.product);
        const std::string what = fmt::format("{} x{} ({} {}, {}% a point, {})",
                                             proto ? proto->Name1 : std::to_string(r.product), plan.casts,
                                             PbpCraft::Name(r.skill), plan.skill, plan.chance,
                                             PbpCraft::WhyName(plan.why));
        if (report)
            *report = "makes " + what +
                      (plan.buyCopper ? fmt::format(", having bought {}c of what it was short of", plan.buyCopper)
                                      : std::string()) +
                      (s.moved ? ", put by a station" : std::string()) +
                      (s.step == Step::Fire ? ", at a campfire it makes" : std::string());
        if (Cfg().logCrafts)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Crafting: {} makes {}{}.", bot->GetName(), what,
                     s.moved ? ", put by a station" : "");
        return true;
    }

    std::string CastFailText(SpellCastResult r)
    {
        switch (r)
        {
            case SPELL_FAILED_REAGENTS:               return "reagents missing";
            case SPELL_FAILED_TOTEMS:                 return "a tool missing";
            case SPELL_FAILED_TOTEM_CATEGORY:         return "a kind of tool missing";
            case SPELL_FAILED_REQUIRES_SPELL_FOCUS:   return "no forge, anvil or fire in reach";
            case SPELL_FAILED_NOT_MOUNTED:            return "mounted";
            case SPELL_FAILED_NOT_SHAPESHIFT:         return "in a form";
            case SPELL_FAILED_NOT_READY:              return "not ready";
            case SPELL_FAILED_DONT_REPORT:            return "turned down quietly (bags full?)";
            case SPELL_FAILED_MOVING:                 return "moving";
            case SPELL_FAILED_SPELL_IN_PROGRESS:      return "casting already";
            case SPELL_FAILED_ONLY_OUTDOORS:          return "indoors";
            case SPELL_FAILED_ONLY_ABOVEWATER:        return "in the water";
            case SPELL_FAILED_NOT_STANDING:           return "not standing";
            case SPELL_FAILED_NOT_HERE:               return "not here";
            case SPELL_FAILED_CANT_DO_THAT_RIGHT_NOW: return "not right now";
            default:                                  return fmt::format("SpellCastResult {}", uint32(r));
        }
    }

    void Failed(char const* what, SpellCastResult r)
    {
        ++g_stats.failed;
        ++g_failReasons[std::string(what) + CastFailText(r)];
    }

    // A tick at it. False: done (let go).
    bool Work(Player* bot, Session& s, const Clock::time_point now)
    {
        PlayerbotAI* ai = PbpAiOf(bot);
        auto stop = [&](bool cut)
        {
            if (cut)
                ++g_stats.cutShort;
            Release(bot, ai, s);
            return false;
        };
        // Anything its AI has to answer: let go at once.
        if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->duel || bot->GetTradeData() ||
            bot->GetGroup() || bot->GetGroupInvite() || bot->GetGuildIdInvited() || bot->GetArenaTeamIdInvited() ||
            now - s.started > kLongest)
            return stop(true);
        if (ai)
            ai->SetNextCheckDelay(kHoldMs);

        if (s.step == Step::Landing)
        {
            if (bot->IsBeingTeleported())
                return true;
            if (bot->GetExactDist(s.ax, s.ay, s.az) > 4.0f)
                return now - s.started > kLandWithin ? stop(true) : true;
            if (!FocusInReach(bot, s.focus))
            {
                Failed("", SPELL_FAILED_REQUIRES_SPELL_FOCUS);   // a station not up just now
                return stop(false);
            }
            s.step = Step::Working;
            bot->SetFacingTo(bot->GetAngle(s.fx, s.fy));
        }
        if (bot->IsNonMeleeSpellCast(false))
            return true;   // at it
        // Moved off what it was doing: something else wants it.
        if (bot->GetExactDist(s.ax, s.ay, s.az) > 5.0f)
            return stop(true);

        if (s.step == Step::Fire)
        {
            if (s.fireCast)
            {
                s.step     = Step::Working;
                s.nextCast = now + std::chrono::milliseconds(500);
                return true;
            }
            s.fireCast = true;
            const SpellCastResult res = bot->CastSpell(bot, kCampfireSpell, false);
            if (res != SPELL_CAST_OK)
            {
                Failed("campfire: ", res);
                return stop(false);
            }
            ++g_stats.campfires;
            return true;
        }

        if (!s.left)
            return stop(false);
        if (now < s.nextCast)
            return true;
        const SpellCastResult res = bot->CastSpell(bot, s.spell, false);
        if (res != SPELL_CAST_OK)
        {
            Failed("", res);
            return stop(false);
        }
        --s.left;
        ++g_stats.casts;
        s.nextCast = now + std::chrono::milliseconds(s.gapMs);
        return true;
    }
}

void PbpCrafting_Update(uint32_t diff)
{
    const auto now = Clock::now();

    // Those at it, whether or not crafting is still on: each let go when done.
    for (auto it = g_sessions.begin(); it != g_sessions.end();)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(it->first));
        if (!bot)
        {
            ++g_stats.cutShort;   // logged out
            it = g_sessions.erase(it);
        }
        else if (!Work(bot, it->second, now))
            it = g_sessions.erase(it);
        else
            ++it;
    }

    // City life's plans it never got round to.
    for (auto it = g_cityPlans.begin(); it != g_cityPlans.end();)
        it = now > it->second.until ? g_cityPlans.erase(it) : std::next(it);

    PbpCraftSettings const& c = Cfg();
    if (!g_Pbp.enable || !c.enable)
        return;
    g_timer += diff;
    if (g_timer < c.intervalMs)
        return;
    g_timer = 0;

    std::vector<std::pair<Player*, PlayerbotAI*>> due;
    for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* bot = it->second;
        if (!bot || !bot->IsInWorld())
            continue;
        const uint64_t key = bot->GetGUID().GetRawValue();
        Due& st = g_bots[key];
        if (!st.scheduled)
        {
            // Seen for the first time: some time in the next while, not all at once.
            st.scheduled = true;
            st.next = now + std::chrono::minutes(Between(0, c.maxMinutes));
            continue;
        }
        if (st.next > now || g_sessions.contains(key))
            continue;
        if (now < st.townUntil && !InTown(bot))
            continue;   // what it has wants a forge or an anvil: looked at again in town
        PlayerbotAI* ai = nullptr;
        if (Free(bot, ai, true, nullptr))
            due.emplace_back(bot, ai);
    }
    std::shuffle(due.begin(), due.end(), Rng());
    if (due.size() > c.botsPerInterval)
        due.resize(c.botsPerInterval);

    for (auto const& [bot, ai] : due)
    {
        Due& st = g_bots[bot->GetGUID().GetRawValue()];
        st.next = now + std::chrono::minutes(Between(c.minMinutes, c.maxMinutes));
        st.townUntil = Clock::time_point{};
        ++g_stats.looked;
        Plan plan;
        if (MakePlan(bot, ai, plan))
            Start(bot, ai, plan, nullptr);
        else
        {
            ++g_stats.nothing;
            // Nothing here, but a forge or an anvil would do: looked at again
            // as soon as it is in town, for as long as its turn would wait.
            if (plan.wantsTown)
            {
                st.townUntil = st.next;
                st.next      = now;
                ++g_stats.waitTown;
            }
        }
    }
}

bool PbpCrafting_Busy(Player* bot)
{
    if (!bot)
        return false;
    auto it = g_sessions.find(bot->GetGUID().GetRawValue());
    return it != g_sessions.end() && !it->second.cityLife;
}

bool PbpCrafting_AtIt(Player* bot)
{
    return bot && g_sessions.contains(bot->GetGUID().GetRawValue());
}

bool PbpCrafting_CityPlan(Player* bot, float walk, float& x, float& y, float& z, float& o)
{
    if (!bot || !g_Pbp.enable || !Cfg().enable || bot->GetLevel() < Cfg().minLevel || !bot->IsAlive() ||
        bot->IsInCombat() || bot->IsNonMeleeSpellCast(false))
        return false;
    const uint64_t key = bot->GetGUID().GetRawValue();
    if (g_sessions.contains(key))
        return false;
    PlayerbotAI* ai = PbpAiOf(bot);
    if (!ai || !ai->IsBotAI())
        return false;
    ++g_stats.looked;
    CityPlan cp;
    if (!MakePlan(bot, ai, cp.plan, std::max(walk, 1.0f)))
    {
        ++g_stats.nothing;
        return false;
    }
    Recipe const& r = *cp.plan.recipe;
    if (!RoomFor(bot, r))
    {
        ++g_stats.noRoom;
        return false;
    }
    if (r.focus && !cp.plan.access.inReach)
    {
        Station const* st = cp.plan.access.walkTo;
        if (!st || !SpotBy(bot, *st, cp.x, cp.y, cp.z))
            return false;
        cp.o       = std::atan2(st->y - cp.y, st->x - cp.x);
        cp.station = true;
    }
    else
    {
        cp.x = bot->GetPositionX();
        cp.y = bot->GetPositionY();
        cp.z = bot->GetPositionZ();
        cp.o = bot->GetOrientation();
    }
    cp.until = Clock::now() + kCityWalkWithin;
    x = cp.x;
    y = cp.y;
    z = cp.z;
    o = cp.o;
    g_cityPlans[key] = std::move(cp);
    return true;
}

bool PbpCrafting_CityBegin(Player* bot)
{
    if (!bot)
        return false;
    const uint64_t key = bot->GetGUID().GetRawValue();
    auto it = g_cityPlans.find(key);
    if (it == g_cityPlans.end())
        return false;
    CityPlan cp = std::move(it->second);
    g_cityPlans.erase(it);
    PlayerbotAI* ai = PbpAiOf(bot);
    if (!ai || g_sessions.contains(key) || Clock::now() > cp.until || !bot->IsAlive() || bot->IsInCombat())
        return false;
    if (cp.station)
    {
        // Walked over: the forge or the anvil is in reach now, as the game looks for it.
        if (!FocusInReach(bot, cp.plan.recipe->focus))
        {
            Failed("", SPELL_FAILED_REQUIRES_SPELL_FOCUS);
            return false;
        }
        cp.plan.access.inReach = true;
    }
    return Start(bot, ai, cp.plan, nullptr, true);
}

void PbpCrafting_CityCancel(Player* bot)
{
    if (!bot)
        return;
    const uint64_t key = bot->GetGUID().GetRawValue();
    g_cityPlans.erase(key);
    auto it = g_sessions.find(key);
    if (it == g_sessions.end() || !it->second.cityLife)
        return;
    if (bot->IsNonMeleeSpellCast(false))
        bot->InterruptNonMeleeSpells(false);
    ++g_stats.cutShort;
    g_sessions.erase(it);
}

bool PbpCrafting_Now(Player* bot, std::string& report)
{
    if (!bot)
    {
        report = "Nobody to look at.";
        return false;
    }
    if (PbpCrafting_Busy(bot))
    {
        report = fmt::format("{} is at it already.", bot->GetName());
        return false;
    }
    PlayerbotAI* ai = nullptr;
    std::string why;
    if (!Free(bot, ai, false, &why))
    {
        report = fmt::format("{} {}.", bot->GetName(), why);
        return false;
    }
    ++g_stats.looked;
    Plan plan;
    if (!MakePlan(bot, ai, plan))
    {
        ++g_stats.nothing;
        report = fmt::format("{}: nothing worth making ({}).", bot->GetName(), plan.note);
        return false;
    }
    std::string done;
    const bool started = Start(bot, ai, plan, &done);
    report = fmt::format("{} {}.", bot->GetName(), done);
    return started;
}

PbpCraftingStats PbpCrafting_GetStats()
{
    PbpCraftingStats s = g_stats;
    s.working = uint32_t(g_sessions.size());
    return s;
}

PbpBrief PbpCrafting_Brief(bool full)
{
    PbpBrief b;
    PbpCraftingStats const s = PbpCrafting_GetStats();
    const bool on = g_Pbp.enable && g_Pbp.crafting.enable;
    b.lines.push_back(fmt::format("Crafting: {} -- {} at it now; {} went to work ({} hanging out in a city; {} for an "
                                  "upgrade, {} for skill, {} to sell), {} crafts; {} looked at with nothing worth "
                                  "making.",
                                  on ? "on" : "OFF", s.working, s.sessions, s.cityLife, s.forUse, s.forSkill,
                                  s.forSale, s.casts, s.nothing));
    if (full)
    {
        b.lines.push_back(fmt::format("{} recipes known to it. At a forge, an anvil or a fire {} times ({} put by one "
                                      "with nobody about, {} of them put back after); {} campfires made; bought {} "
                                      "things from vendors for it ({}c); {} with no room in their bags; {} left for "
                                      "their next time in town, for a forge or an anvil; {} cut short (a fight, a "
                                      "trade, an invite, moved off, died, logged out).",
                                      s.recipes, s.atStation, s.putBy, s.putBack, s.campfires, s.bought,
                                      s.boughtCopper, s.noRoom, s.waitTown, s.cutShort));
        if (!g_failReasons.empty())
        {
            std::string reasons;
            for (auto const& [why, n] : g_failReasons)
                reasons += fmt::format("{}{} x{}", reasons.empty() ? "" : ", ", why, n);
            b.lines.push_back(fmt::format("Crafts the game turned down ({}): {}.", s.failed, reasons));
        }
    }
    // Most of what it begins, the game turning down: something here is wrong.
    if (on && s.casts + s.failed >= 50 && s.failed * 4 > s.casts + s.failed)
        b.issues.push_back(fmt::format("Crafting: the game turned down {} of {} crafts begun (.pbp report says why).",
                                       s.failed, s.casts + s.failed));
    return b;
}
