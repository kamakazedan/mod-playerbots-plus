#include "PlayerbotsPlusPets.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusText.h"
#include "PlayerbotsPlusUtil.h"

#include "CellImpl.h"
#include "CharacterDatabase.h"
#include "DBCStores.h"
#include "DatabaseEnv.h"
#include "GameTime.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "GameEventMgr.h"
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
#include "TemporarySummon.h"
#include "World.h"

#include "NewRpgInfo.h"
#include "PlayerbotAI.h"
#include "RandomPlayerbotMgr.h"

#include <algorithm>
#include <array>
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
        uint8_t  holiday  = 0;   // PbpHolidayKey of the holiday it comes from, 0 none
    };
    std::vector<Companion> g_catalog;
    std::unordered_set<uint32_t> g_ourSpells;
    bool g_catalogBuilt = false;

    // Each PbpHolidayKey, in its order: on right now? Looked at once a check.
    constexpr size_t kHolidays = 11;
    std::array<bool, kHolidays> g_season{};

    bool HolidayOn(uint8_t key)
    {
        switch (key)
        {
            case 1:  return IsHolidayActive(HOLIDAY_FEAST_OF_WINTER_VEIL);
            case 2:  return IsHolidayActive(HOLIDAY_HALLOWS_END);
            case 3:  return IsHolidayActive(HOLIDAY_FIRE_FESTIVAL);
            case 4:  return IsHolidayActive(HOLIDAY_LOVE_IS_IN_THE_AIR);
            case 5:  return IsHolidayActive(HOLIDAY_NOBLEGARDEN);
            case 6:  return IsHolidayActive(HOLIDAY_CHILDRENS_WEEK);
            case 7:  return IsHolidayActive(HOLIDAY_BREWFEST);
            case 8:  return IsHolidayActive(HOLIDAY_PILGRIMS_BOUNTY);
            case 9:  return IsHolidayActive(HOLIDAY_LUNAR_FESTIVAL);
            case 10: return IsHolidayActive(HOLIDAY_DARKMOON_FAIRE_ELWYNN) ||
                            IsHolidayActive(HOLIDAY_DARKMOON_FAIRE_THUNDER) ||
                            IsHolidayActive(HOLIDAY_DARKMOON_FAIRE_SHATTRATH);
            default: return false;
        }
    }

    bool InSeason(Companion const& c)
    {
        return c.holiday && c.holiday < kHolidays && g_season[c.holiday];
    }

    // The companions on the lists, by the spell their item teaches. Built on
    // first use: the item and spell stores are not loaded yet when the
    // settings first are. A companion item is a misc item that teaches a
    // summoning spell, whatever its subclass: the holidays' are not all
    // filed as pets.
    void BuildCatalog()
    {
        g_catalog.clear();
        g_ourSpells.clear();
        g_stats.badEntries = 0;
        std::unordered_set<uint64_t> seen;
        for (PbpCompanionEntry const& e : g_Companions)
        {
            ItemTemplate const* proto = sObjectMgr->GetItemTemplate(e.item);
            uint32_t spell = 0;
            if (proto && proto->Class == ITEM_CLASS_MISC &&
                (proto->Spells[0].SpellId == 55884 || proto->Spells[0].SpellId == 483))
                spell = proto->Spells[1].SpellId;
            SpellInfo const* info = spell ? sSpellMgr->GetSpellInfo(spell) : nullptr;
            // One side's and the other's can be the same companion (the Argent Tournament's wyrmling).
            if (!info || !info->HasEffect(SPELL_EFFECT_SUMMON) || !seen.insert((uint64_t(spell) << 2) | e.side).second)
            {
                ++g_stats.badEntries;
                continue;
            }
            Companion c;
            c.spell    = spell;
            c.minLevel = e.minLevel;
            c.weight   = e.weight;
            c.side     = e.side;
            c.holiday  = e.holiday;
            g_catalog.push_back(c);
            g_ourSpells.insert(spell);
        }
        g_stats.companions = uint32_t(g_catalog.size());
        g_catalogBuilt     = true;
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Companions: {} on the lists{}.", g_catalog.size(),
                 g_stats.badEntries ? fmt::format(", {} left out (not a companion's item)", g_stats.badEntries)
                                    : std::string());
    }

    struct BotPets
    {
        bool              taught    = false;   // given what it owns, this session
        bool              out       = false;   // has one out while somebody is about
        Clock::time_point rollAt{};            // whether it does is decided again then
        Clock::time_point seen{};
        bool              idle      = false;   // in a stretch with nothing on, now
        bool              switchDue = false;   // ...and swaps its companion for another once in it
        Clock::time_point switchAt{};
    };
    std::unordered_map<uint64_t, BotPets> g_bots;
    Clock::time_point g_nextCompanions{};
    Clock::time_point g_nextSweep{};

    uint8_t SideOf(Player* bot)
    {
        return bot->GetTeamId() == TEAM_ALLIANCE ? 1 : 2;
    }

    bool Fits(Companion const& c, uint32_t level, uint8_t side)
    {
        return (!c.side || c.side == side) && c.minLevel <= level;
    }

    template <class Weigh>
    Companion const* PickWeighted(std::vector<Companion const*> const& from, Weigh weigh)
    {
        uint64_t total = 0;
        for (Companion const* c : from)
            total += weigh(*c);
        if (!total)
            return nullptr;
        uint64_t roll = urand(1, uint32_t(std::min<uint64_t>(total, 0xFFFFFFFFull)));
        for (Companion const* c : from)
        {
            const uint64_t w = weigh(*c);
            if (roll <= w)
                return c;
            roll -= w;
        }
        return from.back();
    }

    // Most bots own a common companion or two, bought from a pet seller of
    // their side; a few collect them, the rare ones too, more as they level.
    // In a holiday its own are on offer: about half of the owners have picked
    // one up as well. Only what suits its side and level, ever.
    void Teach(Player* bot)
    {
        const uint32_t level = bot->GetLevel();
        const uint64_t guid  = bot->GetGUID().GetRawValue();
        if (level < 8 || Mix(guid, 11) % 100 >= g_CompanionsOwnChance)
            return;
        const bool collector = Mix(guid, 12) % 100 < g_CompanionsCollectorChance;
        const uint32_t want = collector ? 2 + (level >= 30) + (level >= 55) + (level >= 70) + (level >= 78)
                                        : 1 + (level >= 40);
        const uint8_t side = SideOf(bot);

        uint32_t have = 0;
        bool ownsSeasonal = false;
        std::vector<Companion const*> pool, seasonal;
        for (Companion const& c : g_catalog)
        {
            if (!Fits(c, level, side))
                continue;
            if (bot->HasSpell(c.spell))
            {
                ++have;
                ownsSeasonal = ownsSeasonal || InSeason(c);
                continue;
            }
            if (InSeason(c))
                seasonal.push_back(&c);
            if (collector || c.weight >= g_CompanionsCommonWeight)
                pool.push_back(&c);
        }
        auto weigh = [](Companion const& c) { return uint64_t(c.weight) * (InSeason(c) ? g_CompanionsHolidayBoost : 1); };
        while (have < want && !pool.empty())
        {
            Companion const* pick = PickWeighted(pool, weigh);
            if (!pick)
                break;
            bot->learnSpell(pick->spell);
            ownsSeasonal = ownsSeasonal || InSeason(*pick);
            seasonal.erase(std::remove(seasonal.begin(), seasonal.end(), pick), seasonal.end());
            pool.erase(std::remove(pool.begin(), pool.end(), pick), pool.end());
            ++have;
            ++g_stats.learned;
        }
        if (!ownsSeasonal && !seasonal.empty() && Mix(guid, 13) % 100 < 50)
            if (Companion const* pick = PickWeighted(seasonal, weigh))
            {
                bot->learnSpell(pick->spell);
                ++g_stats.learned;
            }
    }

    // What it owns that it could have out: on the lists, its side's, and
    // had at its level.
    std::vector<Companion const*> Owned(Player* bot)
    {
        std::vector<Companion const*> owned;
        const uint32_t level = bot->GetLevel();
        const uint8_t  side  = SideOf(bot);
        for (Companion const& c : g_catalog)
            if (Fits(c, level, side) && bot->HasSpell(c.spell))
                owned.push_back(&c);
        return owned;
    }

    // Which one it brings out: a holiday's own far more in its season, and
    // far less out of it.
    uint64_t OutWeight(Companion const& c)
    {
        if (!c.holiday)
            return 4;
        return InSeason(c) ? 4ull * g_CompanionsHolidayBoost : 1;
    }

    // The companion it has out, when it is one of ours.
    Creature* OurCritter(Player* bot)
    {
        const ObjectGuid guid = bot->GetCritterGUID();
        if (guid.IsEmpty())
            return nullptr;
        Creature* critter = ObjectAccessor::GetCreature(*bot, guid);
        if (!critter || !critter->IsSummon() || !g_ourSpells.count(critter->GetUInt32Value(UNIT_CREATED_BY_SPELL)))
            return nullptr;
        return critter;
    }

    void PutAway(Player* bot)
    {
        if (Creature* critter = OurCritter(bot))
        {
            critter->ToTempSummon()->UnSummon();
            ++g_stats.putAway;
        }
    }

    // Its first companion earns it Can I Keep Him?, and the game tells
    // everyone in /say range. A crowd of bots learning theirs in front of
    // someone is a crowd of achievements: a bot learns its companions out of
    // earshot of everyone real instead.
    bool Overheard(Player* bot)
    {
        const float range = sWorld->getFloatConfig(CONFIG_LISTEN_RANGE_SAY) + 15.0f;
        std::list<Player*> near;
        Acore::AnyPlayerInObjectRangeCheck check(bot, range, false, true);
        Acore::PlayerListSearcher<Acore::AnyPlayerInObjectRangeCheck> searcher(bot, near, check);
        Cell::VisitObjects(bot, searcher, range);
        for (Player* p : near)
            if (p != bot && !PbpIsBotPlayer(p))
                return true;
        return false;
    }

    // Nothing on in playerbots' own terms, or hanging out somewhere.
    bool NothingOn(Player* bot)
    {
        if (!bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight() || bot->GetVehicle())
            return false;
        std::string spot, doing;
        bool away = false, justBack = false;
        if (PbpHangout_Get(bot, spot, doing, away, justBack))
            return true;
        PlayerbotAI* ai = PbpAiOf(bot);
        if (!ai)
            return false;
        auto const& data = ai->rpgInfo.data;
        return std::holds_alternative<NewRpgInfo::Idle>(data) || std::holds_alternative<NewRpgInfo::Rest>(data) ||
               std::holds_alternative<NewRpgInfo::WanderNpc>(data);
    }

    // Whether it has one out, which, and once in a stretch with nothing on,
    // maybe a different one.
    void Tend(Player* bot, BotPets& s, Clock::time_point now)
    {
        // Out or not is decided every 20 to 40 minutes; one that is not
        // puts its companion away, the way players do.
        if (now >= s.rollAt)
        {
            const uint32_t chance = std::min<uint32_t>(
                100, g_CompanionsOutChance * 100 / std::max<uint32_t>(1, g_CompanionsOwnChance));
            s.out    = urand(1, 100) <= chance;
            s.rollAt = now + std::chrono::minutes(urand(20, 40));
            if (!s.out)
                PutAway(bot);
        }

        const bool idle = NothingOn(bot);
        if (idle && !s.idle)
        {
            s.switchDue = g_CompanionsSwitchChance && urand(1, 100) <= g_CompanionsSwitchChance;
            s.switchAt  = now + std::chrono::seconds(urand(60, 300));
        }
        else if (!idle)
            s.switchDue = false;
        s.idle = idle;

        if (!s.out)
            return;
        // Not in a fight, mid-cast, going anywhere by air, sat down or AFK.
        if (!bot->IsAlive() || bot->IsInCombat() || bot->IsInFlight() || bot->IsSitState() || bot->GetVehicle() ||
            bot->HasUnitState(UNIT_STATE_CASTING) || bot->isAFK())
            return;
        const std::vector<Companion const*> owned = Owned(bot);
        Creature* critter = OurCritter(bot);
        if (!critter)
        {
            if (!bot->GetCritterGUID().IsEmpty())
                return;   // somebody else's doing: left be
            if (Companion const* pick = PickWeighted(owned, OutWeight))
            {
                bot->CastSpell(bot, pick->spell, true);
                if (!bot->GetCritterGUID().IsEmpty())
                    ++g_stats.summoned;
            }
            return;
        }
        const uint32_t current = critter->GetUInt32Value(UNIT_CREATED_BY_SPELL);
        if (std::none_of(owned.begin(), owned.end(), [current](Companion const* c) { return c->spell == current; }))
        {
            PutAway(bot);   // not one it would have at its level
            return;
        }
        if (s.switchDue && s.idle && now >= s.switchAt)
        {
            s.switchDue = false;
            std::vector<Companion const*> others;
            for (Companion const* c : owned)
                if (c->spell != current)
                    others.push_back(c);
            if (Companion const* pick = PickWeighted(others, OutWeight))
            {
                bot->CastSpell(bot, pick->spell, true);   // the new one takes the old one's place
                ++g_stats.switched;
            }
        }
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
        for (size_t k = 1; k < kHolidays; ++k)
            g_season[k] = HolidayOn(uint8_t(k));

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
                if (!s.taught && !Overheard(bot))
                {
                    s.taught = true;
                    Teach(bot);
                }
                Tend(bot, s, now);
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
