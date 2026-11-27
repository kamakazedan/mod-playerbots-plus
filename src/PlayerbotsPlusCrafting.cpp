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
#include "PlayerbotsPlusLooted.h"
#include "PlayerbotsPlusSkills.h"
#include "PlayerbotsPlusSpace.h"
#include "PlayerbotsPlusUtil.h"

#include "Bag.h"
#include "CellImpl.h"
#include "DatabaseEnv.h"
#include "DBCStores.h"
#include "Field.h"
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
#include "Opcodes.h"
#include "PathGenerator.h"
#include "Player.h"
#include "QueryResult.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "Timer.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "Event.h"
#include "ItemUsageValue.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
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
    constexpr uint32_t kDisenchantSpell = 13262;
    constexpr uint32_t kProspectSpell   = 31252;
    constexpr uint32_t kMillSpell       = 51005;
    constexpr uint32_t kBreakRoom     = 4;        // free bag slots before it breaks anything down
    constexpr auto     kToBreakFor    = std::chrono::hours(6);
    constexpr auto     kSkipFor       = std::chrono::hours(6);
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

    // What a go at a profession is.
    enum class Kind : uint8_t
    {
        Make,         // reagents in, a thing out
        Enchant,      // reagents in, an enchantment on a piece of its own gear
        Disenchant,   // a piece of gear into dust, essences, shards
        Mill,         // five herbs into pigment
        Prospect      // five ore into gems
    };

    char const* KindName(Kind kind)
    {
        switch (kind)
        {
            case Kind::Enchant:    return "enchants";
            case Kind::Disenchant: return "disenchants";
            case Kind::Mill:       return "mills";
            case Kind::Prospect:   return "prospects";
            default:               return "makes";
        }
    }

    // A craft: reagents in, a thing out -- or, for an enchanter, onto its
    // gear. Read from the skill lines' spells.
    struct Recipe
    {
        Kind     kind    = Kind::Make;
        uint32_t enchant = 0;   // Kind::Enchant: the enchantment (SpellItemEnchantment.dbc)
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

    // Disenchant, Prospecting and Milling as their professions list them:
    // from what skill, and how likely a point (Disenchant: as any craft).
    struct BreakAbility
    {
        uint32_t skill   = 0;
        uint32_t learnAt = 0;
        uint32_t yellow  = 0;
        uint32_t grey    = 0;
    };
    std::unordered_map<uint32_t, BreakAbility> g_breakAbility;   // by spell
    std::unordered_map<uint32_t, uint32_t>     g_enchantRank;    // an enchantment -> the skill its recipe is learnt at

    // What breaking something down gives, on average, from the world's own
    // loot tables (PbpCrafting_Load): item, how many.
    using YieldList = std::vector<std::pair<uint32_t, double>>;
    std::unordered_map<uint32_t, YieldList> g_disenchantYields;   // by an item's DisenchantID
    std::unordered_map<uint32_t, YieldList> g_millYields;         // by herb
    std::unordered_map<uint32_t, YieldList> g_prospectYields;     // by ore

    // Gear bought to be disenchanted (a deal in Trade chat): its item guid,
    // and until when it may be. World thread.
    std::unordered_map<uint32_t, std::chrono::steady_clock::time_point> g_toBreak;

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
            if (a->Spell == kDisenchantSpell || a->Spell == kProspectSpell || a->Spell == kMillSpell)
            {
                g_breakAbility[a->Spell] = { a->SkillLine, a->MinSkillLineRank, a->TrivialSkillLineRankLow,
                                             a->TrivialSkillLineRankHigh };
                continue;
            }
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
            // An enchanter's own grind: an enchantment put on a piece of gear.
            if (!r.product && a->SkillLine == PbpCraft::Skill::Enchanting)
                for (uint8 e = 0; e < MAX_SPELL_EFFECTS; ++e)
                    if (info->Effects[e].Effect == SPELL_EFFECT_ENCHANT_ITEM && info->Effects[e].MiscValue > 0 &&
                        sSpellItemEnchantmentStore.LookupEntry(uint32(info->Effects[e].MiscValue)))
                    {
                        r.kind    = Kind::Enchant;
                        r.enchant = uint32_t(info->Effects[e].MiscValue);
                        break;
                    }
            if (r.kind == Kind::Make && (!r.product || !sObjectMgr->GetItemTemplate(r.product)))
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
            if (r.kind == Kind::Enchant)
            {
                auto [known, fresh] = g_enchantRank.emplace(r.enchant, r.learnAt);
                if (!fresh)
                    known->second = std::min(known->second, r.learnAt);
            }
            g_recipes[r.skill].push_back(std::move(r));
            ++count;
        }
        // Which items are a kind of tool (a blacksmith hammer, a runed rod), to buy one.
        for (auto const& [entry, proto] : *sObjectMgr->GetItemTemplateStore())
            if (proto.TotemCategory)
                g_toolsOfKind[proto.TotemCategory].push_back(entry);
        g_stats.recipes = count;
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Crafting: {} recipes read from {} professions ({} of "
                 "them an enchanter's, onto gear); disenchanting, prospecting and milling {} listed by theirs.",
                 count, g_recipes.size(), g_enchantRank.size(), g_breakAbility.size() == 3 ? "all" : "not all");
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
        Kind           kind      = Kind::Make;
        Recipe const*  recipe    = nullptr;   // Make, Enchant: the craft
        uint32_t       spell     = 0;
        uint32_t       skillLine = 0;
        uint32_t       gapMs     = 0;
        uint32_t       focus     = 0;
        uint32_t       entry     = 0;         // Mill, Prospect: the herb or the ore
        std::vector<ObjectGuid> targets;      // Enchant: the piece it goes on; Disenchant: each piece in turn
        std::string    name;                  // what it makes, or works on
        PbpCraft::Why  why    = PbpCraft::Why::None;
        uint32_t       casts  = 0;
        uint32_t       skill  = 0;    // its skill in the profession
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

    // ---- breaking things down, and enchanting -------------------------------------------------

    uint32_t FreeSlots(Player* bot)
    {
        uint32_t free = 0;
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (!bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
                ++free;
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = bot->GetBagByPos(bagSlot))
                free += bag->GetFreeSlots();
        return free;
    }

    // Has it the skill for Disenchant, Prospecting or Milling, and the spell?
    // Playerbots sets a bot's skill without what comes with it, so one whose
    // skill says it should know the spell is taught it. `skill`: its skill
    // there, as the game checks it.
    bool KnowsBreak(Player* bot, uint32_t spell, uint32_t& skill)
    {
        auto ability = g_breakAbility.find(spell);
        if (ability == g_breakAbility.end())
            return false;
        skill = bot->GetSkillValue(ability->second.skill);
        if (!skill || bot->GetPureSkillValue(ability->second.skill) < ability->second.learnAt)
            return false;
        if (!bot->HasSpell(spell))
        {
            bot->learnSpell(spell);
            if (!bot->HasSpell(spell))
                return false;
            ++g_stats.taught;
        }
        return true;
    }

    // What a piece is worth to it kept whole: on the market if it could go
    // there, else what a vendor gives.
    uint64_t KeepWorth(Item* item, ItemTemplate const* p)
    {
        if (CouldSell(p) && !item->IsSoulBound())
            return PlayerbotsPlus::ItemWorth(p->ItemId);
        return p->SellPrice;
    }

    uint64_t WorthOf(YieldList const& yields)
    {
        double sum = 0.0;
        for (auto const& [item, many] : yields)
            sum += many * double(PlayerbotsPlus::ItemWorth(item));
        return uint64_t(sum);
    }

    struct Piece
    {
        Item*            item   = nullptr;
        uint64_t         keep   = 0;   // worth kept whole
        uint64_t         broken = 0;   // worth of what it gives
        uint32_t         need   = 0;   // the Enchanting it takes to break down
        YieldList const* yields = nullptr;
    };

    // An enchanter's spare gear, in its bags: its own -- looted, won on a
    // roll or made, never what playerbots hands out -- or bought to be broken
    // down (a deal in Trade chat), and no use to it. What it might
    // disenchant, with the skill for it, and practise an enchant on first.
    // The least worth keeping first.
    std::vector<Piece> SpareGear(Player* bot, PlayerbotAI* ai)
    {
        PbpCraftSettings const& c = Cfg();
        const auto now = Clock::now();
        std::vector<Item*> carried;
        ForEachBagItem(bot, [&carried](Item* item) { carried.push_back(item); });

        uint32_t usageChecks = 0;   // its own allowance: what it makes is asked of the other
        std::vector<Piece> out;
        for (Item* item : carried)
        {
            ItemTemplate const* p = item->GetTemplate();
            if (!p || (p->Class != ITEM_CLASS_WEAPON && p->Class != ITEM_CLASS_ARMOR) || !p->DisenchantID ||
                p->Quality < ITEM_QUALITY_UNCOMMON ||
                p->Quality > std::min<uint32_t>(c.disenchantMaxQuality, ITEM_QUALITY_EPIC) || item->IsInTrade() ||
                item->IsEquipped())
                continue;
            auto yields = g_disenchantYields.find(p->DisenchantID);
            if (yields == g_disenchantYields.end())
                continue;
            auto bought = g_toBreak.find(item->GetGUID().GetCounter());
            const bool toBreak = bought != g_toBreak.end() && bought->second > now;
            if (!toBreak)
            {
                if (!PbpLooted_IsOwn(bot, item) || usageChecks >= c.usageChecks)
                    continue;
                ++usageChecks;
                // Playerbots would sell it, or break it down itself: anything
                // else -- to wear, for a quest, to keep -- is not spare.
                const ItemUsage u = UsageOf(ai, p->ItemId);
                if (u != ITEM_USAGE_AH && u != ITEM_USAGE_VENDOR && u != ITEM_USAGE_DISENCHANT &&
                    u != ITEM_USAGE_NONE)
                    continue;
            }
            out.push_back({ item, toBreak ? 0 : KeepWorth(item, p), WorthOf(yields->second),
                            p->RequiredDisenchantSkill, &yields->second });
        }
        std::sort(out.begin(), out.end(), [](Piece const& a, Piece const& b) { return a.keep < b.keep; });
        return out;
    }

    // A piece of gear an enchant could go on, for the point. One it wears
    // with nothing on it yet -- and, below the level playerbots enchants a
    // bot's gear from itself (Playerbots.MinEnchantingBotLevel: what it chose
    // is left alone, and it puts its own back at every refresh), over what
    // an enchanter's recipe no better than this one left there; never over
    // anything better. Else a spare piece in its bags, whatever is on it:
    // the junk an enchanter practises on before it breaks it down.
    Item* PieceToEnchant(Player* bot, Recipe const& r, SpellInfo const* info, std::vector<Piece> const& spare)
    {
        if (!info)
            return nullptr;
        if (SpellItemEnchantmentEntry const* e = sSpellItemEnchantmentStore.LookupEntry(r.enchant))
            if (e->requiredLevel > bot->GetLevel() ||
                (e->requiredSkill && bot->GetSkillValue(e->requiredSkill) < e->requiredSkillValue))
                return nullptr;
        auto takes = [info](Item* item)
        {
            if (!item || item->IsInTrade() || !item->IsFitToSpellRequirements(info))
                return false;
            if (info->HasAttribute(SPELL_ATTR2_ALLOW_LOW_LEVEL_BUFF))
                return true;
            // As the game asks it (Spell::CheckItems).
            ItemTemplate const* p = item->GetTemplate();
            return (p->RequiredLevel ? p->RequiredLevel : p->ItemLevel) >= info->BaseLevel;
        };
        const bool itsOwnWork = bot->GetLevel() < sPlayerbotAIConfig.MinEnchantingBotLevel;
        for (uint8 slot = EQUIPMENT_SLOT_START; slot < EQUIPMENT_SLOT_END; ++slot)
        {
            Item* item = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
            if (!takes(item))
                continue;
            if (const uint32_t on = item->GetEnchantmentId(PERM_ENCHANTMENT_SLOT))
            {
                auto rank = g_enchantRank.find(on);
                if (!itsOwnWork || rank == g_enchantRank.end() || rank->second > r.learnAt)
                    continue;
            }
            return item;
        }
        for (Piece const& piece : spare)
            if (takes(piece.item))
                return piece.item;
        return nullptr;
    }

    // An empty place in its bags that takes `item` (a herb bag takes no ore).
    bool EmptyPlaceFor(Player* bot, Item* item, uint16& pos)
    {
        auto takes = [&](uint8 bag, uint8 slot)
        {
            if (bot->GetItemByPos(bag, slot))
                return false;
            ItemPosCountVec dest;
            if (bot->CanStoreItem(bag, slot, dest, item, false) != EQUIP_ERR_OK)
                return false;
            pos = uint16((uint16(bag) << 8) | slot);
            return true;
        };
        for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; ++slot)
            if (takes(INVENTORY_SLOT_BAG_0, slot))
                return true;
        for (uint8 bagSlot = INVENTORY_SLOT_BAG_START; bagSlot < INVENTORY_SLOT_BAG_END; ++bagSlot)
            if (Bag* bag = bot->GetBagByPos(bagSlot))
                for (uint32 i = 0; i < bag->GetBagSize(); ++i)
                    if (takes(bagSlot, uint8(i)))
                        return true;
        return false;
    }

    uint64_t SkipKey(uint32_t spell, uint32_t entry)
    {
        return (uint64_t(spell) << 32) | entry;
    }

    struct Due
    {
        bool              scheduled = false;
        Clock::time_point next{};
        Clock::time_point townUntil{};   // waits for its next time in town, till then: a forge or an anvil wanted
        std::unordered_map<uint64_t, Clock::time_point> skip;   // a craft the game turned down: not again till then
    };
    std::unordered_map<uint64_t, Due> g_bots;
    uint32_t g_timer = 0;

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
        const auto now = Clock::now();

        // What the game turned down for it lately is left alone a while.
        auto due = g_bots.find(bot->GetGUID().GetRawValue());
        auto skipped = [&](uint32_t spell, uint32_t entry)
        {
            if (due == g_bots.end())
                return false;
            auto s = due->second.skip.find(SkipKey(spell, entry));
            return s != due->second.skip.end() && s->second > now;
        };

        struct Cand
        {
            Kind             kind      = Kind::Make;
            Recipe const*    recipe    = nullptr;
            Access           access;
            uint32_t         spell     = 0;
            uint32_t         skillLine = 0;
            uint32_t         skill     = 0;
            uint32_t         entry     = 0;
            uint32_t         gapMs     = 0;
            ObjectGuid       target;
            YieldList const* yields    = nullptr;
            std::string      name;
        };
        std::vector<PbpCraft::Option> options;
        std::vector<Cand>             cands;
        std::vector<PbpCraft::Known>  known;            // every craft it knows, for what a point is short of
        std::unordered_map<uint32_t, Access> byFocus;   // worked out once each
        uint32_t usageChecks = 0;

        // An enchanter's spare gear: what it might break down, and practise
        // an enchant on first.
        const bool room = FreeSlots(bot) >= kBreakRoom;
        uint32_t skillNow = 0;
        const bool breaks = c.disenchant && room && KnowsBreak(bot, kDisenchantSpell, skillNow);
        std::vector<Piece> spare;
        if (breaks || (c.enchantOwnGear && bot->GetPureSkillValue(PbpCraft::Skill::Enchanting)))
            spare = SpareGear(bot, ai);

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
                if (r.learnAt > value || !bot->HasSpell(r.spell) || skipped(r.spell, 0))
                    continue;
                const uint32_t chance = PbpCraft::PointChance(value, r.yellow, r.grey);

                // An enchant goes on a piece of its own gear, and only ever for a point.
                Item* piece = nullptr;
                if (r.kind == Kind::Enchant)
                {
                    if (!c.enchantOwnGear || chance < c.minChance)
                        continue;
                    piece = PieceToEnchant(bot, r, sSpellMgr->GetSpellInfo(r.spell), spare);
                    if (!piece)
                        continue;
                }

                // What one takes, for what it is short of on its way to a point.
                {
                    PbpCraft::Known k;
                    k.pointChance = chance;
                    k.product     = r.product;
                    k.needs       = r.reagents;
                    for (uint32_t tool : r.tools)
                        k.needs.emplace_back(tool, 1);
                    for (uint32_t kind : r.toolKinds)
                        if (!bot->HasItemTotemCategory(kind))
                            if (auto kinds = g_toolsOfKind.find(kind); kinds != g_toolsOfKind.end())
                                for (uint32_t tool : kinds->second)
                                    k.needs.emplace_back(tool, 1);
                    known.push_back(std::move(k));
                }

                // Where it can make it.
                Access a;
                if (r.focus)
                {
                    auto found = byFocus.find(r.focus);
                    if (found == byFocus.end())
                    {
                        Access fresh;
                        fresh.inReach = FocusInReach(bot, r.focus);
                        if (!fresh.inReach && walk > 0.0f)
                            fresh.walkTo = StationToWalkTo(bot, r.focus, walk);
                        else if (!fresh.inReach)
                            fresh.putBy = StationToPutBy(bot, r.focus);
                        if (!fresh.inReach && !fresh.putBy && !fresh.walkTo && r.focus == kCookingFire)
                            fresh.fire = CanMakeFire(bot);
                        found = byFocus.emplace(r.focus, fresh).first;
                    }
                    a = found->second;
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
                o.pointChance = chance;
                o.canMake     = most;
                for (auto const& [item, need] : r.reagents)
                {
                    // Bought here: its price. Its own: what it would fetch.
                    auto offer = shop.find(item);
                    const uint64_t each =
                        offer != shop.end() ? UnitPrice(offer->second) : PlayerbotsPlus::ItemWorth(item);
                    o.costEach += each * need;
                }
                Cand cd;
                cd.kind      = r.kind;
                cd.recipe    = &r;
                cd.access    = a;
                cd.spell     = r.spell;
                cd.skillLine = r.skill;
                cd.skill     = value;
                cd.gapMs     = r.gapMs;
                if (r.kind == Kind::Enchant)
                {
                    o.sellable = false;
                    cd.target  = piece->GetGUID();
                    SpellInfo const* info = sSpellMgr->GetSpellInfo(r.spell);
                    cd.name = fmt::format("{} on {} {}", info && info->SpellName[0] ? info->SpellName[0] : "an enchant",
                                          piece->IsEquipped() ? "its" : "a spare", piece->GetTemplate()->Name1);
                }
                else
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(r.product);
                    o.sellable  = CouldSell(proto);
                    o.worthEach = o.sellable ? PlayerbotsPlus::ItemWorth(r.product) * r.makes : 0;
                    if (Wearable(proto) && proto->RequiredLevel <= bot->GetLevel() && usageChecks < c.usageChecks)
                    {
                        ++usageChecks;
                        const ItemUsage u = UsageOf(ai, r.product);
                        o.upgrade = u == ITEM_USAGE_EQUIP || u == ITEM_USAGE_REPLACE;
                    }
                    cd.name = proto ? proto->Name1 : std::to_string(r.product);
                }
                options.push_back(o);
                cands.push_back(std::move(cd));
            }
        }

        // Breaking things down: an enchanter its spare gear, a scribe its
        // herbs, a jewelcrafter its ore. Each by the game's own spell.
        if (breaks)
        {
            BreakAbility const& ab = g_breakAbility[kDisenchantSpell];
            const uint32_t pure   = bot->GetPureSkillValue(ab.skill);
            const uint32_t chance = PbpCraft::PointChance(pure, ab.yellow, ab.grey);
            SpellInfo const* info = sSpellMgr->GetSpellInfo(kDisenchantSpell);
            for (Piece const& piece : spare)
            {
                if (piece.need > skillNow || skipped(kDisenchantSpell, piece.item->GetEntry()))
                    continue;
                PbpCraft::Option o;
                o.spell       = kDisenchantSpell;
                o.pointChance = chance;
                o.canMake     = 1;
                o.costEach    = piece.keep;
                o.worthEach   = piece.broken;
                options.push_back(o);
                Cand cd;
                cd.kind      = Kind::Disenchant;
                cd.spell     = kDisenchantSpell;
                cd.skillLine = ab.skill;
                cd.skill     = pure;
                cd.entry     = piece.item->GetEntry();
                cd.gapMs     = info ? std::max(info->CalcCastTime(), info->StartRecoveryTime) + 300 : 3500;
                cd.target    = piece.item->GetGUID();
                cd.yields    = piece.yields;
                cd.name      = piece.item->GetTemplate()->Name1;
                cands.push_back(std::move(cd));
            }
        }
        auto stacks = [&](Kind kind, uint32_t spell, ItemFlags flag, bool on, bool points,
                          std::unordered_map<uint32_t, YieldList> const& table)
        {
            if (!on || !room || table.empty() || !KnowsBreak(bot, spell, skillNow))
                return;
            BreakAbility const& ab = g_breakAbility[spell];
            const uint32_t pure = bot->GetPureSkillValue(ab.skill);
            SpellInfo const* info = sSpellMgr->GetSpellInfo(spell);
            // Each herb or ore it has a stack of five or more of: how many goes.
            std::map<uint32_t, uint32_t> goes;
            ForEachBagItem(bot, [&](Item* item)
                           {
                               ItemTemplate const* p = item->GetTemplate();
                               if (p && p->HasFlag(flag) && !item->IsInTrade() && item->GetCount() >= 5 &&
                                   p->RequiredSkillRank <= skillNow && table.contains(p->ItemId))
                                   goes[p->ItemId] += item->GetCount() / 5;
                           });
            for (auto const& [entry, many] : goes)
            {
                if (skipped(spell, entry))
                    continue;
                ItemTemplate const* p = sObjectMgr->GetItemTemplate(entry);
                YieldList const& yields = table.at(entry);
                PbpCraft::Option o;
                o.spell       = spell;
                o.pointChance = points ? PbpCraft::GatherChance(pure, p->RequiredSkillRank) : 0;
                o.canMake     = std::min<uint32_t>(many, c.breakdownMaxCasts);
                o.costEach    = PlayerbotsPlus::ItemWorth(entry) * 5;
                o.worthEach   = WorthOf(yields);
                options.push_back(o);
                Cand cd;
                cd.kind      = kind;
                cd.spell     = spell;
                cd.skillLine = ab.skill;
                cd.skill     = pure;
                cd.entry     = entry;
                cd.gapMs     = info ? std::max(info->CalcCastTime(), info->StartRecoveryTime) + 300 : 1500;
                cd.yields    = &yields;
                cd.name      = p->Name1;
                cands.push_back(std::move(cd));
            }
        };
        stacks(Kind::Mill, kMillSpell, ITEM_FLAG_IS_MILLABLE, c.milling, sWorld->getBoolConfig(CONFIG_SKILL_MILLING),
               g_millYields);
        stacks(Kind::Prospect, kProspectSpell, ITEM_FLAG_IS_PROSPECTABLE, c.prospecting,
               sWorld->getBoolConfig(CONFIG_SKILL_PROSPECTING), g_prospectYields);

        // What a craft that would give it a point is short of, and how far
        // from that point: a thing that gets it there is worth doing for that.
        const std::unordered_map<uint32_t, uint32_t> wanted = PbpCraft::Wanted(known, have, c.minChance);
        if (!wanted.empty())
            for (size_t i = 0; i < options.size(); ++i)
            {
                Cand const& cd = cands[i];
                uint32_t steps = 0;
                auto nearer = [&](uint32_t item)
                {
                    auto w = wanted.find(item);
                    if (w != wanted.end() && (!steps || w->second < steps))
                        steps = w->second;
                };
                if (cd.kind == Kind::Make && cd.recipe)
                    nearer(cd.recipe->product);
                else if (cd.yields)
                    for (auto const& [item, many] : *cd.yields)
                        if (many >= 0.05)
                            nearer(item);
                options[i].steps = steps;
            }

        PbpCraft::ChoiceSettings cs;
        cs.maxCasts      = c.maxCasts;
        cs.minChance     = c.minChance;
        cs.sellMarginPct = c.sellMarginPct;
        // The kind of person it is may craft to sell before it crafts for a
        // point (its leaning's "sell": a greedy one).
        PbpCraft::Leaning const* leaning = PbpSkills_LeaningOf(bot);
        cs.sellFirst = leaning && leaning->sellFirst;
        const PbpCraft::Choice choice = PbpCraft::Choose(options, cs);
        plan.note = fmt::format("{} thing{} it could make or break down here and now{}", options.size(),
                                options.size() == 1 ? "" : "s",
                                plan.wantsTown ? "; more with a forge or an anvil, in town" : "");
        if (choice.why == PbpCraft::Why::None)
            return false;

        Cand const& cd = cands[choice.index];
        plan.kind      = cd.kind;
        plan.recipe    = cd.recipe;
        plan.spell     = cd.spell;
        plan.skillLine = cd.skillLine;
        plan.gapMs     = cd.gapMs;
        plan.focus     = cd.recipe ? cd.recipe->focus : 0;
        plan.entry     = cd.entry;
        plan.name      = cd.name;
        plan.why       = choice.why;
        plan.casts     = choice.casts;
        plan.access    = cd.access;
        plan.skill     = cd.skill;
        plan.chance    = options[choice.index].pointChance;
        if (cd.kind == Kind::Enchant)
            plan.targets = { cd.target };
        else if (cd.kind == Kind::Disenchant)
        {
            // This piece, and the others worth breaking down just as much, up
            // to as many as it does at one go: to sell, only those whose dust
            // is worth more than they are; for a point or for materials, the
            // ones worth no more kept than this one.
            plan.targets.push_back(cd.target);
            const uint64_t margin = std::max<uint32_t>(c.sellMarginPct, 100);
            for (size_t i = 0; i < cands.size() && plan.targets.size() < c.breakdownMaxCasts; ++i)
            {
                if (i == choice.index || cands[i].kind != Kind::Disenchant)
                    continue;
                PbpCraft::Option const& o = options[i];
                if (o.worthEach * 100 >= o.costEach * margin ||
                    (choice.why != PbpCraft::Why::Sell && o.costEach <= options[choice.index].costEach))
                    plan.targets.push_back(cands[i].target);
            }
            plan.casts = uint32_t(plan.targets.size());
            if (plan.casts > 1)
                plan.name += fmt::format(" and {} more", plan.casts - 1);
        }
        if (cd.recipe)
            ToBuy(bot, *cd.recipe, plan.casts, have, shop, plan.buy, plan.buyCopper);
        return true;
    }

    // Room in its bags for what a go gives (the game turns a craft down
    // without it; what will not fit of a breakdown goes in the post).
    bool RoomFor(Player* bot, Plan const& plan)
    {
        switch (plan.kind)
        {
            case Kind::Make:
            {
                if (!plan.recipe)
                    return false;
                ItemPosCountVec dest;
                return bot->CanStoreNewItem(NULL_BAG, NULL_SLOT, dest, plan.recipe->product, plan.recipe->makes) ==
                       EQUIP_ERR_OK;
            }
            case Kind::Enchant:
                return true;
            default:
                return FreeSlots(bot) >= kBreakRoom;
        }
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
        Kind              kind  = Kind::Make;
        uint32_t          spell = 0;
        uint32_t          left  = 0;
        uint32_t          gapMs = 0;
        uint32_t          focus = 0;
        uint32_t          entry = 0;              // Mill, Prospect: the herb or the ore
        std::vector<ObjectGuid> targets;          // Enchant: the piece; Disenchant: each in turn
        size_t            nextTarget = 0;
        bool              looting = false;        // broke something down: what it gave is to be taken
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

    // Everything out of the loot window breaking something down opened, and
    // the window shut -- the game's own handlers, as a player's client asks
    // them: what it gives is looted (and so its own), what will not fit goes
    // in the post, and the piece, or five of the stack, goes as it shuts.
    void TakeLoot(Player* bot)
    {
        const ObjectGuid from = bot->GetLootGUID();
        if (!from.IsItem())
            return;   // the cast came to nothing
        Item* item = bot->GetItemByGuid(from);
        const uint32_t slots = item ? uint32_t(item->loot.items.size()) : 0;
        for (uint32_t slot = 0; slot < slots && bot->GetLootGUID() == from; ++slot)
        {
            WorldPacket take(CMSG_AUTOSTORE_LOOT_ITEM, 1);
            take << uint8(slot);
            bot->GetSession()->HandleAutostoreLootItemOpcode(take);
        }
        if (bot->GetLootGUID() == from)
            bot->GetSession()->DoLootRelease(from);
    }

    void Release(Player* bot, PlayerbotAI* ai, Session const& s)
    {
        if (ai && !s.cityLife)
            ai->SetNextCheckDelay(0);
        if (!bot->IsInWorld())
            return;
        if (s.looting)
            TakeLoot(bot);   // cut short with the window open
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
        if (!RoomFor(bot, plan))
        {
            ++g_stats.noRoom;
            return fail("has no room in its bags for what it would get");
        }

        // Where it works first: nothing is bought for a station it cannot stand at.
        Station const* putBy = plan.focus && !plan.access.inReach ? plan.access.putBy : nullptr;
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
        s.kind     = plan.kind;
        s.spell    = plan.spell;
        s.left     = plan.casts;
        s.gapMs    = plan.gapMs;
        s.focus    = plan.focus;
        s.entry    = plan.entry;
        s.targets  = plan.targets;
        s.why      = plan.why;
        s.started  = Clock::now();
        s.cityLife = cityLife;

        ReadyToWork(bot, plan.spell);
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
            if (plan.focus && plan.access.fire && !plan.access.inReach)
                s.step = Step::Fire;
            else if (plan.focus)
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
            case PbpCraft::Why::Use:       ++g_stats.forUse;       break;
            case PbpCraft::Why::Skill:     ++g_stats.forSkill;     break;
            case PbpCraft::Why::Materials: ++g_stats.forMaterials; break;
            case PbpCraft::Why::Sell:      ++g_stats.forSale;      break;
            default: break;
        }
        const std::string what = fmt::format("{} {} x{} ({} {}, {}% a point, {})", KindName(plan.kind), plan.name,
                                             plan.casts, PbpCraft::Name(plan.skillLine), plan.skill, plan.chance,
                                             PbpCraft::WhyName(plan.why));
        if (report)
            *report = what +
                      (plan.buyCopper ? fmt::format(", having bought {}c of what it was short of", plan.buyCopper)
                                      : std::string()) +
                      (s.moved ? ", put by a station" : std::string()) +
                      (s.step == Step::Fire ? ", at a campfire it makes" : std::string());
        if (Cfg().logCrafts)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Crafting: {} {}{}.", bot->GetName(), what,
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
            case SPELL_FAILED_CANT_BE_DISENCHANTED:   return "cannot be disenchanted";
            case SPELL_FAILED_CANT_BE_PROSPECTED:     return "cannot be prospected";
            case SPELL_FAILED_CANT_BE_MILLED:         return "cannot be milled";
            case SPELL_FAILED_NEED_MORE_ITEMS:        return "fewer than five in the stack";
            case SPELL_FAILED_LOW_CASTLEVEL:          return "skill too low for it";
            case SPELL_FAILED_LOWLEVEL:               return "the piece is too low for the enchant";
            case SPELL_FAILED_HIGHLEVEL:              return "the piece is too high for the enchant";
            case SPELL_FAILED_EQUIPPED_ITEM_CLASS:    return "the enchant does not go on that";
            case SPELL_FAILED_ITEM_NOT_FOUND:         return "the piece is gone";
            default:                                  return fmt::format("SpellCastResult {}", uint32(r));
        }
    }

    void Failed(char const* what, SpellCastResult r)
    {
        ++g_stats.failed;
        ++g_failReasons[std::string(what) + CastFailText(r)];
    }

    // The game's own cast of a spell on one of its items.
    SpellCastResult CastOnItem(Player* bot, uint32_t spell, Item* item)
    {
        SpellInfo const* info = sSpellMgr->GetSpellInfo(spell);
        if (!info)
            return SPELL_FAILED_SPELL_UNAVAILABLE;
        SpellCastTargets targets;
        targets.SetItemTarget(item);
        return bot->CastSpell(targets, info, nullptr);
    }

    // What the next go works on: nullptr when there is nothing left.
    Item* NextTarget(Player* bot, Session& s)
    {
        switch (s.kind)
        {
            case Kind::Enchant:
                return s.targets.empty() ? nullptr : bot->GetItemByGuid(s.targets.front());
            case Kind::Disenchant:
                while (s.nextTarget < s.targets.size())
                    if (Item* item = bot->GetItemByGuid(s.targets[s.nextTarget++]))
                        if (!item->IsInTrade() && !item->IsEquipped())
                            return item;
                return nullptr;
            default:
            {
                // A stack of exactly five, split off a bigger one first as a
                // player might. Playerbots handles the loot window a breakdown
                // opens too, once its AI is back: it asks for what is no longer
                // in it and shuts it again, and for a herb or an ore the game
                // takes five off the stack whenever its window is shut
                // (WorldSession::DoLootRelease) -- for nothing, the second
                // time. With the five gone there is no stack left to take from.
                Item* five = nullptr;
                Item* more = nullptr;
                ForEachBagItem(bot, [&](Item* item)
                               {
                                   if (item->GetEntry() != s.entry || item->IsInTrade())
                                       return;
                                   if (item->GetCount() == 5 && !five)
                                       five = item;
                                   else if (item->GetCount() > 5 && !more)
                                       more = item;
                               });
                if (five || !more)
                    return five;
                uint16 to = 0;
                if (!EmptyPlaceFor(bot, more, to))
                    return nullptr;   // nowhere to put five by themselves
                bot->SplitItem(more->GetPos(), to, 5);
                Item* split = bot->GetItemByPos(to);
                return split && split != more && split->GetEntry() == s.entry && split->GetCount() == 5 ? split
                                                                                                    : nullptr;
            }
        }
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
        // What it broke down gave something: taken, and the window shut.
        if (s.looting)
        {
            s.looting = false;
            TakeLoot(bot);
        }
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

        SpellCastResult res = SPELL_CAST_OK;
        uint32_t        on  = 0;   // the thing it was cast on, for what is left alone after a no
        if (s.kind == Kind::Make)
            res = bot->CastSpell(bot, s.spell, false);
        else
        {
            Item* target = NextTarget(bot, s);
            if (!target)
                return stop(false);   // nothing left to work on
            if (s.kind != Kind::Enchant)
                on = target->GetEntry();
            res = CastOnItem(bot, s.spell, target);
            if (res == SPELL_CAST_OK && s.kind != Kind::Enchant)
                s.looting = true;
        }
        if (res != SPELL_CAST_OK)
        {
            Failed(s.kind == Kind::Make ? "" : s.kind == Kind::Enchant ? "enchant: " : "breakdown: ", res);
            // Not again for a while: asked every time, it is turned down every time.
            g_bots[bot->GetGUID().GetRawValue()].skip[SkipKey(s.spell, on)] = now + kSkipFor;
            return stop(false);
        }
        --s.left;
        ++g_stats.casts;
        switch (s.kind)
        {
            case Kind::Enchant:    ++g_stats.enchants;    break;
            case Kind::Disenchant: ++g_stats.disenchants; break;
            case Kind::Mill:       ++g_stats.millings;    break;
            case Kind::Prospect:   ++g_stats.prospects;   break;
            default: break;
        }
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

    // City life's plans it never got round to; gear bought to break down that never was.
    for (auto it = g_cityPlans.begin(); it != g_cityPlans.end();)
        it = now > it->second.until ? g_cityPlans.erase(it) : std::next(it);
    for (auto it = g_toBreak.begin(); it != g_toBreak.end();)
        it = now > it->second ? g_toBreak.erase(it) : std::next(it);

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
    if (!RoomFor(bot, cp.plan))
    {
        ++g_stats.noRoom;
        return false;
    }
    if (cp.plan.focus && !cp.plan.access.inReach)
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
        if (!FocusInReach(bot, cp.plan.focus))
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
    if (it->second.looting && bot->IsInWorld())
        TakeLoot(bot);   // broke something down, and had not taken what it gave
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
        report = fmt::format("{}: nothing worth doing ({}).", bot->GetName(), plan.note);
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
                                  "upgrade, {} for skill, {} for materials, {} to sell), {} crafts; {} looked at "
                                  "with nothing worth doing.",
                                  on ? "on" : "OFF", s.working, s.sessions, s.cityLife, s.forUse, s.forSkill,
                                  s.forMaterials, s.forSale, s.casts, s.nothing));
    if (full)
    {
        b.lines.push_back(fmt::format("Of the crafts: {} pieces of gear disenchanted, {} lots of herbs milled, {} of "
                                      "ore prospected, {} enchants put on their own gear; {} bots taught Disenchant, "
                                      "Milling or Prospecting their skill said they should know; {} pieces bought "
                                      "to disenchant waiting. The game lists what {} kinds of gear disenchant "
                                      "into, {} herbs mill into and {} ores prospect into.",
                                      s.disenchants, s.millings, s.prospects, s.enchants, s.taught, g_toBreak.size(),
                                      g_disenchantYields.size(), g_millYields.size(), g_prospectYields.size()));
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

void PbpCrafting_Load()
{
    const uint32 start = getMSTime();
    g_disenchantYields.clear();
    g_millYields.clear();
    g_prospectYields.clear();

    // A loot table's rows as the core reads them (LootStore::LoadLootTable),
    // leaving out what only someone on a quest gets and what only a hard
    // mode gives. `where`: "" for all of it.
    auto read = [](char const* table, std::string const& where)
    {
        PbpCraft::LootTables out;
        QueryResult result = WorldDatabase.Query(
            "SELECT Entry, Item, Reference, Chance, QuestRequired, LootMode, GroupId, MinCount, MaxCount FROM {}{} "
            "ORDER BY Entry, Item",
            table, where);
        if (!result)
            return out;
        do
        {
            Field* f = result->Fetch();
            if (f[4].Get<bool>() || !(f[5].Get<uint16>() & LOOT_MODE_DEFAULT))
                continue;
            PbpCraft::LootRow row;
            row.item      = f[1].Get<uint32>();
            row.reference = uint32(std::abs(f[2].Get<int32>()));
            row.chance    = f[3].Get<float>();
            row.group     = f[6].Get<uint8>();
            row.minCount  = f[7].Get<uint8>();
            row.maxCount  = f[8].Get<uint8>();
            out[f[0].Get<uint32>()].push_back(row);
        } while (result->NextRow());
        return out;
    };
    const PbpCraft::LootTables disenchant = read("disenchant_loot_template", "");
    const PbpCraft::LootTables milling    = read("milling_loot_template", "");
    const PbpCraft::LootTables prospect   = read("prospecting_loot_template", "");

    // The tables those refer to, and the ones those refer to in turn: only
    // these few of the many there are.
    PbpCraft::LootTables references;
    std::unordered_set<uint32_t> wanted;
    auto refer = [&](PbpCraft::LootTables const& tables)
    {
        for (auto const& [entry, rows] : tables)
            for (PbpCraft::LootRow const& row : rows)
                if (row.reference && !references.contains(row.reference))
                    wanted.insert(row.reference);
    };
    refer(disenchant);
    refer(milling);
    refer(prospect);
    for (int round = 0; round < 4 && !wanted.empty(); ++round)
    {
        std::string in;
        for (uint32_t entry : wanted)
            in += (in.empty() ? "" : ",") + std::to_string(entry);
        const PbpCraft::LootTables more = read("reference_loot_template", " WHERE Entry IN (" + in + ")");
        wanted.clear();
        for (auto const& [entry, rows] : more)
            references[entry] = rows;
        refer(more);
    }

    auto yields = [&](PbpCraft::LootTables const& tables, std::unordered_map<uint32_t, YieldList>& into)
    {
        for (auto const& [entry, rows] : tables)
        {
            YieldList list;
            for (auto const& [item, many] : PbpCraft::Expected(rows, references))
                if (many > 0.0 && sObjectMgr->GetItemTemplate(item))
                    list.emplace_back(item, many);
            std::sort(list.begin(), list.end());
            if (!list.empty())
                into[entry] = std::move(list);
        }
    };
    yields(disenchant, g_disenchantYields);
    yields(milling, g_millYields);
    yields(prospect, g_prospectYields);
    LOG_INFO("server.loading", ">> Playerbots Plus: what {} kinds of gear disenchant into, {} herbs mill into and {} "
             "ores prospect into, in {} ms",
             g_disenchantYields.size(), g_millYields.size(), g_prospectYields.size(), GetMSTimeDiffToNow(start));
}

bool PbpCrafting_CanDisenchant(Player* bot, uint32_t itemEntry)
{
    ItemTemplate const* p = sObjectMgr->GetItemTemplate(itemEntry);
    if (!bot || !p || !g_Pbp.enable || !Cfg().enable || !Cfg().disenchant ||
        (p->Class != ITEM_CLASS_WEAPON && p->Class != ITEM_CLASS_ARMOR) || !p->DisenchantID ||
        p->Quality < ITEM_QUALITY_UNCOMMON ||
        p->Quality > std::min<uint32_t>(Cfg().disenchantMaxQuality, ITEM_QUALITY_EPIC) ||
        !g_disenchantYields.contains(p->DisenchantID))
        return false;
    ReadRecipes();
    uint32_t skill = 0;
    return KnowsBreak(bot, kDisenchantSpell, skill) && p->RequiredDisenchantSkill <= skill;
}

uint64_t PbpCrafting_DisenchantWorth(uint32_t itemEntry)
{
    ItemTemplate const* p = sObjectMgr->GetItemTemplate(itemEntry);
    if (!p || !p->DisenchantID)
        return 0;
    auto yields = g_disenchantYields.find(p->DisenchantID);
    return yields == g_disenchantYields.end() ? 0 : WorthOf(yields->second);
}

void PbpCrafting_BoughtToBreak(Player* bot, Item* item)
{
    if (!bot || !item)
        return;
    g_toBreak[item->GetGUID().GetCounter()] = Clock::now() + kToBreakFor;
    // Looked at as soon as it is free, not when its turn comes round.
    Due& due = g_bots[bot->GetGUID().GetRawValue()];
    due.scheduled = true;
    due.next      = Clock::now();
}
