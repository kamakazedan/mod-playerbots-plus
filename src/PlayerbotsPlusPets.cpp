#include "PlayerbotsPlusPets.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusUtil.h"

#include "CellImpl.h"
#include "CharacterDatabase.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "Group.h"
#include "ItemTemplate.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Pet.h"
#include "Player.h"
#include "SpellInfo.h"
#include "SpellMgr.h"
#include "World.h"

#include "RandomPlayerbotMgr.h"

#include <algorithm>
#include <chrono>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    PbpPetsStats g_stats;

    // A number that is the same for a bot or a pet every session, so that what
    // it is like -- names its pets or not, owns companions or not -- does not
    // change from one day to the next.
    uint32_t Mix(uint64_t value, uint32_t salt)
    {
        uint64_t x = value * 0x9E3779B97F4A7C15ull + (uint64_t(salt) + 1) * 0xC2B2AE3D27D4EB4Full;
        x ^= x >> 31;
        x *= 0xBF58476D1CE4E5B9ull;
        x ^= x >> 29;
        return uint32_t(x);
    }

    bool OneOfTheRandomBots(Player* bot)
    {
        return bot && bot->IsInWorld() && PbpIsBotPlayer(bot) && sRandomPlayerbotMgr.IsRandomBot(bot);
    }

    // ---- hunter pets' names ----------------------------------------------------

    // PlayerbotsPlus.Pets.Names.<key>, lower case, for a pet family.
    char const* FamilyKey(uint32_t family)
    {
        switch (family)
        {
            case CREATURE_FAMILY_WOLF:         return "wolf";
            case CREATURE_FAMILY_CAT:          return "cat";
            case CREATURE_FAMILY_SPIDER:       return "spider";
            case CREATURE_FAMILY_BEAR:         return "bear";
            case CREATURE_FAMILY_BOAR:         return "boar";
            case CREATURE_FAMILY_CROCOLISK:    return "crocolisk";
            case CREATURE_FAMILY_CARRION_BIRD: return "carrionbird";
            case CREATURE_FAMILY_CRAB:         return "crab";
            case CREATURE_FAMILY_GORILLA:      return "gorilla";
            case CREATURE_FAMILY_RAPTOR:       return "raptor";
            case CREATURE_FAMILY_TALLSTRIDER:  return "tallstrider";
            case CREATURE_FAMILY_SCORPID:      return "scorpid";
            case CREATURE_FAMILY_TURTLE:       return "turtle";
            case CREATURE_FAMILY_BAT:          return "bat";
            case CREATURE_FAMILY_HYENA:        return "hyena";
            case CREATURE_FAMILY_BIRD_OF_PREY: return "owl";
            case CREATURE_FAMILY_WIND_SERPENT: return "windserpent";
            case CREATURE_FAMILY_DRAGONHAWK:   return "dragonhawk";
            case CREATURE_FAMILY_RAVAGER:      return "ravager";
            case CREATURE_FAMILY_WARP_STALKER: return "warpstalker";
            case CREATURE_FAMILY_SPOREBAT:     return "sporebat";
            case CREATURE_FAMILY_NETHER_RAY:   return "netherray";
            case CREATURE_FAMILY_SERPENT:      return "serpent";
            case CREATURE_FAMILY_MOTH:         return "moth";
            case CREATURE_FAMILY_CHIMAERA:     return "chimaera";
            case CREATURE_FAMILY_DEVILSAUR:    return "devilsaur";
            case CREATURE_FAMILY_SILITHID:     return "silithid";
            case CREATURE_FAMILY_WORM:         return "worm";
            case CREATURE_FAMILY_RHINO:        return "rhino";
            case CREATURE_FAMILY_WASP:         return "wasp";
            case CREATURE_FAMILY_CORE_HOUND:   return "corehound";
            case CREATURE_FAMILY_SPIRIT_BEAST: return "spiritbeast";
            default:                           return "";
        }
    }

    // The name a tamed beast comes with: its family's ("Cat", "Wind Serpent"),
    // as Pet::CreateBaseAtCreatureInfo gives it -- or, with no family, its own.
    bool CameWithName(Pet* pet, CreatureTemplate const* tmpl)
    {
        const std::string& name = pet->GetName();
        if (CreatureFamilyEntry const* family = sCreatureFamilyStore.LookupEntry(tmpl->family))
        {
            char const* familyName = family->Name[sWorld->GetDefaultDbcLocale()];
            if (familyName && name == familyName)
                return true;
        }
        return name == tmpl->Name;
    }

    // Pets left with the name they came with, on purpose or because nothing
    // on the lists would do: not looked at again this session.
    std::unordered_set<uint32_t> g_leftAlone;   // pet numbers
    Clock::time_point g_nextNaming{};

    // What WorldSession::HandlePetRename does when a player renames a pet,
    // in the same order and with the same checks; the declined names it takes
    // from a client are not given.
    bool Rename(Player* bot, Pet* pet, std::string const& name)
    {
        PetStable* stable = bot->GetPetStable();
        CharmInfo* charm  = pet->GetCharmInfo();
        if (!charm || !stable || !stable->CurrentPet || stable->CurrentPet->PetNumber != charm->GetPetNumber())
            return false;
        if (ObjectMgr::CheckPetName(name) != PET_NAME_SUCCESS)
            return false;

        pet->SetName(name);
        if (bot->GetGroup())
            bot->SetGroupUpdateFlag(GROUP_UPDATE_FLAG_PET_NAME);
        pet->RemoveByteFlag(UNIT_FIELD_BYTES_2, 2, UNIT_CAN_BE_RENAMED);
        stable->CurrentPet->Name       = name;
        stable->CurrentPet->WasRenamed = true;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_UPD_CHAR_PET_NAME);
        stmt->SetData(0, name);
        stmt->SetData(1, bot->GetGUID().GetCounter());
        stmt->SetData(2, charm->GetPetNumber());
        CharacterDatabase.Execute(stmt);

        // The client asks for the name again.
        pet->SetUInt32Value(UNIT_FIELD_PET_NAME_TIMESTAMP, uint32(GameTime::GetGameTime().count()));
        return true;
    }

    // A random bot's hunter whose pet still has the name it came with: a name
    // from its family's list, or anyone's, the same one each time for the same
    // pet. Pets.NamesPerMinute at most, the realm over.
    void NamePets(Clock::time_point now)
    {
        if (!g_PetNamesEnable || g_PetNames.empty() || now < g_nextNaming)
            return;
        constexpr uint32_t kEverySeconds = 15;
        g_nextNaming = now + std::chrono::seconds(kEverySeconds);
        uint32_t budget = std::max<uint32_t>(1, g_PetNamesPerMinute * kEverySeconds / 60);

        static std::vector<std::string> const none;
        auto names = [](std::string const& key) -> std::vector<std::string> const&
        {
            auto it = g_PetNames.find(key);
            return it == g_PetNames.end() ? none : it->second;
        };

        for (auto const& [guid, bot] : ObjectAccessor::GetPlayers())
        {
            if (!budget)
                break;
            if (!bot || bot->getClass() != CLASS_HUNTER || !OneOfTheRandomBots(bot))
                continue;
            Pet* pet = bot->GetPet();
            if (!pet || !pet->IsInWorld() || pet->getPetType() != HUNTER_PET || !pet->GetCharmInfo() ||
                !pet->HasByteFlag(UNIT_FIELD_BYTES_2, 2, UNIT_CAN_BE_RENAMED))
                continue;
            CreatureTemplate const* tmpl = pet->GetCreatureTemplate();
            const uint32_t number = pet->GetCharmInfo()->GetPetNumber();
            // Still the name it came with: never one somebody gave it (playerbots'
            // own "rename pet" leaves the game's renamed flag alone).
            if (!tmpl || !CameWithName(pet, tmpl) || g_leftAlone.count(number))
                continue;

            if (Mix(number, 1) % 100 >= g_PetNameChance)
            {
                g_leftAlone.insert(number);   // the kind who never bothers
                ++g_stats.keptName;
                continue;
            }

            std::vector<std::string> const& own = names(FamilyKey(tmpl->family));
            std::vector<std::string> const& any = names("any");
            const size_t total = own.size() + any.size();
            bool named = false;
            for (uint32_t tries = 0; total && tries < 6 && !named; ++tries)
            {
                // Mostly its family's names; anyone's now and then.
                const uint32_t pick = Mix(number, 2 + tries);
                const bool fromOwn = !own.empty() && (any.empty() || pick % 100 < 75);
                std::vector<std::string> const& list = fromOwn ? own : any;
                if (list.empty())
                    continue;
                named = Rename(bot, pet, list[(pick / 100) % list.size()]);
            }
            if (!named)
            {
                g_leftAlone.insert(number);
                continue;
            }
            --budget;
            ++g_stats.named;
            if (g_PbpDebug)
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} named its {} {}.", bot->GetName(), tmpl->Name,
                         pet->GetName());
        }
    }

    // ---- companions ------------------------------------------------------------

    struct Companion
    {
        uint32_t spell    = 0;
        uint32_t minLevel = 1;
        uint32_t weight   = 1;
        uint8_t  side     = 0;
    };
    std::vector<Companion> g_catalog;
    bool g_catalogBuilt = false;

    // The companions on the lists, by the spell their item teaches. Built on
    // first use: the item and spell stores are not loaded yet when the
    // settings first are.
    void BuildCatalog()
    {
        g_catalog.clear();
        g_stats.badEntries = 0;
        std::unordered_set<uint32_t> seen;
        for (PbpCompanionEntry const& e : g_Companions)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(e.item);
            uint32_t spell = 0;
            if (proto && proto->Class == ITEM_CLASS_MISC && proto->SubClass == ITEM_SUBCLASS_JUNK_PET &&
                (proto->Spells[0].SpellId == 55884 || proto->Spells[0].SpellId == 483))
                spell = proto->Spells[1].SpellId;
            SpellInfo const* info = spell ? sSpellMgr->GetSpellInfo(spell) : nullptr;
            if (!info || !info->HasEffect(SPELL_EFFECT_SUMMON) || !seen.insert(spell).second)
            {
                ++g_stats.badEntries;
                continue;
            }
            Companion c;
            c.spell    = spell;
            c.minLevel = e.minLevel;
            c.weight   = e.weight;
            c.side     = e.side;
            g_catalog.push_back(c);
        }
        g_stats.companions = uint32_t(g_catalog.size());
        g_catalogBuilt     = true;
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Companions: {} on the lists{}.", g_catalog.size(),
                 g_stats.badEntries ? fmt::format(", {} left out (not a companion's item)", g_stats.badEntries)
                                    : std::string());
    }

    struct BotPets
    {
        bool              taught = false;   // given what it owns, this session
        bool              out    = false;   // has one out while somebody is about
        Clock::time_point rollAt{};         // whether it does is decided again then
        Clock::time_point seen{};
    };
    std::unordered_map<uint64_t, BotPets> g_bots;
    Clock::time_point g_nextCompanions{};
    Clock::time_point g_nextSweep{};

    // Most bots own a common companion or two, bought from a pet seller of
    // their side; a few collect them, the rare ones too, more as they level.
    void Teach(Player* bot)
    {
        const uint32_t level = bot->GetLevel();
        const uint64_t guid  = bot->GetGUID().GetRawValue();
        if (level < 5 || Mix(guid, 11) % 100 >= g_CompanionsOwnChance)
            return;
        const bool collector = Mix(guid, 12) % 100 < g_CompanionsCollectorChance;
        const uint32_t want = collector ? 2 + (level >= 30) + (level >= 55) + (level >= 70) + (level >= 78)
                                        : 1 + (level >= 40);
        const uint8_t side = bot->GetTeamId() == TEAM_ALLIANCE ? 1 : 2;

        uint32_t have = 0;
        std::vector<Companion const*> pool;
        for (Companion const& c : g_catalog)
        {
            if (bot->HasSpell(c.spell))
            {
                ++have;
                continue;
            }
            if ((c.side && c.side != side) || c.minLevel > level || (!collector && c.weight < g_CompanionsCommonWeight))
                continue;
            pool.push_back(&c);
        }
        while (have < want && !pool.empty())
        {
            uint32_t total = 0;
            for (Companion const* c : pool)
                total += c->weight;
            uint32_t roll = urand(1, total);
            size_t at = 0;
            while (at + 1 < pool.size() && roll > pool[at]->weight)
                roll -= pool[at++]->weight;
            bot->learnSpell(pool[at]->spell);
            pool.erase(pool.begin() + at);
            ++have;
            ++g_stats.learned;
        }
    }

    void BringOneOut(Player* bot, BotPets& s, Clock::time_point now)
    {
        if (now >= s.rollAt)
        {
            s.out    = urand(1, 100) <= g_CompanionsOutChance;
            s.rollAt = now + std::chrono::minutes(urand(20, 40));
        }
        if (!s.out || !bot->GetCritterGUID().IsEmpty())
            return;
        // Not on a mount or a bench, in a fight, mid-cast or going anywhere by air.
        if (!bot->IsAlive() || bot->IsInCombat() || bot->IsMounted() || bot->IsInFlight() || bot->IsSitState() ||
            bot->GetVehicle() || bot->HasUnitState(UNIT_STATE_CASTING) || bot->isAFK())
            return;
        std::vector<uint32_t> owned;
        for (Companion const& c : g_catalog)
            if (bot->HasSpell(c.spell))
                owned.push_back(c.spell);
        if (owned.empty())
            return;
        bot->CastSpell(bot, owned[urand(0, uint32_t(owned.size() - 1))], true);
        if (!bot->GetCritterGUID().IsEmpty())
            ++g_stats.summoned;
    }

    // Every Companions.CheckSeconds: the random bots near a real player, out
    // in the world. Nobody else's are worth the trouble.
    void Companions(Clock::time_point now)
    {
        if (!g_CompanionsEnable || now < g_nextCompanions)
            return;
        g_nextCompanions = now + std::chrono::seconds(g_CompanionsCheckSeconds);
        if (!g_catalogBuilt)
            BuildCatalog();
        if (g_catalog.empty())
            return;

        std::unordered_set<uint64_t> looked;
        for (auto const& [guid, real] : ObjectAccessor::GetPlayers())
        {
            if (!real || !real->IsInWorld() || PbpIsBotPlayer(real))
                continue;
            Map* map = real->GetMap();
            if (!map || map->Instanceable())   // dungeons, raids, battlegrounds, arenas
                continue;
            std::list<Player*> near;
            Acore::AnyPlayerInObjectRangeCheck check(real, g_CompanionsRange, true, true);
            Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(real, near, check);
            Cell::VisitObjects(real, searcher, g_CompanionsRange);
            for (Player* bot : near)
            {
                if (bot == real || !OneOfTheRandomBots(bot) || !looked.insert(bot->GetGUID().GetRawValue()).second)
                    continue;
                BotPets& s = g_bots[bot->GetGUID().GetRawValue()];
                s.seen = now;
                if (!s.taught)
                {
                    s.taught = true;
                    Teach(bot);
                }
                BringOneOut(bot, s, now);
            }
        }

        if (now >= g_nextSweep)
        {
            g_nextSweep = now + std::chrono::minutes(10);
            for (auto it = g_bots.begin(); it != g_bots.end();)
                it = (now - it->second.seen > std::chrono::hours(2)) ? g_bots.erase(it) : std::next(it);
        }
    }
}

void PbpPets_Update(uint32_t /*diff*/)
{
    const Clock::time_point now = Clock::now();
    NamePets(now);
    Companions(now);
}

void PbpPets_Reset()
{
    g_leftAlone.clear();
    g_catalog.clear();
    g_catalogBuilt = false;
    g_bots.clear();
}

PbpPetsStats PbpPets_GetStats()
{
    return g_stats;
}
