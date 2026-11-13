#include "PlayerbotsPlusDungeons.h"

#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusDungeonQuests.h"
#include "PlayerbotsPlusDungeonsText.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusUtil.h"

#include "DBCStores.h"
#include "Log.h"
#include "Player.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>

int64_t PbpDungeons_FreeMemoryMb()
{
    std::ifstream in("/proc/meminfo");
    if (!in)
        return -1;
    std::string const text{ std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>() };
    return PbpDungeons::MemAvailableMb(text);
}

#if PBP_BOT_DUNGEONS

#include "AiFactory.h"
#include "Group.h"
#include "GroupMgr.h"
#include "InstanceSaveMgr.h"
#include "LFGMgr.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "UpdateTime.h"
#include "WorldSessionMgr.h"

#include "Opcodes.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include <atomic>
#include <chrono>
#include <deque>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;
    using PbpDungeons::Role;

    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    // ---- the dungeon finder's dungeons -----------------------------------------------

    std::vector<PbpLfgDungeon> g_lfg;
    bool g_lfgRead = false;

    void ReadLfg()
    {
        if (g_lfgRead)
            return;
        g_lfgRead = true;
        for (uint32 i = 0; i < sLFGDungeonStore.GetNumRows(); ++i)
        {
            LFGDungeonEntry const* e = sLFGDungeonStore.LookupEntry(i);
            if (!e || e->TypeID != lfg::LFG_TYPE_DUNGEON || e->Difficulty != DUNGEON_DIFFICULTY_NORMAL ||
                (e->Flags & lfg::LFG_FLAG_SEASONAL))
                continue;
            MapEntry const* map = sMapStore.LookupEntry(e->MapID);
            if (!map || !map->IsNonRaidDungeon())
                continue;
            PbpLfgDungeon d;
            d.id = e->ID;
            d.map = e->MapID;
            d.name = e->Name[0] ? e->Name[0] : "";
            d.minLevel = e->MinLevel;
            d.maxLevel = e->MaxLevel;
            d.targetLevel = e->TargetLevel;
            g_lfg.push_back(std::move(d));
        }
        std::sort(g_lfg.begin(), g_lfg.end(),
                  [](PbpLfgDungeon const& a, PbpLfgDungeon const& b) { return a.id < b.id; });
    }

    // A filler's level for a dungeon: what the dungeon finder lets in, and no
    // more than ten over its least -- what playerbots' own queueing keeps to.
    bool FillerFits(PbpLfgDungeon const& d, uint32 level)
    {
        return level >= d.minLevel && level <= d.maxLevel && level <= d.minLevel + 10;
    }

    // ---- who may go ------------------------------------------------------------------

    // The role playerbots' own queueing gives a random bot (LfgJoinAction::GetRoles).
    Role RoleOf(Player* bot)
    {
        uint8 const spec = AiFactory::GetPlayerSpecTab(bot);
        switch (bot->getClass())
        {
            case CLASS_DRUID:
                if (spec == 2)
                    return Role::Healer;
                if (spec == 1 && bot->HasAura(16931))   // Thick Hide: a bear
                    return Role::Tank;
                return Role::Damage;
            case CLASS_PALADIN:
                return spec == 1 ? Role::Tank : spec == 0 ? Role::Healer : Role::Damage;
            case CLASS_PRIEST:
                return spec != 2 ? Role::Healer : Role::Damage;
            case CLASS_SHAMAN:
                return spec == 2 ? Role::Healer : Role::Damage;
            case CLASS_WARRIOR:
                return spec == 2 ? Role::Tank : Role::Damage;
            case CLASS_DEATH_KNIGHT:
                return spec == 0 ? Role::Tank : Role::Damage;
            default:
                return Role::Damage;
        }
    }

    uint8 RoleMask(Role role)
    {
        switch (role)
        {
            case Role::Tank:   return lfg::PLAYER_ROLE_TANK;
            case Role::Healer: return lfg::PLAYER_ROLE_HEALER;
            default:           return lfg::PLAYER_ROLE_DAMAGE;
        }
    }

    // Someone is playing: the same test playerbots makes before it lets its
    // random bots log out.
    bool PlayerOnline()
    {
        return sWorldSessionMgr->GetActiveAndQueuedSessionCount() > 0;
    }

    std::unordered_map<uint64, Clock::time_point> g_cooldown;   // by bot: not queued again before

    bool Cooling(Player* bot, Clock::time_point now)
    {
        auto it = g_cooldown.find(bot->GetGUID().GetRawValue());
        if (it == g_cooldown.end())
            return false;
        if (now < it->second)
            return true;
        g_cooldown.erase(it);
        return false;
    }

    void CoolDown(ObjectGuid guid, uint32 minutes, Clock::time_point now)
    {
        if (minutes)
            g_cooldown[guid.GetRawValue()] = now + std::chrono::minutes(minutes);
    }

    // One of playerbots' random bots, on its own and out in the world, with
    // nothing on: free to queue.
    bool Free(Player* bot, PlayerbotAI* ai)
    {
        if (!bot->IsInWorld() || !bot->IsAlive() || bot->IsInCombat() || bot->GetGroup() || bot->GetGroupInvite() ||
            bot->IsBeingTeleported() || bot->IsInFlight() || bot->GetTransport() || bot->GetVehicle() || bot->duel ||
            bot->GetTradeData() || bot->InBattleground() || bot->InArena() || bot->InBattlegroundQueue())
            return false;
        Map* map = bot->GetMap();
        if (!map || map->Instanceable())
            return false;
        if (!ai || !ai->IsBotAI() || ai->GetMaster())
            return false;
        if (bot->HasAura(lfg::LFG_SPELL_DUNGEON_DESERTER) || sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE)
            return false;
        if (PbpHangout_Has(bot) || PbpDungeonQuests_Walking(bot))
            return false;
        return sRandomPlayerbotMgr.IsRandomBot(bot);
    }

    Group* GroupOf(ObjectGuid guid)
    {
        return guid ? sGroupMgr->GetGroupByGUID(guid.GetCounter()) : nullptr;
    }

    // A person in the group, or a person's own character on autopilot.
    bool SomeonePlaying(Group* group)
    {
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource())
            {
                if (PbpIsRealPlayer(member))
                    return true;
                if (PlayerbotAI* ai = PbpAiOf(member); ai && ai->GetMaster() == member)
                    return true;
            }
        return false;
    }

    // Every member on line is one of playerbots' random bots with no master.
    bool AllRandomBots(Group* group)
    {
        bool any = false;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
        {
            Player* member = ref->GetSource();
            if (!member)
                continue;
            PlayerbotAI* ai = PbpAiOf(member);
            if (!ai || !ai->IsBotAI() || ai->GetMaster() || !sRandomPlayerbotMgr.IsRandomBot(member))
                return false;
            any = true;
        }
        return any;
    }

    // The wander-about strategies a random bot gets on its own (AiFactory): in
    // a party nobody leads they would have it grinding the dungeon's mobs and
    // walking off. Taken off for the run, given back after.
    char const* const kWander[] = { "new rpg", "rpg", "grind", "move random", "travel", "start duel" };

    void Strip(Player* bot)
    {
        PlayerbotAI* ai = PbpAiOf(bot);
        if (!ai)
            return;
        std::string names;
        for (char const* name : kWander)
            if (ai->HasStrategy(name, BOT_STATE_NON_COMBAT))
                names += (names.empty() ? "-" : ",-") + std::string(name);
        if (!names.empty())
            ai->ChangeStrategy(names, BOT_STATE_NON_COMBAT);
    }

    void Revive(Player* bot)
    {
        if (bot->IsAlive() && !bot->HasPlayerFlag(PLAYER_FLAGS_GHOST))
            return;
        bot->RemoveAurasDueToSpell(27827);   // Spirit of Redemption: the death it only puts off
        bot->ResurrectPlayer(1.0f, false);
        bot->SpawnCorpseBones();
    }

    // ---- what it is doing ------------------------------------------------------------

    struct Queued
    {
        std::vector<ObjectGuid> bots;
        std::vector<uint8> roles;    // each one's, as it queued
        lfg::LfgDungeonSet dungeons;
        std::string what;            // "Deadmines, for 2 quests"
        Clock::time_point since;
        uint32 rejoins = 0;          // dropouts put back in the queue
        ObjectGuid party;            // queued together as this group, after waiting alone (QueueAsParty)
        Clock::time_point partySince;
    };
    std::vector<Queued> g_queued;

    // The dungeon finder's offers, by the bot they went to (PbpDungeonsPacketScript
    // notes them; AnswerOffers answers). Noted only while a party this queued waits.
    // Members of a run in its dungeon, kept there (PbpDungeonsTeleportScript):
    // each with its run's map, published every world tick.
    std::mutex g_keepMutex;
    std::unordered_map<uint64, uint32> g_keepIn;
    std::unordered_map<uint64, std::chrono::steady_clock::time_point> g_keptSaid;   // logged at most once a minute
    std::atomic<uint64_t> g_keptIn{ 0 };

    std::mutex g_offerMutex;
    std::unordered_map<uint64, uint32> g_offers;
    std::atomic<bool> g_noteOffers{ false };
    // The offer each bot was last said yes to. Every yes sends the offer out again
    // to everyone in it, so one still waiting on somebody else (a player) must not
    // be said yes to again each tick.
    std::unordered_map<uint64, uint32> g_answered;

    enum class Phase : uint8
    {
        Gathering,   // the group is made: everyone into the dungeon, then the clear
        Clearing,    // mod-dungeon-clear runs it
        Leaving      // done: a moment for the loot, then everyone out
    };

    struct Run
    {
        ObjectGuid group;
        uint32 lfgDungeon = 0;
        uint32 map = 0;
        std::string name;
        Phase phase = Phase::Gathering;
        Clock::time_point since;          // taken on
        Clock::time_point phaseSince;
        Clock::time_point nextTry;        // Gathering: the next go at starting the clear
        Clock::time_point nextStrip;      // Clearing: the next look at the strategies
        std::unordered_map<uint64, Clock::time_point> broughtIn;   // Gathering: last teleport in, by member
        std::vector<ObjectGuid> members;  // at the start of the clear
        std::unordered_map<uint64, std::vector<uint32>> held;      // the quests each held there then
        std::unordered_map<uint64, uint8> standing;                // Clearing: each member as last seen
        bool cleared = false;
        bool handedOver = false;          // a player joined: theirs from then on
        std::string why;
    };
    std::unordered_map<uint64, Run> g_runs;   // by group guid

    // A dungeon whose runs keep failing is left alone for a while.
    struct Luck
    {
        uint32 failedInARow = 0;
        Clock::time_point avoidUntil;
    };
    std::unordered_map<uint32, Luck> g_luck;

    std::deque<std::string> g_lately;                              // the last few results, newest first
    std::unordered_map<uint64, Clock::time_point> g_strays;        // in a dungeon, no group, since
    PbpDungeonsStats g_stats;
    uint32 g_planMs = 0, g_watchMs = 0, g_sweepMs = 0;
    bool g_wasOn = false;

    std::string NameOf(uint32 lfgDungeon, uint32 map)
    {
        if (PbpLfgDungeon const* d = PbpDungeons_LfgDungeon(lfgDungeon))
            return d->name;
        if (MapEntry const* entry = sMapStore.LookupEntry(map))
            return entry->name[0] ? entry->name[0] : "a dungeon";
        return "a dungeon";
    }

    void Lately(std::string line)
    {
        g_lately.push_front(std::move(line));
        while (g_lately.size() > 6)
            g_lately.pop_back();
    }

    uint32 SecondsOf(Clock::duration d)
    {
        return uint32(std::chrono::duration_cast<std::chrono::seconds>(d).count());
    }

    // ---- into the queue ----------------------------------------------------------------

    // Queues `party` (tank, healer, three damage dealers), each on its own, for
    // `dungeons`. All or none: one the dungeon finder will not take undoes the rest.
    bool Queue(std::vector<uint64> const& party, lfg::LfgDungeonSet const& dungeons, std::string const& what,
               Clock::time_point now, std::string& why)
    {
        std::vector<Player*> bots;
        for (uint64 raw : party)
        {
            Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(raw));
            if (!bot)
            {
                why = "one of the party logged out";
                return false;
            }
            bots.push_back(bot);
        }

        std::vector<ObjectGuid> joined;
        std::vector<uint8> roles;
        for (size_t i = 0; i < bots.size(); ++i)
        {
            Player* bot = bots[i];
            Role const role = i == 0 ? Role::Tank : i == 1 ? Role::Healer : Role::Damage;
            roles.push_back(RoleMask(role));
            lfg::LfgDungeonSet set = dungeons;   // JoinLfg narrows it to what the bot may enter
            sLFGMgr->JoinLfg(bot, RoleMask(role), set, "");
            if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_QUEUED)
            {
                why = bot->GetName() + " could not queue for it";
                for (ObjectGuid guid : joined)
                    sLFGMgr->LeaveLfg(guid);
                CoolDown(bot->GetGUID(), 10, now);   // whatever stops it, not straight back
                return false;
            }
            joined.push_back(bot->GetGUID());
        }

        Queued q;
        q.bots = std::move(joined);
        q.roles = std::move(roles);
        q.dungeons = dungeons;
        q.what = what;
        q.since = now;
        g_queued.push_back(std::move(q));
        ++g_stats.queued;

        std::string names;
        for (size_t i = 0; i < bots.size(); ++i)
            names += (i ? ", " : "") + bots[i]->GetName();
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: queued {} -- {}.", what, names);
        return true;
    }

    struct Pooled
    {
        Player* bot = nullptr;
        uint64 guid = 0;
        TeamId team = TEAM_NEUTRAL;
        uint32 level = 0;
        Role role = Role::Damage;
        std::vector<PbpDungeonQuestHold> holds;

        uint32 QuestsFor(uint32 lfgDungeon) const
        {
            for (PbpDungeonQuestHold const& h : holds)
                if (h.lfgDungeon == lfgDungeon)
                    return h.quests;
            return 0;
        }
    };

    std::vector<Pooled> Pool(Clock::time_point now, bool quests)
    {
        std::vector<Pooled> pool;
        for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
        {
            Player* bot = it->second;
            if (!bot || bot->GetLevel() < 10)
                continue;
            PlayerbotAI* ai = PbpAiOf(bot);
            if (!Free(bot, ai) || Cooling(bot, now))
                continue;
            // A dungeon quest done is handed in before it goes anywhere.
            if (quests && PbpDungeonQuests_ToHandIn(bot))
                continue;
            Pooled p;
            p.bot = bot;
            p.guid = bot->GetGUID().GetRawValue();
            p.team = bot->GetTeamId();
            p.level = bot->GetLevel();
            p.role = RoleOf(bot);
            if (quests)
                p.holds = PbpDungeonQuests_Holds(bot);
            pool.push_back(std::move(p));
        }
        return pool;
    }

    // The party for `d` from `pool`'s bots of `team`: quest holders first.
    std::vector<uint64> PartyFor(std::vector<Pooled> const& pool, TeamId team, PbpLfgDungeon const& d, bool needQuest,
                                 uint32& questsHeld, uint32 (*roles)[3] = nullptr)
    {
        std::vector<PbpDungeons::Candidate> candidates;
        uint32 holderLevels = 0, holders = 0;
        for (Pooled const& p : pool)
        {
            if (p.team != team)
                continue;
            uint32 const quests = p.QuestsFor(d.id);
            if (!quests && !FillerFits(d, p.level))
                continue;
            if (quests)
            {
                holderLevels += p.level;
                ++holders;
            }
            candidates.push_back({ p.guid, p.role, p.level, quests });
            if (roles)
                ++(*roles)[uint8(p.role)];
        }
        uint32 const target = holders ? holderLevels / holders : d.targetLevel;
        std::vector<uint64> party = PbpDungeons::PickParty(candidates, target, needQuest);
        questsHeld = 0;
        for (uint64 id : party)
            for (Pooled const& p : pool)
                if (p.guid == id)
                    questsHeld += p.QuestsFor(d.id);
        return party;
    }

    // ---- level bands (BotDungeons.LevelBands) ------------------------------------------

    // The level a dungeon is banded by: the one the dungeon finder recommends.
    uint32 BandLevel(PbpLfgDungeon const& d)
    {
        return d.targetLevel ? d.targetLevel : (d.minLevel + d.maxLevel) / 2;
    }

    // A dungeon's band, -1 for none: no bands, or its level in none of them.
    int DungeonBand(uint32 lfgDungeon)
    {
        PbpLfgDungeon const* d = PbpDungeons_LfgDungeon(lfgDungeon);
        return d ? PbpDungeons::BandOf(g_Pbp.dungeons.bands, BandLevel(*d)) : -1;
    }

    std::string BandText(int band)
    {
        return band < 0 ? std::string() : PbpDungeons::BandName(g_Pbp.dungeons.bands[size_t(band)]);
    }

    // The parties in the queue and the runs going on, by band; those whose
    // dungeon is in none (playerbots' own, for a dungeon a player wanted) in
    // `other`. Both count towards MaxRuns.
    std::vector<uint32> RunsByBand(uint32& other)
    {
        std::vector<uint32> runs(g_Pbp.dungeons.bands.size(), 0);
        other = 0;
        auto add = [&](uint32 lfgDungeon)
        {
            int const band = DungeonBand(lfgDungeon);
            if (band >= 0)
                ++runs[size_t(band)];
            else
                ++other;
        };
        for (Queued const& q : g_queued)
            add(q.dungeons.empty() ? 0 : *q.dungeons.begin());
        for (auto const& [key, r] : g_runs)
            add(r.lfgDungeon);
        return runs;
    }

    // Why the last look found no party in a band, by band: "" when it found
    // one (or has not looked at that band yet).
    std::vector<std::string> g_bandWhy;

    std::string ShortOf(uint32 const (&n)[3])
    {
        return "short of a tank " + std::to_string(n[0]) + ", a healer " + std::to_string(n[1]) + ", damage " +
               std::to_string(n[2]);
    }

    std::string What(PbpLfgDungeon const& d, uint32 quests)
    {
        std::string out = d.name;
        if (int const band = DungeonBand(d.id); band >= 0)
            out += " (" + BandText(band) + ")";
        if (quests)
            out += ", for " + std::to_string(quests) + (quests == 1 ? " quest" : " quests");
        return out;
    }

    std::string g_lastPlan;   // what the last look for a party found, for .pbp dungeons

    // One party, if there is room and one can be made.
    void Plan(Clock::time_point now)
    {
        PbpBotDungeonSettings const& s = g_Pbp.dungeons;
        if (g_queued.size() + g_runs.size() >= s.maxRuns)
        {
            g_lastPlan = "waiting: " + std::to_string(s.maxRuns) + " already in the queue or running";
            return;
        }
        if (s.needPlayerOnline && !PlayerOnline())
        {
            g_lastPlan = "waiting: nobody is playing";
            return;
        }
        if (s.maxTickMs && sWorldUpdateTime.GetAverageUpdateTime() > s.maxTickMs)
        {
            g_lastPlan = "waiting: the world tick averages " + std::to_string(sWorldUpdateTime.GetAverageUpdateTime()) +
                         " ms";
            return;
        }
        // Each run is a dungeon instance more in memory. Unknown (no
        // /proc/meminfo) is not short: startup says it is not checked.
        if (s.minFreeMemoryMb)
            if (int64 const freeMb = PbpDungeons_FreeMemoryMb(); freeMb >= 0 && freeMb < int64(s.minFreeMemoryMb))
            {
                g_lastPlan = "waiting: " + PbpDungeons::MemoryText(freeMb) + " of memory free";
                return;
            }
        if (!sLFGMgr->isOptionEnabled(lfg::LFG_OPTION_ENABLE_DUNGEON_FINDER))
        {
            g_lastPlan = "waiting: the dungeon finder is switched off";
            return;
        }

        std::vector<Pooled> const pool = Pool(now, s.quests);
        uint32 holding = 0;
        for (Pooled const& p : pool)
            if (!p.holds.empty())
                ++holding;
        if (pool.size() < 5)
        {
            g_lastPlan = std::to_string(pool.size()) + " bots free to go: too few";
            return;
        }

        // Every dungeon and side there is a party for, those with the most
        // quest holders first (a shuffle first, so equals take turns). With
        // level bands, a dungeon in none of them is not gone to.
        bool const banded = !s.bands.empty();
        struct Option
        {
            PbpLfgDungeon const* dungeon = nullptr;
            TeamId team = TEAM_NEUTRAL;
            uint32 holders = 0;
            int band = -1;
        };
        std::vector<Option> options;
        for (PbpLfgDungeon const& d : PbpDungeons_LfgDungeons())
        {
            int const band = banded ? DungeonBand(d.id) : -1;
            if (banded && band < 0)
                continue;
            if (auto luck = g_luck.find(d.id); luck != g_luck.end() && now < luck->second.avoidUntil)
                continue;
            for (TeamId team : { TEAM_ALLIANCE, TEAM_HORDE })
            {
                uint32 holders = 0;
                for (Pooled const& p : pool)
                    if (p.team == team && p.QuestsFor(d.id))
                        ++holders;
                if (s.questRequired && !holders)
                    continue;
                options.push_back({ &d, team, holders, band });
            }
        }
        std::shuffle(options.begin(), options.end(), Rng());
        std::stable_sort(options.begin(), options.end(),
                         [](Option const& a, Option const& b) { return a.holders > b.holders; });

        // The bands in the order a free place is offered to them: the one
        // furthest under its share first, and on down while a band can make no
        // party (PbpDungeons::BandOrder). Without bands, one band of them all.
        std::vector<int> bandOrder;
        if (banded)
        {
            uint32 other = 0;
            for (size_t band : PbpDungeons::BandOrder(RunsByBand(other), s.bandShares))
                bandOrder.push_back(int(band));
            g_bandWhy.resize(s.bands.size());
        }
        else
            bandOrder.push_back(-1);

        // Why no party, where there is none: the role it is short of.
        uint32 shortOf[3] = { 0, 0, 0 };
        for (int band : bandOrder)
        {
            uint32 bandShort[3] = { 0, 0, 0 };
            uint32 bandOptions = 0;
            for (Option const& o : options)
            {
                if (o.band != band)
                    continue;
                ++bandOptions;
                uint32 quests = 0;
                uint32 roles[3] = { 0, 0, 0 };
                std::vector<uint64> const party = PartyFor(pool, o.team, *o.dungeon, s.questRequired, quests, &roles);
                if (party.empty())
                {
                    uint8 const role = !roles[0] ? 0 : !roles[1] ? 1 : 2;
                    ++shortOf[role];
                    ++bandShort[role];
                    continue;
                }
                std::string why;
                if (Queue(party, { o.dungeon->id }, What(*o.dungeon, quests), now, why))
                {
                    if (band >= 0)
                        g_bandWhy[size_t(band)].clear();
                    g_lastPlan = "queued " + What(*o.dungeon, quests);
                    return;
                }
                if (g_PbpDebug)
                    LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: no queue for {}: {}.",
                             o.dungeon->name, why);
            }
            if (band >= 0)
                g_bandWhy[size_t(band)] =
                    bandOptions ? "no party, " + ShortOf(bandShort)
                                : s.questRequired ? "nobody free holds a quest for its dungeons"
                                                  : "none of its dungeons to go to just now";
        }
        g_lastPlan = std::to_string(pool.size()) + " bots free to go, " + std::to_string(holding) +
                     " of them holding dungeon quests; no party" +
                     (options.empty() ? std::string(": no dungeon anyone holds a quest for")
                                      : " for " + std::to_string(options.size()) + " dungeon/side pairs (" +
                                        ShortOf(shortOf) + ")");
    }

    // ---- the queue -----------------------------------------------------------------------

    void TakeOn(Group* group, Clock::time_point now);

    bool Waiting(lfg::LfgState state)
    {
        return state == lfg::LFG_STATE_QUEUED || state == lfg::LFG_STATE_PROPOSAL ||
               state == lfg::LFG_STATE_ROLECHECK;
    }

    // A party this made that the dungeon finder never turned into a dungeon
    // group: broken up again.
    void Unparty(Queued& q)
    {
        Group* group = GroupOf(q.party);
        q.party.Clear();
        if (!group || group->isLFGGroup() || !AllRandomBots(group))
            return;
        if (sLFGMgr->GetState(group->GetGUID()) == lfg::LFG_STATE_QUEUED)
            sLFGMgr->LeaveLfg(group->GetGUID());
        group->Disband(true);
    }

    // Bots queued each on their own only ever go with a player: playerbots never
    // lets the dungeon finder put a group of bots together by themselves (its
    // CanCreateLfgProposal), but a party that queues as one it lets be. So after
    // AloneMinutes, when nobody has been matched in, the five leave the queue,
    // make a party (the tank leads) and queue as one, each in the role it had --
    // answered here and now, as the dungeon finder asks each member. The offer
    // then goes to each of them, and AnswerOffers says yes.
    bool QueueAsParty(Queued& q, Clock::time_point now, std::string& why)
    {
        std::vector<Player*> bots;
        for (ObjectGuid guid : q.bots)
        {
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
            {
                why = "one of them logged out";
                return false;
            }
            if (bot->GetGroup() || bot->GetGroupInvite() || sLFGMgr->GetState(guid) != lfg::LFG_STATE_QUEUED)
            {
                why = bot->GetName() + " went another way";
                return false;
            }
            bots.push_back(bot);
        }
        if (bots.size() != MAXGROUPSIZE || q.roles.size() != bots.size())
        {
            why = "not five of them";
            return false;
        }

        for (Player* bot : bots)
        {
            sLFGMgr->LeaveLfg(bot->GetGUID());
            // What playerbots does for a random bot that takes a dungeon finder
            // offer (AnswerOffers' Refresh): it leaves any group, so before the party.
            if (bot->IsAlive() && !bot->IsInCombat())
                sRandomPlayerbotMgr.Refresh(bot);
        }

        Group* group = new Group();
        if (!group->Create(bots[0]))
        {
            delete group;
            why = "the party could not be made";
            return false;
        }
        sGroupMgr->AddGroup(group);
        q.party = group->GetGUID();
        for (size_t i = 1; i < bots.size(); ++i)
            if (bots[i]->GetGroup() || !group->AddMember(bots[i]))
            {
                why = bots[i]->GetName() + " could not join the party";
                Unparty(q);
                return false;
            }

        lfg::LfgDungeonSet set = q.dungeons;
        sLFGMgr->JoinLfg(bots[0], q.roles[0] | lfg::PLAYER_ROLE_LEADER, set, "");
        for (size_t i = 1; i < bots.size(); ++i)
            sLFGMgr->UpdateRoleCheck(group->GetGUID(), bots[i]->GetGUID(), q.roles[i]);
        if (sLFGMgr->GetState(group->GetGUID()) != lfg::LFG_STATE_QUEUED)
        {
            why = "the dungeon finder would not take the party";
            Unparty(q);
            return false;
        }

        q.partySince = now;
        ++g_stats.together;
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: nobody came along for {} in {} -- queued "
                 "again as a party.", q.what, PbpDungeons::Duration(SecondsOf(now - q.since)));
        return true;
    }

    void WatchQueues(Clock::time_point now)
    {
        PbpBotDungeonSettings const& s = g_Pbp.dungeons;
        for (auto it = g_queued.begin(); it != g_queued.end();)
        {
            Queued& q = *it;
            bool waiting = false;
            for (ObjectGuid guid : q.bots)
                waiting = waiting || Waiting(sLFGMgr->GetState(guid));
            for (size_t i = 0; i < q.bots.size(); ++i)
            {
                ObjectGuid const guid = q.bots[i];
                lfg::LfgState const state = sLFGMgr->GetState(guid);
                if (state != lfg::LFG_STATE_PROPOSAL)
                    g_answered.erase(guid.GetRawValue());
                if (Waiting(state))
                    continue;
                Player* bot = ObjectAccessor::FindPlayer(guid);
                if (!bot)
                    continue;
                // Matched: the group it is in now, if nobody is playing in it.
                if (Group* group = bot->GetGroup())
                {
                    if (group->isLFGGroup())
                        TakeOn(group, now);
                    continue;
                }
                // Out of the queue with no group while the rest still wait alone: an
                // offer turned down (or one nobody answered in time). Back in, a few times.
                if (waiting && !q.party && q.rejoins < 5 && i < q.roles.size() && !bot->InBattlegroundQueue() &&
                    !bot->HasAura(lfg::LFG_SPELL_DUNGEON_DESERTER))
                {
                    lfg::LfgDungeonSet set = q.dungeons;
                    sLFGMgr->JoinLfg(bot, q.roles[i], set, "");
                    ++q.rejoins;
                    if (Waiting(sLFGMgr->GetState(guid)))
                        ++g_stats.rejoined;
                }
            }
            if (!waiting)
            {
                Unparty(q);
                it = g_queued.erase(it);
                continue;
            }
            // Alone long enough with nobody matched in: as a party, then -- not
            // while an offer with a player in it is out to one of them.
            if (!q.party && now - q.since >= std::chrono::minutes(s.aloneMinutes))
            {
                bool offered = false;
                for (ObjectGuid guid : q.bots)
                {
                    lfg::LfgState const state = sLFGMgr->GetState(guid);
                    offered = offered || state == lfg::LFG_STATE_PROPOSAL || state == lfg::LFG_STATE_ROLECHECK;
                }
                std::string why;
                if (!offered && !QueueAsParty(q, now, why))
                {
                    for (ObjectGuid guid : q.bots)
                    {
                        if (sLFGMgr->GetState(guid) == lfg::LFG_STATE_QUEUED)
                            sLFGMgr->LeaveLfg(guid);
                        CoolDown(guid, 5, now);
                    }
                    ++g_stats.cameApart;
                    LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: no party for {} after all: "
                             "{}.", q.what, why);
                    it = g_queued.erase(it);
                    continue;
                }
            }
            if (now - q.since >= std::chrono::minutes(s.queueMinutes))
            {
                for (ObjectGuid guid : q.bots)
                    if (sLFGMgr->GetState(guid) == lfg::LFG_STATE_QUEUED)
                    {
                        if (!q.party)
                            sLFGMgr->LeaveLfg(guid);
                        CoolDown(guid, 5, now);
                    }
                Unparty(q);
                ++g_stats.gaveUp;
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: gave up waiting for {}.", q.what);
                it = g_queued.erase(it);
                continue;
            }
            ++it;
        }
    }

    // Says yes to the dungeon finder's offers for the bots this queued, in the
    // world tick the offer was made in (PbpDungeonsPacketScript says why).
    void AnswerOffers()
    {
        std::unordered_map<uint64, uint32> offers;
        {
            std::lock_guard<std::mutex> lock(g_offerMutex);
            if (g_offers.empty())
                return;
            offers.swap(g_offers);
        }
        for (auto const& [raw, offer] : offers)
        {
            ObjectGuid const guid(raw);
            if (sLFGMgr->GetState(guid) != lfg::LFG_STATE_PROPOSAL)
                continue;
            if (auto said = g_answered.find(raw); said != g_answered.end() && said->second == offer)
                continue;
            bool ours = false;
            for (Queued const& q : g_queued)
                ours = ours || std::find(q.bots.begin(), q.bots.end(), guid) != q.bots.end();
            if (!ours)
                continue;
            // What playerbots does for a random bot saying yes: repaired, healed and
            // stocked up. Only on its feet and out of a fight (playerbots says no
            // then; this says yes, and the run brings it in once it can come), and
            // before it is in the group: it leaves any group it is in.
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (bot && bot->IsAlive() && !bot->IsInCombat() && !bot->GetGroup())
                sRandomPlayerbotMgr.Refresh(bot);
            g_answered[raw] = offer;
            sLFGMgr->UpdateProposal(offer, guid, true);
            ++g_stats.offers;
        }
    }

    // The last player has gone (OnlyWithPlayersOnline): the parties still
    // waiting leave the queue -- not one whose offer is out, which is as good
    // as in -- and the runs going on carry on to their end. Plan starts
    // nothing new until someone logs in.
    void LeaveQueuesNobodyPlaying()
    {
        uint32 left = 0;
        std::string what;
        for (auto it = g_queued.begin(); it != g_queued.end();)
        {
            Queued& q = *it;
            bool offered = false;
            for (ObjectGuid guid : q.bots)
            {
                lfg::LfgState const state = sLFGMgr->GetState(guid);
                offered = offered || state == lfg::LFG_STATE_PROPOSAL || state == lfg::LFG_STATE_ROLECHECK;
            }
            if (offered)
            {
                ++it;
                continue;
            }
            for (ObjectGuid guid : q.bots)
                if (!q.party && sLFGMgr->GetState(guid) == lfg::LFG_STATE_QUEUED)
                    sLFGMgr->LeaveLfg(guid);
            Unparty(q);
            what += (what.empty() ? "" : "; ") + q.what;
            ++left;
            it = g_queued.erase(it);
        }
        if (!left)
            return;
        g_stats.leftNobody += left;
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: nobody is playing -- out of the queue: {}. "
                 "{} run(s) carry on to their end; nothing new until someone logs in.", what, g_runs.size());
    }

    // Leave every queue it made (the feature switched off).
    void LeaveQueues(Clock::time_point now)
    {
        for (Queued& q : g_queued)
        {
            for (ObjectGuid guid : q.bots)
                if (sLFGMgr->GetState(guid) == lfg::LFG_STATE_QUEUED)
                {
                    if (!q.party)
                        sLFGMgr->LeaveLfg(guid);
                    CoolDown(guid, 5, now);
                }
            Unparty(q);
        }
        g_queued.clear();
        g_answered.clear();
    }

    // ---- runs ------------------------------------------------------------------------------

    // A group of bots the dungeon finder made, with nobody playing in it.
    void TakeOn(Group* group, Clock::time_point now)
    {
        uint64 const key = group->GetGUID().GetRawValue();
        if (g_runs.contains(key) || !group->isLFGGroup() || group->isRaidGroup() || SomeonePlaying(group) ||
            !AllRandomBots(group))
            return;
        uint32 const dungeon = sLFGMgr->GetDungeon(group->GetGUID());
        uint32 const map = sLFGMgr->GetDungeonMapId(group->GetGUID());
        if (!dungeon || !map)
            return;
        MapEntry const* entry = sMapStore.LookupEntry(map);
        if (!entry || !entry->IsNonRaidDungeon())
            return;
        Run r;
        r.group = group->GetGUID();
        r.lfgDungeon = dungeon;
        r.map = map;
        r.name = NameOf(dungeon, map);
        r.since = r.phaseSince = r.nextTry = r.nextStrip = now;
        g_runs.emplace(key, std::move(r));
        LOG_INFO("module.playerbotsplus",
                 "[Playerbots Plus] Bot dungeons: the dungeon finder made a group of bots for {} -- taking it on.",
                 NameOf(dungeon, map));
    }

    DcBotRun::Limits Limits()
    {
        PbpBotDungeonSettings const& s = g_Pbp.dungeons;
        DcBotRun::Limits limits;
        limits.noProgressS = s.noProgressMinutes * 60;
        limits.overallS = s.maxRunMinutes * 60;
        return limits;
    }

    void Gather(Run& r, Group* group, Clock::time_point now)
    {
        std::vector<Player*> in, out;
        for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            if (Player* member = ref->GetSource(); member && member->IsInWorld())
                (member->GetMapId() == r.map ? in : out).push_back(member);

        // Stragglers: the dungeon finder takes them in (as its teleport would),
        // once out of a fight, and on their feet.
        for (Player* member : out)
        {
            if (member->IsBeingTeleported() || member->IsInCombat())
                continue;
            Clock::time_point& last = r.broughtIn[member->GetGUID().GetRawValue()];
            if (now - last < std::chrono::seconds(15))
                continue;
            last = now;
            Revive(member);
            sLFGMgr->TeleportPlayer(member, false);
        }

        auto const waited = now - r.since;
        if (in.empty())
        {
            if (waited >= std::chrono::minutes(3))
            {
                r.why = "nobody got into the dungeon";
                r.phase = Phase::Leaving;
                r.phaseSince = now;
            }
            return;
        }
        // Everyone in, or a minute and a half for them to get there.
        if (!out.empty() && waited < std::chrono::seconds(90))
            return;
        if (now < r.nextTry)
            return;

        for (Player* member : in)
            Strip(member);

        std::string why;
        if (DcBotRun::Start(in.front(), r.lfgDungeon, Limits(), why))
        {
            r.phase = Phase::Clearing;
            r.phaseSince = now;
            r.nextStrip = now + std::chrono::seconds(10);
            r.members.clear();
            r.held.clear();
            for (Player* member : in)
            {
                r.members.push_back(member->GetGUID());
                r.held[member->GetGUID().GetRawValue()] = PbpDungeonQuests_ForMap(member, r.map);
            }
            ++g_stats.runs;
            std::string names;
            for (size_t i = 0; i < in.size(); ++i)
                names += (i ? ", " : "") + in[i]->GetName();
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: {} begins -- {}.", r.name, names);
            return;
        }
        r.nextTry = now + std::chrono::seconds(10);
        if (waited >= std::chrono::minutes(3))
        {
            r.why = why;
            r.phase = Phase::Leaving;
            r.phaseSince = now;
        }
    }

    // ---- the dead lie where they fell -----------------------------------------------

    // A member of a run, as last seen.
    enum class Standing : uint8
    {
        Up,      // on its feet in the dungeon
        Down,    // dead where it fell
        Ghost,   // released: its spirit is at a graveyard
        Away,    // not in the dungeon, dead or alive
        Gone     // logged out
    };

    Standing StandingOf(Player* bot, uint32 map)
    {
        if (!bot || !bot->IsInWorld())
            return Standing::Gone;
        if (bot->GetMapId() != map)
            return Standing::Away;
        if (bot->IsAlive())
            return Standing::Up;
        return bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? Standing::Ghost : Standing::Down;
    }

    std::string HowItIs(Player* bot)
    {
        return std::string(bot->IsAlive() ? "alive" : bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? "a ghost" : "dead") +
               " in " + PbpZoneName(bot);
    }

    // Everyone in the run (its members at the start, and anyone in the group now).
    std::vector<ObjectGuid> Everyone(Run const& r, Group* group)
    {
        std::vector<ObjectGuid> guids = r.members;
        if (group)
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
                if (std::find(guids.begin(), guids.end(), slot.guid) == guids.end())
                    guids.push_back(slot.guid);
        return guids;
    }

    Player* InWorld(ObjectGuid guid)
    {
        Player* bot = ObjectAccessor::FindPlayer(guid);
        return bot && bot->IsInWorld() ? bot : nullptr;
    }

    // Each refreshed every tick while it lies there; let go, its AI runs
    // again within this.
    constexpr uint32 kKeepDownMs = 1000;

    // A member of a run that dies lies where it fell until someone rezzes it,
    // as anyone does in a party with a healer. Playerbots releases a dead bot
    // with no player in its group at its next AI tick -- the tank always,
    // since it leads the group -- and a tank that is out of the dungeon ends
    // the clear there and then. mod-dungeon-clear's PreventBotRelease stops
    // that inside a dungeon; this makes sure of it for a run: the AI of a
    // member lying dead in the dungeon is held, and a rez offered to it is
    // taken, as a player clicks Accept. mod-dungeon-clear sends a healer over
    // once the fight is done, and calls the run off itself if nobody left can
    // rez. Every tick, ahead of that AI tick. During a clear, a member that
    // dies, gets up, releases anyway or leaves the dungeon is noted, with how
    // (dead, a ghost, or alive, and where): that is how a run ends early --
    // it is how the level-up teleport (PbpDungeonsTeleportScript) was found.
    void KeepTheDeadDown()
    {
        for (auto& [key, r] : g_runs)
        {
            Group* group = GroupOf(r.group);
            bool const clearing = r.phase == Phase::Clearing;
            ObjectGuid tank;
            bool tankRead = false;
            for (ObjectGuid guid : Everyone(r, group))
            {
                Player* bot = InWorld(guid);
                Standing const now = StandingOf(bot, r.map);
                if (now == Standing::Down)
                {
                    PlayerbotAI* ai = PbpAiOf(bot);
                    if (bot->isResurrectRequested() && !bot->HasPreventResurectionAura() && !bot->IsBeingTeleported())
                    {
                        bot->ResurectUsingRequestData();   // what Accept does (HandleResurrectResponseOpcode)
                        ++g_stats.rezzed;
                        if (ai)
                            ai->SetNextCheckDelay(0);
                    }
                    else if (ai)
                        ai->SetNextCheckDelay(kKeepDownMs);
                }
                if (!clearing)
                    continue;

                auto [it, first] = r.standing.try_emplace(guid.GetRawValue(), uint8(now));
                Standing const was = Standing(it->second);
                it->second = uint8(now);
                if (first || was == now)
                    continue;
                if (!tankRead)
                {
                    tank = DcBotRun::Get(r.group).tank;
                    tankRead = true;
                }
                std::string const name = bot ? bot->GetName() : std::string("one of them");
                std::string const who = name + (guid == tank ? " (the tank)" : "");
                switch (now)
                {
                    case Standing::Down:
                        ++g_stats.keptDown;
                        if (g_PbpDebug)
                            LOG_INFO("module.playerbotsplus",
                                     "[Playerbots Plus] Bot dungeons: {} died in {} -- left where it fell for a rez.",
                                     who, r.name);
                        break;
                    case Standing::Up:
                        if (g_PbpDebug && was == Standing::Down)
                            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: {} is up again in {}.",
                                     who, r.name);
                        break;
                    case Standing::Ghost:
                        LOG_INFO("module.playerbotsplus",
                                 "[Playerbots Plus] Bot dungeons: {} released its spirit in {} after all.", who, r.name);
                        break;
                    case Standing::Away:
                        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: {} left {} ({}).", who,
                                 r.name, HowItIs(bot));
                        break;
                    case Standing::Gone:
                        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: {} logged out during {}.",
                                 who, r.name);
                        break;
                }
            }
        }
    }

    // Who is kept in which dungeon: those in the group of every run gathering
    // or clearing (one who has left the group is free to go, as the game's
    // own homebind sends it). Every tick, so a run that has just begun is
    // covered.
    void PublishKeepIn()
    {
        std::unordered_map<uint64, uint32> keep;
        for (auto const& [key, r] : g_runs)
            if (r.phase == Phase::Gathering || r.phase == Phase::Clearing)
                if (Group* group = GroupOf(r.group))
                    for (Group::MemberSlot const& slot : group->GetMemberSlots())
                        keep.emplace(slot.guid.GetRawValue(), r.map);
        std::lock_guard<std::mutex> lock(g_keepMutex);
        g_keepIn.swap(keep);
        for (auto it = g_keptSaid.begin(); it != g_keptSaid.end();)
            it = g_keepIn.contains(it->first) ? std::next(it) : g_keptSaid.erase(it);
    }

    // Before the run's own end takes them out of the dungeon.
    void LetOut(std::vector<ObjectGuid> const& guids)
    {
        std::lock_guard<std::mutex> lock(g_keepMutex);
        for (ObjectGuid guid : guids)
            g_keepIn.erase(guid.GetRawValue());
    }

    // How everyone stood when a run failed, for the log: "Rahilan (the tank)
    // dead, Fimur alive, Lugedin away (alive in The Barrens)".
    std::string AsItEnded(Run const& r, Group* group, ObjectGuid tank)
    {
        std::string out;
        for (ObjectGuid guid : Everyone(r, group))
        {
            Player* bot = InWorld(guid);
            std::string one = bot ? bot->GetName() : std::string("one logged out");
            if (guid == tank)
                one += " (the tank)";
            if (bot && bot->GetMapId() != r.map)
                one += " away (" + HowItIs(bot) + ")";
            else if (bot)
                one += bot->IsAlive() ? " alive" : bot->HasPlayerFlag(PLAYER_FLAGS_GHOST) ? " a ghost" : " dead";
            if (group && !group->IsMember(guid))
                one += ", out of the group";
            out += (out.empty() ? "" : ", ") + one;
        }
        return out;
    }

    void Clear(Run& r, Group* group, Clock::time_point now)
    {
        DcBotRun::Status const st = DcBotRun::Get(r.group);
        switch (st.phase)
        {
            case DcBotRun::Phase::Starting:
            case DcBotRun::Phase::Running:
                // A reset of a bot's strategies (playerbots does them for its
                // own reasons) puts the wander-about ones back.
                if (now >= r.nextStrip)
                {
                    r.nextStrip = now + std::chrono::seconds(10);
                    for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
                        if (Player* member = ref->GetSource(); member && member->GetMapId() == r.map)
                            Strip(member);
                }
                return;
            case DcBotRun::Phase::Cleared:
                r.cleared = true;
                r.why.clear();
                break;
            case DcBotRun::Phase::Failed:
                r.why = st.why;
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: {} as it failed -- {}.", r.name,
                         AsItEnded(r, group, st.tank));
                break;
            case DcBotRun::Phase::HandedOver:
                // A player was in the group, and is gone again by now: the
                // party goes as after any other end.
                r.why = "a player came and went";
                break;
            case DcBotRun::Phase::None:
                r.why = "dungeon clear lost track of it";
                break;
        }
        r.phase = Phase::Leaving;
        r.phaseSince = now;
    }

    // Everyone in the run: alive, out of the group (which takes them back
    // where they queued from), unbound from the dungeon, their strategies
    // back, and a cooldown.
    void Release(Run& r, Clock::time_point now)
    {
        PbpBotDungeonSettings const& s = g_Pbp.dungeons;
        DcBotRun::Stop(r.group, r.cleared ? "the party is leaving" : r.why);

        std::vector<ObjectGuid> guids = r.members;
        if (Group* group = GroupOf(r.group))
            for (Group::MemberSlot const& slot : group->GetMemberSlots())
                if (std::find(guids.begin(), guids.end(), slot.guid) == guids.end())
                    guids.push_back(slot.guid);
        LetOut(guids);

        for (ObjectGuid guid : guids)
        {
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (bot && !bot->IsInWorld())
                bot = nullptr;
            if (bot)
            {
                bot->CombatStop(true);
                Revive(bot);
                if (s.quests)
                {
                    auto const held = r.held.find(guid.GetRawValue());
                    if (held != r.held.end())
                        PbpDungeonQuests_RunEnded(bot, r.lfgDungeon, held->second);
                }
            }
            // Out of the group, logged in or not: one left in it would find
            // itself in an empty group in the dungeon at its next login. The
            // group may be gone by now (disbanded with its last but one), so it
            // is looked up afresh each time.
            if (Group* group = GroupOf(r.group); group && group->IsMember(guid))
                group->RemoveMember(guid, GROUP_REMOVEMETHOD_LEAVE);
            if (bot)
            {
                // Leaving the group took it back where it queued from; if it
                // did not (the group went with the one before), this does.
                if (bot->GetMapId() == r.map && !bot->IsBeingTeleported())
                    bot->TeleportToEntryPoint();
                if (PlayerbotAI* ai = PbpAiOf(bot); ai && !bot->GetGroup())
                    ai->ResetStrategies();
            }
            sInstanceSaveMgr->PlayerUnbindInstance(guid, r.map, DUNGEON_DIFFICULTY_NORMAL, true);
            CoolDown(guid, r.cleared ? s.cooldownMinutes : s.failCooldownMinutes, now);
        }
    }

    void Finish(Run& r, Clock::time_point now)
    {
        PbpBotDungeonSettings const& s = g_Pbp.dungeons;
        bool const handedOver = r.handedOver;
        uint32 const took = SecondsOf(now - r.since);
        if (r.cleared)
        {
            ++g_stats.cleared;
            g_luck.erase(r.lfgDungeon);
            Lately(r.name + " cleared in " + PbpDungeons::Duration(took));
        }
        else if (handedOver)
        {
            ++g_stats.handedOver;
            Lately(r.name + ": a player joined after " + PbpDungeons::Duration(took));
        }
        else
        {
            ++g_stats.failed;
            Luck& luck = g_luck[r.lfgDungeon];
            if (++luck.failedInARow >= s.avoidAfterFailures && s.avoidAfterFailures)
            {
                luck.avoidUntil = now + std::chrono::minutes(s.avoidMinutes);
                luck.failedInARow = 0;
                LOG_INFO("module.playerbotsplus",
                         "[Playerbots Plus] Bot dungeons: {} failed {} times running -- left alone for {} min.",
                         r.name, s.avoidAfterFailures, s.avoidMinutes);
            }
            Lately(r.name + " failed after " + PbpDungeons::Duration(took) + " (" + r.why + ")");
        }
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: {} {} after {}{}{}.", r.name,
                 r.cleared ? "cleared" : handedOver ? "handed over" : "failed", PbpDungeons::Duration(took),
                 r.why.empty() ? "" : " -- ", r.why);
    }

    void WatchRuns(Clock::time_point now, bool on)
    {
        PbpBotDungeonSettings const& s = g_Pbp.dungeons;
        for (auto it = g_runs.begin(); it != g_runs.end();)
        {
            Run& r = it->second;
            Group* group = GroupOf(r.group);

            // Switched off: a run not begun yet is not begun.
            if (!on && r.phase == Phase::Gathering)
            {
                r.why = "bot dungeons were switched off";
                r.phase = Phase::Leaving;
                r.phaseSince = now;
            }

            if (r.phase != Phase::Leaving)
            {
                if (!group)
                {
                    // Everyone logged out, or the group came apart: whoever is
                    // left in the dungeon is sent out by the stray sweep.
                    DcBotRun::Stop(r.group, "the group broke up");
                    r.why = "the group broke up";
                    Finish(r, now);
                    it = g_runs.erase(it);
                    continue;
                }
                if (SomeonePlaying(group))
                {
                    // A player's group now: whatever happens there is theirs.
                    DcBotRun::Stop(r.group, "a player joined");
                    r.why = "a player joined";
                    r.handedOver = true;
                    Finish(r, now);
                    it = g_runs.erase(it);
                    continue;
                }
            }

            switch (r.phase)
            {
                case Phase::Gathering:
                    Gather(r, group, now);
                    break;
                case Phase::Clearing:
                    Clear(r, group, now);
                    break;
                case Phase::Leaving:
                    if (now - r.phaseSince >= std::chrono::seconds(r.cleared ? s.lootSeconds : 5))
                    {
                        Release(r, now);
                        Finish(r, now);
                        it = g_runs.erase(it);
                        continue;
                    }
                    break;
            }
            ++it;
        }
    }

    // Groups of bots the dungeon finder made that this did not queue
    // (playerbots' own queueing, or one that outlived a restart), and bots in
    // a dungeon with no group for it.
    void Sweep(Clock::time_point now)
    {
        std::unordered_set<uint64> stray;
        for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
        {
            Player* bot = it->second;
            if (!bot || !bot->IsInWorld() || bot->IsBeingTeleported())
                continue;
            Map* map = bot->GetMap();
            if (Group* group = bot->GetGroup())
            {
                if (group->isLFGGroup() && !g_runs.contains(group->GetGUID().GetRawValue()) && map &&
                    map->IsDungeon() && !map->IsRaid())
                    TakeOn(group, now);
                continue;
            }
            if (!map || !map->IsDungeon() || bot->InBattleground() || bot->InArena())
                continue;
            PlayerbotAI* ai = PbpAiOf(bot);
            if (!ai || !ai->IsBotAI() || ai->GetMaster() || !sRandomPlayerbotMgr.IsRandomBot(bot))
                continue;
            uint64 const key = bot->GetGUID().GetRawValue();
            stray.insert(key);
            auto const seen = g_strays.emplace(key, now).first->second;
            if (now - seen < std::chrono::minutes(1))
                continue;
            // A minute in a dungeon with nobody: out, the way it came in.
            Revive(bot);
            bot->CombatStop(true);
            if (ai)
                ai->ResetStrategies();
            if (bot->TeleportToEntryPoint())
                ++g_stats.sentOut;
            stray.erase(key);
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: {} was in {} with no group -- sent out.",
                     bot->GetName(), map->GetMapName());
        }
        for (auto it = g_strays.begin(); it != g_strays.end();)
            it = stray.contains(it->first) ? std::next(it) : g_strays.erase(it);
    }

    PbpLfgDungeon const* FindDungeon(std::string const& which)
    {
        if (which.empty())
            return nullptr;
        if (std::all_of(which.begin(), which.end(), [](unsigned char c) { return std::isdigit(c); }))
            return PbpDungeons_LfgDungeon(uint32(std::strtoul(which.c_str(), nullptr, 10)));
        std::string want = which;
        std::transform(want.begin(), want.end(), want.begin(), [](unsigned char c) { return char(std::tolower(c)); });
        PbpLfgDungeon const* found = nullptr;
        for (PbpLfgDungeon const& d : PbpDungeons_LfgDungeons())
        {
            std::string name = d.name;
            std::transform(name.begin(), name.end(), name.begin(),
                           [](unsigned char c) { return char(std::tolower(c)); });
            if (name == want)
                return &d;
            if (!found && name.find(want) != std::string::npos)
                found = &d;
        }
        return found;
    }
}

std::vector<PbpLfgDungeon> const& PbpDungeons_LfgDungeons()
{
    ReadLfg();
    return g_lfg;
}

PbpLfgDungeon const* PbpDungeons_LfgDungeon(uint32_t id)
{
    ReadLfg();
    auto it = std::lower_bound(g_lfg.begin(), g_lfg.end(), id,
                               [](PbpLfgDungeon const& d, uint32_t v) { return d.id < v; });
    return it != g_lfg.end() && it->id == id ? &*it : nullptr;
}

void PbpDungeons_Update(uint32_t diff)
{
    bool const on = g_Pbp.enable && g_Pbp.dungeons.enable;
    Clock::time_point const now = Clock::now();
    if (!on)
    {
        // Nobody new; the queues it made are left; the runs going on finish.
        if (g_wasOn)
        {
            LeaveQueues(now);
            PbpDungeonQuests_Stop();
            g_wasOn = false;
        }
        if (g_runs.empty())
            return;
    }
    else
        g_wasOn = true;

    // Every tick: an offer is answered in the tick it was made in, and a
    // member of a run that has just died is kept down before its AI's next
    // tick can release it.
    g_noteOffers.store(on && !g_queued.empty(), std::memory_order_relaxed);
    AnswerOffers();
    KeepTheDeadDown();
    PublishKeepIn();

    g_watchMs += diff;
    if (g_watchMs >= 1000)
    {
        g_watchMs = 0;
        if (on && g_Pbp.dungeons.needPlayerOnline && !g_queued.empty() && !PlayerOnline())
            LeaveQueuesNobodyPlaying();
        WatchQueues(now);
        WatchRuns(now, on);
    }
    if (!on)
        return;

    g_sweepMs += diff;
    if (g_sweepMs >= 15000)
    {
        g_sweepMs = 0;
        Sweep(now);
    }

    if (g_Pbp.dungeons.quests)
        PbpDungeonQuests_Update(diff);

    g_planMs += diff;
    if (g_planMs >= g_Pbp.dungeons.checkSeconds * 1000)
    {
        g_planMs = 0;
        Plan(now);
    }
}

bool PbpDungeons_Busy(Player* bot)
{
    if (!bot)
        return false;
    if (Group* group = bot->GetGroup())
        if (g_runs.contains(group->GetGUID().GetRawValue()))
            return true;
    for (Queued const& q : g_queued)
        if (std::find(q.bots.begin(), q.bots.end(), bot->GetGUID()) != q.bots.end())
            return true;
    return false;
}

std::vector<std::string> PbpDungeons_Status()
{
    PbpBotDungeonSettings const& s = g_Pbp.dungeons;
    std::vector<std::string> out;
    Clock::time_point const now = Clock::now();
    bool const on = g_Pbp.enable && s.enable;
    std::string tick = std::to_string(sWorldUpdateTime.GetAverageUpdateTime()) + " ms";
    if (s.maxTickMs)
        tick += " (new parties up to " + std::to_string(s.maxTickMs) + " ms)";
    std::string memory;
    if (int64 const freeMb = PbpDungeons_FreeMemoryMb(); freeMb >= 0)
    {
        memory = "; memory free " + PbpDungeons::MemoryText(freeMb);
        if (s.minFreeMemoryMb)
            memory += " (new parties down to " + PbpDungeons::MemoryText(s.minFreeMemoryMb) + ")";
    }
    else if (s.minFreeMemoryMb)
        memory = "; free memory not known here, so not checked";
    std::string const plan = g_lastPlan.empty() ? std::string() : "  Last look for a party: " + g_lastPlan + ".";
    out.push_back(std::string("|cff00ff00[Bot dungeons]|r ") + (on ? "on" : "off") + ": " +
                  std::to_string(g_queued.size()) + " in the queue, " + std::to_string(g_runs.size()) +
                  " run(s) going on, up to " + std::to_string(s.maxRuns) + "; world tick " + tick + memory + "; " +
                  (PlayerOnline() ? "players online" : "no player online") +
                  (s.questRequired ? "; only where a quest takes them" : "; quests or not"));

    if (!plan.empty())
        out.push_back(plan);
    if (!s.bands.empty())
    {
        uint32 other = 0;
        std::vector<uint32> const runs = RunsByBand(other);
        std::string line;
        for (size_t i = 0; i < s.bands.size(); ++i)
        {
            uint32 const share = i < s.bandShares.size() ? s.bandShares[i] : 1;
            line += (line.empty() ? "" : ", ") + PbpDungeons::BandName(s.bands[i]) + " " + std::to_string(runs[i]) +
                    "/" + std::to_string(share);
            if (runs[i] < share && i < g_bandWhy.size() && !g_bandWhy[i].empty())
                line += " (" + g_bandWhy[i] + ")";
        }
        if (other)
            line += "; " + std::to_string(other) + " in no band";
        out.push_back("  By level, now/share: " + line + ". .pbp dungeons bands lists them.");
    }
    for (Queued const& q : g_queued)
    {
        std::string names;
        for (ObjectGuid guid : q.bots)
            if (Player* bot = ObjectAccessor::FindPlayer(guid))
                names += (names.empty() ? "" : ", ") + bot->GetName();
        std::string const together = q.party ? " (as a party for " +
                                                   PbpDungeons::Duration(SecondsOf(now - q.partySince)) + ")"
                                             : std::string();
        out.push_back("  In the queue for " + PbpDungeons::Duration(SecondsOf(now - q.since)) + together + ": " +
                      q.what + " -- " + names);
    }
    for (auto const& [key, r] : g_runs)
    {
        int const band = DungeonBand(r.lfgDungeon);
        std::string line = "  " + r.name + (band >= 0 ? " (" + BandText(band) + ")" : std::string()) + ": ";
        if (r.phase == Phase::Gathering)
            line += "getting everyone in (" + PbpDungeons::Duration(SecondsOf(now - r.since)) + ")";
        else if (r.phase == Phase::Leaving)
            line += r.cleared ? "cleared, looting before they leave" : "leaving (" + r.why + ")";
        else
        {
            DcBotRun::Status const st = DcBotRun::Get(r.group);
            std::string tank;
            if (Player* p = ObjectAccessor::FindPlayer(st.tank))
                tank = p->GetName();
            line += "clearing for " + PbpDungeons::Duration(st.elapsedS) + ", " + std::to_string(st.bossesDown) + "/" +
                    std::to_string(st.bossesTotal) + " bosses, last progress " +
                    PbpDungeons::Duration(st.sinceProgressS) + " ago" + (tank.empty() ? "" : ", " + tank + " leading");
        }
        out.push_back(line);
    }
    if (!g_lately.empty())
    {
        std::string lately;
        for (std::string const& l : g_lately)
            lately += (lately.empty() ? "" : "; ") + l;
        out.push_back("  Lately: " + lately);
    }
    std::string avoided;
    for (auto const& [id, luck] : g_luck)
        if (now < luck.avoidUntil)
            avoided += (avoided.empty() ? "" : ", ") + NameOf(id, 0) + " (" +
                       PbpDungeons::Duration(SecondsOf(luck.avoidUntil - now)) + ")";
    if (!avoided.empty())
        out.push_back("  Left alone after failing: " + avoided);

    PbpDungeonsStats const st = PbpDungeons_GetStats();
    out.push_back("  Since start: " + std::to_string(st.queued) + " parties queued (" + std::to_string(st.together) +
                  " queued again as a party after waiting alone, " + std::to_string(st.cameApart) +
                  " came apart, " + std::to_string(st.gaveUp) + " gave up waiting, " + std::to_string(st.leftNobody) +
                  " left when nobody was playing; " + std::to_string(st.offers) +
                  " group offers said yes to, " + std::to_string(st.rejoined) + " dropouts back in the queue), " +
                  std::to_string(st.runs) + " runs: " + std::to_string(st.cleared) +
                  " cleared, " + std::to_string(st.failed) + " failed, " + std::to_string(st.handedOver) +
                  " joined by a player; " + std::to_string(st.keptIn) +
                  " teleports out of a dungeon turned down (a level-up, mostly); " + std::to_string(st.keptDown) +
                  " deaths left where they fell for a rez (" +
                  std::to_string(st.rezzed) + " rezzes taken); " + std::to_string(st.sentOut) +
                  " bots sent out of a dungeon.");

    PbpDungeonQuestsStats const q = PbpDungeonQuests_GetStats();
    if (s.quests)
        out.push_back("  Dungeon quests: " + std::to_string(q.quests) + " known, given out at " +
                      std::to_string(q.givers) + " places; " + std::to_string(q.sent) + " bots sent to take some (" +
                      std::to_string(q.taken) + " taken), " + std::to_string(q.handIns) + " to hand some in (" +
                      std::to_string(q.handedIn) + " handed in); " + std::to_string(q.walksLost) +
                      " walks given up; " + std::to_string(q.made) + " log slots made; let go: " +
                      std::to_string(q.greyGone) + " grey, " + std::to_string(q.triesGone) + " after " +
                      std::to_string(s.questRuns) + " runs; " + std::to_string(q.walking) + " on their way now.");
    else
        out.push_back("  Dungeon quests: left to playerbots (BotDungeons.Quests = 0).");
    return out;
}

std::vector<std::string> PbpDungeons_Bands()
{
    PbpBotDungeonSettings const& s = g_Pbp.dungeons;
    if (s.bands.empty())
        return { "|cff00ff00[Bot dungeons]|r no level bands (BotDungeons.LevelBands is empty): any dungeon, the "
                 "ones the most free bots hold quests for first." };

    // Each band's dungeons by level, then name.
    std::vector<PbpLfgDungeon const*> dungeons;
    for (PbpLfgDungeon const& d : PbpDungeons_LfgDungeons())
        dungeons.push_back(&d);
    std::sort(dungeons.begin(), dungeons.end(), [](PbpLfgDungeon const* a, PbpLfgDungeon const* b)
    {
        return BandLevel(*a) != BandLevel(*b) ? BandLevel(*a) < BandLevel(*b) : a->name < b->name;
    });
    std::vector<std::string> lists(s.bands.size());
    std::string none;
    for (PbpLfgDungeon const* d : dungeons)
    {
        int const band = DungeonBand(d->id);
        std::string& list = band >= 0 ? lists[size_t(band)] : none;
        list += (list.empty() ? "" : ", ") + d->name + " " + std::to_string(BandLevel(*d));
    }

    uint32 other = 0;
    std::vector<uint32> const runs = RunsByBand(other);
    std::vector<std::string> out;
    out.push_back("|cff00ff00[Bot dungeons]|r by level -- each dungeon's is the one the dungeon finder recommends; "
                  "runs now/share:");
    for (size_t i = 0; i < s.bands.size(); ++i)
        out.push_back("  " + PbpDungeons::BandName(s.bands[i]) + " (" + std::to_string(runs[i]) + "/" +
                      std::to_string(i < s.bandShares.size() ? s.bandShares[i] : 1) + "): " +
                      (lists[i].empty() ? std::string("no dungeon") : lists[i]));
    if (!none.empty())
        out.push_back("  In no band, so not gone to: " + none);
    return out;
}

std::string PbpDungeons_Send(std::string const& which)
{
    PbpLfgDungeon const* d = FindDungeon(which);
    if (!d)
        return "No normal dungeon finder dungeon called '" + which + "' (its id, or part of its name).";
    if (!sLFGMgr->isOptionEnabled(lfg::LFG_OPTION_ENABLE_DUNGEON_FINDER))
        return "The dungeon finder is switched off on this server.";

    Clock::time_point const now = Clock::now();
    std::vector<Pooled> const pool = Pool(now, g_Pbp.dungeons.quests);
    std::string counts;
    for (TeamId team : { TEAM_ALLIANCE, TEAM_HORDE })
    {
        uint32 quests = 0;
        std::vector<uint64> const party = PartyFor(pool, team, *d, false, quests);
        if (!party.empty())
        {
            std::string why;
            if (Queue(party, { d->id }, What(*d, quests), now, why))
                return "Queued a party for " + What(*d, quests) + " (" +
                       (team == TEAM_ALLIANCE ? "Alliance" : "Horde") + "): .pbp dungeons to follow it.";
            return "Could not queue for " + d->name + ": " + why + ".";
        }
        uint32 n[3] = { 0, 0, 0 };
        for (Pooled const& p : pool)
            if (p.team == team && (p.QuestsFor(d->id) || FillerFits(*d, p.level)))
                ++n[uint8(p.role)];
        counts += std::string(counts.empty() ? "" : "; ") + (team == TEAM_ALLIANCE ? "Alliance" : "Horde") + " " +
                  std::to_string(n[0]) + " tank(s), " + std::to_string(n[1]) + " healer(s), " + std::to_string(n[2]) +
                  " damage";
    }
    return "No party for " + d->name + " (levels " + std::to_string(d->minLevel) + "-" + std::to_string(d->maxLevel) +
           "): free bots of a level for it -- " + counts + ".";
}

bool PbpDungeonsTeleportScript::OnPlayerBeforeTeleport(Player* player, uint32 mapid, float x, float y, float z,
                                                       float /*orientation*/, uint32 /*options*/, Unit* /*target*/)
{
    // Within its own map (a stranded member brought to the tank), and the
    // dead on their way to a graveyard, as ever.
    if (!player || mapid == player->GetMapId() || !player->IsAlive())
        return true;
    bool say = false;
    {
        std::lock_guard<std::mutex> lock(g_keepMutex);
        uint64 const id = player->GetGUID().GetRawValue();
        auto it = g_keepIn.find(id);
        if (it == g_keepIn.end() || it->second != player->GetMapId())
            return true;   // not in a run's dungeon: on its way in, or anyone else
        auto const now = std::chrono::steady_clock::now();
        auto& said = g_keptSaid[id];
        if (said == std::chrono::steady_clock::time_point() || now - said >= std::chrono::minutes(1))
        {
            said = now;
            say = true;
        }
    }
    g_keptIn.fetch_add(1, std::memory_order_relaxed);
    if (say)
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Bot dungeons: kept {} (level {}) in its dungeon -- "
                 "something tried to take it to map {} ({:.1f}, {:.1f}, {:.1f}): playerbots' teleport on a "
                 "level-up, most likely.", player->GetName(), player->GetLevel(), mapid, x, y, z);
    return false;
}

void PbpDungeonsPacketScript::OnPacketSent(WorldSession* session, WorldPacket const& packet)
{
    // Every packet to every session comes through here: the opcode first.
    if (packet.GetOpcode() != SMSG_LFG_PROPOSAL_UPDATE || !g_noteOffers.load(std::memory_order_relaxed))
        return;
    // The dungeon (uint32), the offer's state (uint8), then its id (uint32).
    if (!session || packet.size() < 9)
        return;
    Player* player = session->GetPlayer();
    if (!player)
        return;
    uint32 const offer = packet.read<uint32>(5);
    std::lock_guard<std::mutex> lock(g_offerMutex);
    g_offers[player->GetGUID().GetRawValue()] = offer;
}

PbpDungeonsStats PbpDungeons_GetStats()
{
    PbpDungeonsStats out = g_stats;
    out.keptIn = g_keptIn.load(std::memory_order_relaxed);
    out.queuedNow = uint32(g_queued.size());
    out.runsNow = uint32(g_runs.size());
    return out;
}

#else // no mod-dungeon-clear: nothing to run a dungeon with

namespace
{
    std::vector<PbpLfgDungeon> const g_none;
}

std::vector<PbpLfgDungeon> const& PbpDungeons_LfgDungeons()
{
    return g_none;
}

PbpLfgDungeon const* PbpDungeons_LfgDungeon(uint32_t)
{
    return nullptr;
}

void PbpDungeons_Update(uint32_t) { }

bool PbpDungeons_Busy(Player*)
{
    return false;
}

std::vector<std::string> PbpDungeons_Status()
{
    return { "|cff00ff00[Bot dungeons]|r not in this build: it needs mod-dungeon-clear (DcBotRun.h)." };
}

std::vector<std::string> PbpDungeons_Bands()
{
    return PbpDungeons_Status();
}

std::string PbpDungeons_Send(std::string const&)
{
    return "Bot dungeons are not in this build: they need mod-dungeon-clear.";
}

PbpDungeonsStats PbpDungeons_GetStats()
{
    return {};
}

#endif
