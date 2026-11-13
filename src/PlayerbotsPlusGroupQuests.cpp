#include "PlayerbotsPlusGroupQuests.h"

#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusDungeonQuests.h"
#include "PlayerbotsPlusDungeons.h"
#include "PlayerbotsPlusDungeonsText.h"
#include "PlayerbotsPlusGroupQuestsText.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusUtil.h"

#include "Creature.h"
#include "DBCStores.h"
#include "Formulas.h"
#include "GameObject.h"
#include "Group.h"
#include "GroupMgr.h"
#include "LFGMgr.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Opcodes.h"
#include "Player.h"
#include "QuestDef.h"
#include "QuestPackets.h"
#include "WorldPacket.h"
#include "WorldSession.h"

#include "NewRpgInfo.h"
#include "PlayerbotAI.h"
#include "Playerbots.h"
#include "RandomPlayerbotMgr.h"
#include "StatsWeightCalculator.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <map>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <variant>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    // As playerbots' own questing (QUEST_MAX_LEVELS_ABOVE_BOT): a quest more
    // levels above the bot than this is not taken.
    constexpr int32 kMaxLevelsAbove = 3;

    // Playerbots' questing looks for a quest's place within 1500 yards of the
    // bot (NewRpgBaseAction::GetQuestPOIPosAndObjectiveIdx): a leader further
    // off than this walks there first.
    constexpr float kQuestReach = 1200.0f;

    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    PbpGroupQuestSettings const& Cfg()
    {
        return g_Pbp.groupQuests;
    }

    uint32 MinutesOf(Clock::duration d)
    {
        return uint32(std::chrono::duration_cast<std::chrono::minutes>(d).count());
    }

    // ---- the quests ----------------------------------------------------------------------

    // Where someone stands who gives a quest out or takes it back.
    struct Spot
    {
        uint32 map = 0;
        float x = 0.0f, y = 0.0f, z = 0.0f;
        uint32 entry = 0;
        bool go = false;      // a gameobject (a wanted poster) rather than a creature
        uint32 faction = 0;   // a creature's faction template: the other side's are not walked to
    };

    struct GroupQuest
    {
        uint32 id = 0;
        uint32 zone = 0;           // the zone it is done in
        uint32 map = 0;
        int32 level = 0;           // -1: always the player's own
        uint32 minLevel = 0;
        uint32 suggested = 0;      // the players it suggests, 0 for none
        float areaX = 0.0f;        // the middle of its first objective's map marker
        float areaY = 0.0f;
        std::vector<Spot> enders;  // who takes it back, out in the world
    };

    struct Giver
    {
        Spot spot;
        std::vector<uint32> quests;
    };

    std::unordered_map<uint32, GroupQuest> g_quests;
    std::unordered_map<uint32, std::vector<Giver>> g_givers;    // by map
    std::unordered_map<uint32, std::vector<uint32>> g_byZone;   // each zone's, by level
    bool g_indexed = false;
    uint32 g_giverCount = 0;

    // Every objective one a bot can do: creatures to kill, things to gather
    // (from what it kills, or what it finds lying about). Not an escort, a
    // spell to cast, an object to use, somewhere to find, someone to talk to,
    // a timer, an item to use, or a player to kill.
    bool Doable(Quest const* quest)
    {
        if (quest->GetType() == QUEST_TYPE_ESCORT || quest->GetTimeAllowed() || quest->GetPlayersSlain() ||
            quest->GetSrcSpell() || quest->GetSrcItemId())
            return false;
        if (quest->HasSpecialFlag(QUEST_SPECIAL_FLAGS_EXPLORATION_OR_EVENT | QUEST_SPECIAL_FLAGS_CAST |
                                  QUEST_SPECIAL_FLAGS_SPEAKTO | QUEST_SPECIAL_FLAGS_TIMED))
            return false;
        bool any = false;
        for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
        {
            if (quest->RequiredNpcOrGo[i] < 0)
                return false;
            if (quest->RequiredNpcOrGo[i] > 0)
                any = true;
        }
        for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
            if (quest->RequiredItemId[i])
                any = true;
        return any;
    }

    // A map marker for an objective on `map`: where playerbots' questing goes
    // to do it. Without one it never finds where to go. `x`, `y`: the middle
    // of the first.
    bool Marked(uint32 questId, uint32 map, float& x, float& y)
    {
        QuestPOIVector const* pois = sObjectMgr->GetQuestPOIVector(questId);
        if (!pois)
            return false;
        for (QuestPOI const& poi : *pois)
            if (uint32(poi.MapId) == map && poi.ObjectiveIndex >= 0 && !poi.points.empty())
            {
                x = y = 0.0f;
                for (QuestPOIPoint const& point : poi.points)
                {
                    x += float(point.x);
                    y += float(point.y);
                }
                x /= float(poi.points.size());
                y /= float(poi.points.size());
                return true;
            }
        return false;
    }

    void Index()
    {
        if (g_indexed)
            return;
        g_indexed = true;
        Clock::time_point const start = Clock::now();

        for (auto const& [id, quest] : sObjectMgr->GetQuestTemplates())
        {
            if (!quest || (quest->GetType() != QUEST_TYPE_ELITE && quest->GetSuggestedPlayers() < 2))
                continue;
            switch (quest->GetType())
            {
                case QUEST_TYPE_DUNGEON:
                case QUEST_TYPE_HEROIC:
                case QUEST_TYPE_RAID:
                case QUEST_TYPE_RAID_10:
                case QUEST_TYPE_RAID_25:
                case QUEST_TYPE_PVP:
                case QUEST_TYPE_LEGENDARY:
                case QUEST_TYPE_WORLD_EVENT:
                    continue;
                default:
                    break;
            }
            if (quest->IsRepeatable() || quest->IsDaily() || quest->IsWeekly() || quest->IsMonthly() ||
                quest->IsSeasonal() || quest->IsDFQuest() || quest->GetZoneOrSort() <= 0)
                continue;
            AreaTableEntry const* area = sAreaTableStore.LookupEntry(uint32(quest->GetZoneOrSort()));
            if (!area)
                continue;
            MapEntry const* map = sMapStore.LookupEntry(area->mapid);
            if (!map || map->Instanceable())
                continue;   // done in a dungeon or a raid: not one of these
            float areaX = 0.0f, areaY = 0.0f;
            if (!Doable(quest) || !Marked(id, area->mapid, areaX, areaY))
                continue;
            GroupQuest q;
            q.areaX = areaX;
            q.areaY = areaY;
            q.id = id;
            q.zone = area->zone ? area->zone : area->ID;
            q.map = area->mapid;
            q.level = quest->GetQuestLevel();
            q.minLevel = quest->GetMinLevel();
            q.suggested = quest->GetSuggestedPlayers();
            g_byZone[q.zone].push_back(id);
            g_quests.emplace(id, std::move(q));
        }
        for (auto& [zone, ids] : g_byZone)
            std::sort(ids.begin(), ids.end(), [](uint32 a, uint32 b)
            {
                int32 const la = g_quests[a].level, lb = g_quests[b].level;
                return la != lb ? la < lb : a < b;
            });

        // Who gives them out and takes them back, and where they stand: out
        // in the world, in the normal phase.
        std::unordered_map<uint32, std::vector<uint32>> gives, takes, goGives, goTakes;
        auto collect = [](QuestRelations const* relations, std::unordered_map<uint32, std::vector<uint32>>& out)
        {
            if (relations)
                for (auto const& [entry, quest] : *relations)
                    if (g_quests.contains(quest))
                        out[entry].push_back(quest);
        };
        collect(sObjectMgr->GetCreatureQuestRelationMap(), gives);
        collect(sObjectMgr->GetCreatureQuestInvolvedRelationMap(), takes);
        collect(sObjectMgr->GetGOQuestRelationMap(), goGives);
        collect(sObjectMgr->GetGOQuestInvolvedRelationMap(), goTakes);
        auto place = [](Spot const& spot, std::unordered_map<uint32, std::vector<uint32>> const& give,
                        std::unordered_map<uint32, std::vector<uint32>> const& take)
        {
            if (auto it = give.find(spot.entry); it != give.end())
            {
                g_givers[spot.map].push_back({ spot, it->second });
                ++g_giverCount;
            }
            if (auto it = take.find(spot.entry); it != take.end())
                for (uint32 quest : it->second)
                    g_quests[quest].enders.push_back(spot);
        };
        auto outdoors = [](uint32 map, uint32 phaseMask)
        {
            MapEntry const* entry = sMapStore.LookupEntry(map);
            return entry && !entry->Instanceable() && (phaseMask & 1);
        };
        for (auto const& [spawn, data] : sObjectMgr->GetAllCreatureData())
        {
            if (!outdoors(data.mapid, data.phaseMask))
                continue;
            for (uint32 entry : { data.id, data.id2, data.id3 })
                if (entry && (gives.contains(entry) || takes.contains(entry)))
                {
                    CreatureTemplate const* t = sObjectMgr->GetCreatureTemplate(entry);
                    place({ data.mapid, data.posX, data.posY, data.posZ, entry, false, t ? t->faction : 0u }, gives,
                          takes);
                }
        }
        for (auto const& [spawn, data] : sObjectMgr->GetAllGOData())
            if (outdoors(data.mapid, data.phaseMask) && (goGives.contains(data.id) || goTakes.contains(data.id)))
                place({ data.mapid, data.posX, data.posY, data.posZ, data.id, true, 0u }, goGives, goTakes);

        LOG_INFO("module.playerbotsplus",
                 "[Playerbots Plus] Group quests: {} known, in {} zones, given out at {} places ({} ms).",
                 g_quests.size(), g_byZone.size(), g_giverCount,
                 std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start).count());
    }

    std::string TitleOf(uint32 questId)
    {
        Quest const* quest = sObjectMgr->GetQuestTemplate(questId);
        return quest ? quest->GetTitle() : "quest " + std::to_string(questId);
    }

    // ---- each bot ------------------------------------------------------------------------

    std::unordered_map<uint64, std::unordered_map<uint32, uint32>> g_tries;   // failed tries at each quest
    std::unordered_map<uint64, std::unordered_set<uint32>> g_gaveUp;          // not taken again
    std::unordered_map<uint64, std::unordered_set<uint64>> g_unreached;       // spots a walk never got to
    std::unordered_map<uint64, Clock::time_point> g_lookedAt;
    std::unordered_map<uint64, Clock::time_point> g_cooldown;                 // no asking for help before
    std::unordered_set<uint64> g_holders;                                     // hold a group quest
    // Who holds each of a zone's quests, not done, able to try it: who hears an ask.
    std::unordered_map<uint32, std::unordered_map<uint32, std::vector<uint64>>> g_holding;
    std::deque<Clock::time_point> g_recentWalks, g_recentAsks;
    PbpGroupQuestsStats g_stats;
    uint32 g_tickMs = 0, g_scanMs = 0, g_lookMs = 0;
    bool g_wasOn = false;

    Player* InWorld(uint64 raw)
    {
        Player* bot = ObjectAccessor::FindPlayer(ObjectGuid(raw));
        return bot && bot->IsInWorld() ? bot : nullptr;
    }

    uint32 GreyFor(Player* bot)
    {
        return Acore::XP::GetGrayLevel(uint8(bot->GetLevel()));
    }

    uint32 Tries(uint64 bot, uint32 quest)
    {
        auto it = g_tries.find(bot);
        if (it == g_tries.end())
            return 0;
        auto t = it->second.find(quest);
        return t == it->second.end() ? 0 : t->second;
    }

    bool GaveUp(uint64 bot, uint32 quest)
    {
        auto it = g_gaveUp.find(bot);
        return it != g_gaveUp.end() && it->second.contains(quest);
    }

    uint32 FreeSlots(Player* bot)
    {
        uint32 free = 0;
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            if (!bot->GetQuestSlotQuestId(slot))
                ++free;
        return free;
    }

    // Let a quest go, as a player does from the quest log.
    void Drop(Player* bot, uint32 questId)
    {
        uint16 const slot = bot->FindQuestSlot(questId);
        if (slot >= MAX_QUEST_LOG_SIZE)
            return;
        WorldPacket packet(CMSG_QUESTLOG_REMOVE_QUEST);
        packet << uint8(slot);
        WorldPackets::Quest::QuestLogRemoveQuest removeQuest(std::move(packet));
        removeQuest.Read();
        bot->GetSession()->HandleQuestLogRemoveQuest(removeQuest);
        if (PlayerbotAI* ai = PbpAiOf(bot))
            ai->lowPriorityQuest.erase(questId);
    }

    // Given up on: let go, and not taken again.
    void GiveUp(Player* bot, uint32 questId)
    {
        uint64 const key = bot->GetGUID().GetRawValue();
        Drop(bot, questId);
        g_gaveUp[key].insert(questId);
        if (auto it = g_tries.find(key); it != g_tries.end())
            it->second.erase(questId);
        ++g_stats.gaveUp;
    }

    // Holds it, not done, able to try it again: what makes one who could come.
    bool StillAt(Player* bot, GroupQuest const& q)
    {
        uint64 const key = bot->GetGUID().GetRawValue();
        return bot->GetQuestStatus(q.id) == QUEST_STATUS_INCOMPLETE && !GaveUp(key, q.id) &&
               Tries(key, q.id) < Cfg().attempts && !PbpDungeons::Grey(q.level, GreyFor(bot));
    }

    // One of playerbots' random bots with nobody over it, out in the world and
    // wanted by nothing else: one that may take part.
    bool Free(Player* bot, PlayerbotAI* ai)
    {
        if (!bot->IsInWorld() || bot->IsBeingTeleported() || bot->IsInFlight() || bot->GetTransport() ||
            bot->GetVehicle() || bot->InBattleground() || bot->InArena() || bot->InBattlegroundQueue())
            return false;
        Map* map = bot->GetMap();
        if (!map || map->Instanceable())
            return false;
        if (!ai || !ai->IsBotAI() || ai->GetMaster() || !sRandomPlayerbotMgr.IsRandomBot(bot))
            return false;
        if (sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE || PbpHangout_Has(bot) ||
            PbpDungeons_Busy(bot) || PbpDungeonQuests_Walking(bot))
            return false;
        return true;
    }

    // The wander-about strategies a random bot gets on its own (AiFactory): a
    // party member that keeps them goes off about its own business instead of
    // following its leader.
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

    // ---- walks to givers and to hand one in ----------------------------------------------

    enum class WalkKind : uint8
    {
        Take,
        HandIn
    };

    struct Walk
    {
        WalkKind kind = WalkKind::Take;
        Spot spot;
        std::vector<uint32> quests;
        Clock::time_point since;
        Clock::time_point arrived;
        bool there = false;
        uint32 nudges = 0;
    };
    std::unordered_map<uint64, Walk> g_walks;

    uint64 SpotKey(Spot const& spot)
    {
        return (uint64(spot.map) << 48) ^ (uint64(uint32(int32(spot.x)) & 0xFFFFFF) << 24) ^
               uint64(uint32(int32(spot.y)) & 0xFFFFFF);
    }

    bool Hostile(Player* bot, Spot const& spot)
    {
        if (spot.go || !spot.faction)
            return false;
        FactionTemplateEntry const* theirs = sFactionTemplateStore.LookupEntry(spot.faction);
        FactionTemplateEntry const* mine = bot->GetFactionTemplateEntry();
        return theirs && mine && theirs->IsHostileTo(*mine);
    }

    bool Unreached(Player* bot, Spot const& spot)
    {
        auto it = g_unreached.find(bot->GetGUID().GetRawValue());
        return it != g_unreached.end() && it->second.contains(SpotKey(spot));
    }

    // One it may take now: not taken or done, of its level (not grey, at most
    // kMaxLevelsAbove up), not given up on, one the game lets it take.
    bool Takeable(Player* bot, GroupQuest const& q, uint32 level, uint32 grey)
    {
        if (q.minLevel > level || PbpDungeons::Grey(q.level, grey) || q.level > int32(level) + kMaxLevelsAbove)
            return false;
        if (bot->GetQuestStatus(q.id) != QUEST_STATUS_NONE || GaveUp(bot->GetGUID().GetRawValue(), q.id))
            return false;
        Quest const* quest = sObjectMgr->GetQuestTemplate(q.id);
        return quest && bot->CanTakeQuest(quest, false);
    }

    // On its own, nothing on, and playerbots' questing idle: free to walk
    // somewhere for a quest, or to ask for help.
    bool Idle(Player* bot, PlayerbotAI* ai)
    {
        if (!bot->IsAlive() || bot->IsInCombat() || bot->GetGroup() || bot->GetGroupInvite() || bot->duel ||
            !Free(bot, ai) || !ai->HasStrategy("new rpg", BOT_STATE_NON_COMBAT))
            return false;
        auto const& data = ai->rpgInfo.data;
        return std::holds_alternative<NewRpgInfo::Idle>(data) || std::holds_alternative<NewRpgInfo::Rest>(data) ||
               std::holds_alternative<NewRpgInfo::WanderNpc>(data) ||
               std::holds_alternative<NewRpgInfo::WanderRandom>(data) ||
               std::holds_alternative<NewRpgInfo::GoGrind>(data);
    }

    bool GoingTo(PlayerbotAI* ai, uint32 map, float x, float y, float z, float within)
    {
        auto const* camp = std::get_if<NewRpgInfo::GoCamp>(&ai->rpgInfo.data);
        return camp && camp->pos.GetMapId() == map && camp->pos.GetExactDist(x, y, z) < within;
    }

    void Send(Player* bot, PlayerbotAI* ai, Walk walk, Clock::time_point now)
    {
        walk.since = now;
        ai->rpgInfo.ChangeToGoCamp(WorldPosition(walk.spot.map, walk.spot.x, walk.spot.y, walk.spot.z));
        g_walks[bot->GetGUID().GetRawValue()] = std::move(walk);
        g_recentWalks.push_back(now);
    }

    // One done: off to whoever takes it back, the nearest on this map within
    // twice the range a giver is walked to.
    bool StartHandIn(Player* bot, PlayerbotAI* ai, Clock::time_point now)
    {
        float const range = Cfg().range * 2.0f;
        Spot const* best = nullptr;
        uint32 bestQuest = 0;
        float bestDist = 0.0f;
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 const id = bot->GetQuestSlotQuestId(slot);
            auto it = id ? g_quests.find(id) : g_quests.end();
            if (it == g_quests.end() || bot->GetQuestStatus(id) != QUEST_STATUS_COMPLETE)
                continue;
            for (Spot const& spot : it->second.enders)
            {
                if (spot.map != bot->GetMapId() || Hostile(bot, spot) || Unreached(bot, spot))
                    continue;
                float const dist = bot->GetExactDist2d(spot.x, spot.y);
                if (dist <= range && (!best || dist < bestDist))
                {
                    best = &spot;
                    bestQuest = id;
                    bestDist = dist;
                }
            }
        }
        if (!best)
            return false;
        Walk w;
        w.kind = WalkKind::HandIn;
        w.spot = *best;
        w.quests = { bestQuest };
        ai->lowPriorityQuest.erase(bestQuest);
        Send(bot, ai, std::move(w), now);
        ++g_stats.handIns;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Group quests: {} goes to hand in {} ({} yd).",
                     bot->GetName(), TitleOf(bestQuest), uint32(bestDist));
        return true;
    }

    // Quests to take: the nearest giver on this map, within range, with one
    // for the zone it is in that suits it.
    bool StartTake(Player* bot, PlayerbotAI* ai, Clock::time_point now)
    {
        if (FreeSlots(bot) <= Cfg().keepLogFree)
            return false;
        auto const givers = g_givers.find(bot->GetMapId());
        if (givers == g_givers.end())
            return false;
        uint32 const level = bot->GetLevel();
        uint32 const grey = GreyFor(bot);
        uint32 const zone = bot->GetZoneId();
        float const range = Cfg().range;
        Giver const* best = nullptr;
        std::vector<uint32> bestQuests;
        float bestDist = 0.0f;
        for (Giver const& g : givers->second)
        {
            float const dist = bot->GetExactDist2d(g.spot.x, g.spot.y);
            if (dist > range || (best && dist >= bestDist) || Hostile(bot, g.spot) || Unreached(bot, g.spot))
                continue;
            std::vector<uint32> quests;
            for (uint32 id : g.quests)
                if (auto it = g_quests.find(id);
                    it != g_quests.end() && it->second.zone == zone && Takeable(bot, it->second, level, grey))
                    quests.push_back(id);
            if (quests.empty())
                continue;
            best = &g;
            bestQuests = std::move(quests);
            bestDist = dist;
        }
        if (!best)
            return false;
        Walk w;
        w.kind = WalkKind::Take;
        w.spot = best->spot;
        w.quests = std::move(bestQuests);
        Send(bot, ai, std::move(w), now);
        ++g_stats.sent;
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Group quests: {} goes to take {} ({} yd).",
                     bot->GetName(), TitleOf(g_walks[bot->GetGUID().GetRawValue()].quests.front()),
                     uint32(bestDist));
        return true;
    }

    Object* Find(Player* bot, Spot const& spot)
    {
        if (spot.go)
            return bot->FindNearestGameObject(spot.entry, 30.0f);
        Creature* creature = bot->FindNearestCreature(spot.entry, 30.0f, true);
        return creature && !creature->IsHostileTo(bot) ? creature : nullptr;
    }

    void TakeAt(Player* bot, PlayerbotAI* ai, Walk const& w)
    {
        Object* giver = Find(bot, w.spot);
        if (!giver)
            return;
        uint32 const level = bot->GetLevel();
        uint32 const grey = GreyFor(bot);
        for (uint32 id : w.quests)
        {
            if (FreeSlots(bot) <= Cfg().keepLogFree)
                break;
            auto it = g_quests.find(id);
            Quest const* quest = sObjectMgr->GetQuestTemplate(id);
            if (it == g_quests.end() || !quest || !Takeable(bot, it->second, level, grey) ||
                !bot->CanAddQuest(quest, false))
                continue;
            bot->AddQuestAndCheckCompletion(quest, giver);
            QuestStatus const status = bot->GetQuestStatus(id);
            if (status == QUEST_STATUS_NONE)
                continue;
            if (status == QUEST_STATUS_INCOMPLETE)
                ai->lowPriorityQuest.insert(id);
            ++g_stats.taken;
            g_holders.insert(bot->GetGUID().GetRawValue());
            LOG_DEBUG("module.playerbotsplus", "[Playerbots Plus] {} took group quest {} ({}).", bot->GetName(), id,
                      quest->GetTitle());
        }
    }

    uint32 BestReward(Player* bot, Quest const* quest)
    {
        if (quest->GetRewChoiceItemsCount() <= 1)
            return 0;
        StatsWeightCalculator calc(bot);
        uint32 best = 0;
        float bestScore = -1.0f;
        for (uint32 i = 0; i < QUEST_REWARD_CHOICES_COUNT; ++i)
        {
            uint32 const item = quest->RewardChoiceItemId[i];
            if (!item)
                continue;
            float const score = calc.CalculateItem(item);
            if (score > bestScore)
            {
                best = i;
                bestScore = score;
            }
        }
        return best;
    }

    // At the one who takes it back. Playerbots' own questing gets a minute;
    // then it is handed in here. True when the walk is over.
    bool HandInAt(Player* bot, Walk& w, Clock::time_point now)
    {
        uint32 const id = w.quests.front();
        QuestStatus const status = bot->GetQuestStatus(id);
        if (status != QUEST_STATUS_COMPLETE)
        {
            if (status == QUEST_STATUS_REWARDED || bot->GetQuestRewardStatus(id))
                ++g_stats.handedIn;
            return true;
        }
        if (!w.there)
        {
            w.there = true;
            w.arrived = now;
            return false;
        }
        if (now - w.arrived < std::chrono::minutes(1))
            return false;
        Object* ender = Find(bot, w.spot);
        Quest const* quest = sObjectMgr->GetQuestTemplate(id);
        if (!ender || !quest)
            return true;
        uint32 const choice = BestReward(bot, quest);
        if (bot->CanRewardQuest(quest, choice, false))
        {
            bot->RewardQuest(quest, choice, ender);
            ++g_stats.handedIn;
        }
        return true;
    }

    void EndWalk(Player* bot, PlayerbotAI* ai, Walk const& w)
    {
        if (bot && ai && GoingTo(ai, w.spot.map, w.spot.x, w.spot.y, w.spot.z, 2.0f))
            ai->rpgInfo.ChangeToIdle();
    }

    std::unordered_map<uint64, uint32> g_partyOf;   // member -> party

    void Walks(Clock::time_point now)
    {
        for (auto it = g_walks.begin(); it != g_walks.end();)
        {
            Walk& w = it->second;
            Player* bot = InWorld(it->first);
            PlayerbotAI* ai = bot ? PbpAiOf(bot) : nullptr;
            bool const lost = !bot || !ai || bot->GetGroup() || ai->GetMaster() || bot->GetMapId() != w.spot.map ||
                              PbpHangout_Has(bot) || g_partyOf.contains(it->first) ||
                              sLFGMgr->GetState(bot->GetGUID()) != lfg::LFG_STATE_NONE;
            if (lost || now - w.since >= std::chrono::minutes(Cfg().walkMinutes))
            {
                ++g_stats.walksLost;
                if (!lost)
                    g_unreached[it->first].insert(SpotKey(w.spot));
                EndWalk(bot, ai, w);
                it = g_walks.erase(it);
                continue;
            }
            if (!bot->IsAlive() || bot->IsInCombat())
            {
                ++it;
                continue;
            }
            float const dist = bot->GetExactDist(w.spot.x, w.spot.y, w.spot.z);
            bool const going = GoingTo(ai, w.spot.map, w.spot.x, w.spot.y, w.spot.z, 2.0f);
            if (w.there || dist <= 12.0f || (!going && dist <= 30.0f))
            {
                bool done = true;
                if (w.kind == WalkKind::Take)
                    TakeAt(bot, ai, w);
                else
                    done = HandInAt(bot, w, now);
                if (done)
                {
                    it = g_walks.erase(it);
                    continue;
                }
            }
            else if (!going)
            {
                if (++w.nudges > 5)
                {
                    ++g_stats.walksLost;
                    g_unreached[it->first].insert(SpotKey(w.spot));
                    it = g_walks.erase(it);
                    continue;
                }
                ai->rpgInfo.ChangeToGoCamp(WorldPosition(w.spot.map, w.spot.x, w.spot.y, w.spot.z));
            }
            ++it;
        }
    }

    // ---- the quest log -------------------------------------------------------------------

    // Room in a full log for the group quests it holds: what a player would
    // let go of first -- failed, grey, for other zones, the lowest -- never a
    // group quest, a dungeon quest, one done, or the one it is out doing.
    void KeepRoom(Player* bot, PlayerbotAI* ai)
    {
        uint32 const keepFree = Cfg().keepLogFree;
        if (FreeSlots(bot) >= keepFree)
            return;
        uint32 doing = 0;
        if (auto const* quest = std::get_if<NewRpgInfo::DoQuest>(&ai->rpgInfo.data))
            doing = quest->questId;
        uint32 const grey = GreyFor(bot);
        int32 const zone = int32(bot->GetZoneId());
        std::vector<PbpDungeons::LogEntry> log;
        for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
        {
            uint32 const id = bot->GetQuestSlotQuestId(slot);
            if (!id)
                continue;
            Quest const* quest = sObjectMgr->GetQuestTemplate(id);
            QuestStatus const status = bot->GetQuestStatus(id);
            PbpDungeons::LogEntry e;
            e.quest = id;
            e.keep = !quest || g_quests.contains(id) || PbpDungeonQuests_IsOne(id) ||
                     status == QUEST_STATUS_COMPLETE || id == doing;
            e.failed = status == QUEST_STATUS_FAILED;
            e.grey = quest && PbpDungeons::Grey(quest->GetQuestLevel(), grey);
            e.otherZone = quest && quest->GetZoneOrSort() > 0 && quest->GetZoneOrSort() != zone;
            e.level = quest ? bot->GetQuestLevel(quest) : 0;
            log.push_back(e);
        }
        for (uint32 id : PbpDungeons::ToDrop(log, MAX_QUEST_LOG_SIZE, keepFree))
        {
            Drop(bot, id);
            ++g_stats.made;
        }
    }

    // ---- parties -------------------------------------------------------------------------

    enum class Phase : uint8
    {
        Recruiting,   // asking in General; each still about its own questing
        Meeting,      // enough of them, or out of time: getting together where the leader is
        Questing      // doing it: the leader's questing goes to it, the rest follow (or it alone)
    };

    struct Party
    {
        uint32 id = 0;
        uint64 leader = 0;
        uint32 quest = 0;
        uint32 zone = 0;
        uint32 target = 0;                    // the party it wants
        std::vector<uint64> members;          // the leader first
        Phase phase = Phase::Recruiting;
        bool solo = false;                    // Questing alone: nobody came
        ObjectGuid group;                     // made once the first of them gets to the leader
        Clock::time_point since, lastAsk, phaseSince, progressAt, nextStrip;
        uint32 asks = 0;
        uint64 progress = 0;
        bool down = false;                    // everyone dead: that try failed, until they are up again
        bool walking = false;                 // the leader on its way to the quest's place, since walkSince
        Clock::time_point walkSince;
        std::vector<uint32> done;             // quests it did or gave up on: not gone back to
        std::unordered_set<uint64> arrived;   // Meeting: got to the leader
    };
    std::map<uint32, Party> g_parties;   // by id: the oldest first
    uint32 g_nextParty = 1;

    // Members of a party getting together or at it, kept there
    // (PbpGroupQuestsTeleportScript), published every second.
    std::mutex g_keepMutex;
    std::unordered_set<uint64> g_keep;
    std::unordered_map<uint64, std::chrono::steady_clock::time_point> g_keptSaid;   // logged at most once a minute
    std::atomic<uint64_t> g_keptAt{ 0 };

    void PublishKeep()
    {
        std::unordered_set<uint64> keep;
        for (auto const& [id, p] : g_parties)
            if (p.phase != Phase::Recruiting)
                keep.insert(p.members.begin(), p.members.end());
        std::lock_guard<std::mutex> lock(g_keepMutex);
        g_keep.swap(keep);
        for (auto it = g_keptSaid.begin(); it != g_keptSaid.end();)
            it = g_keep.contains(it->first) ? std::next(it) : g_keptSaid.erase(it);
    }

    Party* PartyOf(uint64 bot)
    {
        auto it = g_partyOf.find(bot);
        if (it == g_partyOf.end())
            return nullptr;
        auto p = g_parties.find(it->second);
        return p == g_parties.end() ? nullptr : &p->second;
    }

    Group* GroupOf(ObjectGuid guid)
    {
        return guid ? sGroupMgr->GetGroupByGUID(guid.GetCounter()) : nullptr;
    }

    std::string Zone(uint32 zone)
    {
        AreaTableEntry const* area = sAreaTableStore.LookupEntry(zone);
        return area && area->area_name[0] ? area->area_name[0] : "the zone";
    }

    void Raise(PlayerbotsPlus::BotEventType type, Player* bot, Player* other, Party const& p, uint32 need = 0)
    {
        PlayerbotsPlus::BotEvent e;
        e.type = type;
        e.bot = bot->GetGUID();
        if (other)
            e.other = other->GetGUID();
        e.what = TitleOf(p.quest);
        e.zone = PbpZoneName(bot);
        e.quest = p.quest;
        e.need = need;
        e.have = uint32(p.members.size());
        PbpRaiseBotEvent(e);
    }

    bool CanAsk(Clock::time_point now)
    {
        while (!g_recentAsks.empty() && now - g_recentAsks.front() >= std::chrono::minutes(1))
            g_recentAsks.pop_front();
        return g_recentAsks.size() < Cfg().asksPerMinute;
    }

    // In world, in the zone, still holding it and able to try it, and wanted
    // by nothing else: one that is still in for it. Out of a group, unless
    // it is this party's.
    bool StillIn(Player* bot, Party const& p)
    {
        auto q = g_quests.find(p.quest);
        if (q == g_quests.end() || !StillAt(bot, q->second) || !Free(bot, PbpAiOf(bot)))
            return false;
        if (Group* group = bot->GetGroup(); group && group->GetGUID() != p.group)
            return false;
        return p.phase == Phase::Questing || bot->GetZoneId() == p.zone;
    }

    // Off what this had it doing: back to playerbots' own plans for it. While
    // the party was only asking, each was about its own business: nothing to
    // undo.
    void LetGo(Player* bot, PlayerbotAI* ai, Party const& p, bool cooldown, Clock::time_point now)
    {
        if (Group* group = GroupOf(p.group); group && group->IsMember(bot->GetGUID()))
            group->RemoveMember(bot->GetGUID(), GROUP_REMOVEMETHOD_LEAVE);
        if (ai && p.phase != Phase::Recruiting)
        {
            auto const& data = ai->rpgInfo.data;
            if (std::holds_alternative<NewRpgInfo::Rest>(data) || std::holds_alternative<NewRpgInfo::GoCamp>(data) ||
                std::holds_alternative<NewRpgInfo::DoQuest>(data))
                ai->rpgInfo.ChangeToIdle();
            if (!bot->GetGroup())
                ai->ResetStrategies();
        }
        if (cooldown)
            g_cooldown[bot->GetGUID().GetRawValue()] = now + std::chrono::minutes(Cfg().cooldownMinutes);
    }

    void Leave(Party& p, uint64 member, bool cooldown, Clock::time_point now)
    {
        g_partyOf.erase(member);
        p.members.erase(std::remove(p.members.begin(), p.members.end(), member), p.members.end());
        p.arrived.erase(member);
        if (Player* bot = ObjectAccessor::FindConnectedPlayer(ObjectGuid(member)))
            LetGo(bot, PbpAiOf(bot), p, cooldown, now);
    }

    void Dissolve(uint32 id, std::string const& why, bool cooldown, Clock::time_point now)
    {
        auto it = g_parties.find(id);
        if (it == g_parties.end())
            return;
        Party& p = it->second;
        if (g_PbpDebug || p.phase == Phase::Questing)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Group quests: the party for {} in {} breaks up -- {}.",
                     TitleOf(p.quest), Zone(p.zone), why);
        std::vector<uint64> const members = p.members;
        for (uint64 m : members)
            Leave(p, m, cooldown, now);
        if (Group* group = GroupOf(p.group); group && group->GetMembersCount() <= 1)
            group->Disband(true);
        g_parties.erase(it);
    }

    void Join(Party& p, Player* bot)
    {
        uint64 const key = bot->GetGUID().GetRawValue();
        if (auto w = g_walks.find(key); w != g_walks.end())
        {
            EndWalk(bot, PbpAiOf(bot), w->second);
            g_walks.erase(w);
        }
        p.members.push_back(key);
        g_partyOf[key] = p.id;
    }

    Party& NewParty(Player* leader, uint32 quest, Clock::time_point now)
    {
        Party& p = g_parties[g_nextParty];
        p.id = g_nextParty++;
        p.leader = leader->GetGUID().GetRawValue();
        p.quest = quest;
        p.zone = leader->GetZoneId();
        auto q = g_quests.find(quest);
        p.target = PbpGroupQuests::PartySize(q != g_quests.end() ? q->second.suggested : 0, Cfg().partySize);
        p.since = p.phaseSince = now;
        Join(p, leader);
        return p;
    }

    // Those it asked who hold it and could come: the nearest in level first.
    void Ask(Party& p, Player* leader, Clock::time_point now)
    {
        ++p.asks;
        p.lastAsk = now;
        g_recentAsks.push_back(now);
        ++g_stats.asks;
        Raise(PlayerbotsPlus::BotEventType::GroupQuestAsk, leader, nullptr, p,
              p.target > p.members.size() ? p.target - uint32(p.members.size()) : 0);

        auto zone = g_holding.find(p.zone);
        if (zone == g_holding.end())
            return;
        auto holders = zone->second.find(p.quest);
        if (holders == zone->second.end())
            return;
        std::vector<Player*> could;
        for (uint64 raw : holders->second)
        {
            if (raw == p.leader || std::find(p.members.begin(), p.members.end(), raw) != p.members.end())
                continue;
            if (Party* other = PartyOf(raw))
            {
                // Asking for this same quest on its own (or trying it alone):
                // it hears this one and comes along instead.
                if (other->quest != p.quest || other->members.size() > 1 || other->phase == Phase::Meeting)
                    continue;
            }
            Player* bot = InWorld(raw);
            if (bot && bot->IsAlive() && !bot->GetGroup() && !bot->GetGroupInvite() && bot->GetZoneId() == p.zone &&
                StillIn(bot, p))
                could.push_back(bot);
        }
        int32 const level = int32(leader->GetLevel());
        std::sort(could.begin(), could.end(), [level](Player* a, Player* b)
        {
            return std::abs(int32(a->GetLevel()) - level) < std::abs(int32(b->GetLevel()) - level);
        });
        for (Player* bot : could)
        {
            if (p.members.size() >= p.target)
                break;
            uint64 const key = bot->GetGUID().GetRawValue();
            if (Party* other = PartyOf(key))
                Dissolve(other->id, "one of them heard another ask and went to them", false, now);
            Join(p, bot);
            ++g_stats.answers;
            Raise(PlayerbotsPlus::BotEventType::GroupQuestAnswer, bot, leader, p);
        }
    }

    // Everyone it has meets up where the leader is: it waits there, the rest
    // walk over, and join the party as they get there.
    void Meet(Party& p, Clock::time_point now)
    {
        p.phase = Phase::Meeting;
        p.phaseSince = now;
        p.arrived.clear();
        p.arrived.insert(p.leader);
    }

    uint64 Progress(Party const& p)
    {
        uint64 sum = 0;
        for (uint64 m : p.members)
        {
            Player* bot = InWorld(m);
            if (!bot)
                continue;
            QuestStatusMap const& statuses = bot->getQuestStatusMap();
            auto it = statuses.find(p.quest);
            if (it == statuses.end())
                continue;
            for (uint32 i = 0; i < QUEST_OBJECTIVES_COUNT; ++i)
                sum += it->second.CreatureOrGOCount[i];
            for (uint32 i = 0; i < QUEST_ITEM_OBJECTIVES_COUNT; ++i)
                sum += it->second.ItemCount[i];
            if (it->second.Status == QUEST_STATUS_COMPLETE)
                sum += 1000;
        }
        return sum;
    }

    // Under way: the leader's questing goes to it, the rest follow.
    void Begin(Party& p, Clock::time_point now)
    {
        p.phase = Phase::Questing;
        p.phaseSince = p.progressAt = p.nextStrip = now;
        p.progress = Progress(p);
        p.down = p.walking = false;
        Player* leader = InWorld(p.leader);
        if (!leader)
            return;
        if (p.solo)
        {
            ++g_stats.solos;
            Raise(PlayerbotsPlus::BotEventType::GroupQuestSolo, leader, nullptr, p);
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Group quests: nobody came for {} in {} -- {} tries "
                     "it alone.", TitleOf(p.quest), Zone(p.zone), leader->GetName());
            return;
        }
        ++g_stats.parties;
        Raise(PlayerbotsPlus::BotEventType::GroupQuestGo, leader, nullptr, p);
        std::string names;
        for (uint64 m : p.members)
            if (Player* bot = InWorld(m))
                names += (names.empty() ? "" : ", ") + bot->GetName();
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Group quests: a party of {} goes for {} in {} -- {}.",
                 p.members.size(), TitleOf(p.quest), Zone(p.zone), names);
    }

    void GoAlone(Party& p, Clock::time_point now)
    {
        p.solo = true;
        Begin(p, now);
    }

    // Asking: one more ask when its time comes; enough of them, they meet;
    // out of time, whoever came, or it alone.
    void Recruit(Party& p, Clock::time_point now)
    {
        Player* leader = InWorld(p.leader);
        if (!leader || !StillIn(leader, p) || leader->GetGroup())
        {
            Dissolve(p.id, "the one asking is no longer about it", false, now);
            return;
        }
        std::vector<uint64> const members = p.members;
        for (uint64 m : members)
            if (m != p.leader)
                if (Player* bot = InWorld(m); !bot || !StillIn(bot, p) || bot->GetGroup())
                    Leave(p, m, false, now);

        switch (PbpGroupQuests::AfterAsk(uint32(p.members.size()), p.target, MinutesOf(now - p.since),
                                         Cfg().askForMinutes))
        {
            case PbpGroupQuests::Next::Meet:
                Meet(p, now);
                return;
            case PbpGroupQuests::Next::Solo:
                GoAlone(p, now);
                return;
            case PbpGroupQuests::Next::Wait:
                break;
        }
        if (now - p.lastAsk >= std::chrono::minutes(Cfg().askMinutes) && CanAsk(now))
            Ask(p, leader, now);
    }

    void Meeting(Party& p, Clock::time_point now)
    {
        Player* leader = InWorld(p.leader);
        if (!leader || !StillIn(leader, p))
        {
            Dissolve(p.id, "the leader is no longer about it", false, now);
            return;
        }
        PlayerbotAI* leaderAi = PbpAiOf(leader);
        if (leader->IsAlive() && !leader->IsInCombat() && leaderAi &&
            !std::holds_alternative<NewRpgInfo::Rest>(leaderAi->rpgInfo.data))
            leaderAi->rpgInfo.ChangeToRest();   // it waits where it is

        std::vector<uint64> const members = p.members;
        for (uint64 m : members)
        {
            if (m == p.leader || p.arrived.contains(m))
                continue;
            Player* bot = InWorld(m);
            PlayerbotAI* ai = bot ? PbpAiOf(bot) : nullptr;
            if (!bot || !ai || !StillIn(bot, p) || bot->GetMapId() != leader->GetMapId())
            {
                Leave(p, m, false, now);
                continue;
            }
            if (!bot->IsAlive() || bot->IsInCombat())
                continue;
            if (bot->GetExactDist(leader) > 25.0f)
            {
                if (!GoingTo(ai, leader->GetMapId(), leader->GetPositionX(), leader->GetPositionY(),
                             leader->GetPositionZ(), 15.0f))
                    ai->rpgInfo.ChangeToGoCamp(WorldPosition(leader->GetMapId(), leader->GetPositionX(),
                                                             leader->GetPositionY(), leader->GetPositionZ()));
                continue;
            }
            // There: into the party, as it would be invited and say yes.
            Group* group = GroupOf(p.group);
            if (!group)
            {
                if (leader->GetGroup())
                {
                    Dissolve(p.id, "the leader is in another group", false, now);
                    return;
                }
                group = new Group();
                if (!group->Create(leader))
                {
                    delete group;
                    Dissolve(p.id, "the party could not be made", false, now);
                    return;
                }
                sGroupMgr->AddGroup(group);
                p.group = group->GetGUID();
            }
            if (bot->GetGroup() || !group->AddMember(bot))
            {
                Leave(p, m, false, now);
                continue;
            }
            if (std::holds_alternative<NewRpgInfo::GoCamp>(ai->rpgInfo.data))
                ai->rpgInfo.ChangeToIdle();
            Strip(bot);
            p.arrived.insert(m);
        }

        bool const all = p.arrived.size() >= p.members.size();
        if (!all && now - p.phaseSince < std::chrono::minutes(Cfg().meetMinutes))
            return;
        // Whoever is not there by now is left out.
        for (uint64 m : std::vector<uint64>(p.members))
            if (!p.arrived.contains(m))
                Leave(p, m, false, now);
        if (p.members.size() >= 2 && GroupOf(p.group))
        {
            Begin(p, now);
            return;
        }
        // Nobody got there: a party made for the first to come, that then
        // could not take it, is only the leader's now.
        if (Group* group = GroupOf(p.group); group && group->GetMembersCount() <= 1)
            group->Disband(true);
        p.group.Clear();
        if (now - p.since < std::chrono::minutes(Cfg().askForMinutes))
        {
            p.phase = Phase::Recruiting;   // back to asking, and about its own business
            p.phaseSince = now;
            if (leaderAi && std::holds_alternative<NewRpgInfo::Rest>(leaderAi->rpgInfo.data))
                leaderAi->rpgInfo.ChangeToIdle();
        }
        else
            GoAlone(p, now);
    }

    // The quest's tries: one more failed for everyone at it; one who has had
    // them all lets it go.
    void FailTry(Party& p, std::string const& why)
    {
        ++g_stats.failedTries;
        for (uint64 m : p.members)
            if (Player* bot = InWorld(m); bot && bot->GetQuestStatus(p.quest) == QUEST_STATUS_INCOMPLETE)
                if (++g_tries[m][p.quest] >= Cfg().attempts)
                {
                    GiveUp(bot, p.quest);
                    LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Group quests: {} let {} go after {} tries.",
                             bot->GetName(), TitleOf(p.quest), Cfg().attempts);
                }
        if (g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Group quests: a try at {} failed -- {}.",
                     TitleOf(p.quest), why);
    }

    // The one who holds the quest still, and leads: the party's lead passes
    // to it; it gets the strategies a leader has, the old one follows.
    void PassLead(Party& p, uint64 to)
    {
        Player* newLeader = InWorld(to);
        if (!newLeader || to == p.leader)
            return;
        if (Group* group = GroupOf(p.group))
            group->ChangeLeader(newLeader->GetGUID());
        // The old one follows now, if it is still with them (it may have left).
        bool const stays = std::find(p.members.begin(), p.members.end(), p.leader) != p.members.end();
        if (Player* old = stays ? InWorld(p.leader) : nullptr)
        {
            if (PlayerbotAI* ai = PbpAiOf(old))
                ai->rpgInfo.ChangeToIdle();
            Strip(old);
        }
        if (PlayerbotAI* ai = PbpAiOf(newLeader))
            ai->ResetStrategies();   // a group leader's: questing and grinding
        p.leader = to;
        p.members.erase(std::remove(p.members.begin(), p.members.end(), to), p.members.end());
        p.members.insert(p.members.begin(), to);
    }

    // Done with this one (or given up on it): the next group quest of the zone
    // two of them hold, those who do not hold it leaving; none, and it breaks
    // up.
    void NextQuest(Party& p, Clock::time_point now)
    {
        p.done.push_back(p.quest);
        auto const zoneQuests = g_byZone.find(p.zone);
        std::vector<PbpGroupQuests::Holding> holdings;
        if (zoneQuests != g_byZone.end() && !p.solo)
            for (uint64 m : p.members)
            {
                Player* bot = InWorld(m);
                if (!bot)
                    continue;
                PbpGroupQuests::Holding h;
                h.member = m;
                for (uint32 id : zoneQuests->second)
                    if (std::find(p.done.begin(), p.done.end(), id) == p.done.end() && StillAt(bot, g_quests[id]))
                        h.quests.push_back(id);
                holdings.push_back(std::move(h));
            }
        uint32 const next = zoneQuests != g_byZone.end()
                                ? PbpGroupQuests::NextTogether(holdings, zoneQuests->second)
                                : 0;
        if (!next)
        {
            Dissolve(p.id, "nothing more to do together", true, now);
            return;
        }
        uint64 lead = 0;
        for (PbpGroupQuests::Holding const& h : holdings)
        {
            bool const holds = std::find(h.quests.begin(), h.quests.end(), next) != h.quests.end();
            if (!holds)
                Leave(p, h.member, true, now);
            else if (!lead || h.member == p.leader)
                lead = h.member;
        }
        if (lead != p.leader)
            PassLead(p, lead);
        p.quest = next;
        auto q = g_quests.find(next);
        p.target = PbpGroupQuests::PartySize(q != g_quests.end() ? q->second.suggested : 0, Cfg().partySize);
        Begin(p, now);
    }

    void Questing(Party& p, Clock::time_point now)
    {
        if (!p.solo && !GroupOf(p.group))
        {
            Dissolve(p.id, "the group broke up", true, now);
            return;
        }
        // Who is still with it: in the world, in its group, on the leader's map.
        // (One leaving a party of two breaks the group up: looked up afresh.)
        Player* leader = InWorld(p.leader);
        for (uint64 m : std::vector<uint64>(p.members))
        {
            Player* bot = m == p.leader ? leader : InWorld(m);
            Group* group = GroupOf(p.group);
            bool const grouped = p.solo ? bot && !bot->GetGroup() : group && bot && group->IsMember(bot->GetGUID());
            bool const with = grouped && (!leader || bot->GetMapId() == leader->GetMapId());
            if (!with)
            {
                if (m == p.leader)
                {
                    Dissolve(p.id, "the leader left", true, now);
                    return;
                }
                Leave(p, m, true, now);
            }
        }
        if (!leader)
            return;
        if (!p.solo && p.members.size() < 2)
        {
            // Everyone else gone: the leader carries on alone.
            Leave(p, p.leader, false, now);
            p.members.push_back(p.leader);
            g_partyOf[p.leader] = p.id;
            p.group.Clear();
            p.solo = true;
        }

        // Done: everyone who held it has it done.
        bool held = false, allDone = true;
        uint64 still = 0;   // one who has it still to do
        for (uint64 m : p.members)
            if (Player* bot = InWorld(m))
            {
                QuestStatus const status = bot->GetQuestStatus(p.quest);
                if (status == QUEST_STATUS_INCOMPLETE)
                {
                    held = true;
                    allDone = false;
                    if (!still)
                        still = m;
                }
                else if (status == QUEST_STATUS_COMPLETE || status == QUEST_STATUS_REWARDED)
                    held = true;
            }
        if (!held || allDone)
        {
            if (held)
            {
                ++g_stats.done;
                Raise(PlayerbotsPlus::BotEventType::GroupQuestDone, leader, nullptr, p);
                LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Group quests: {} done in {} ({}{}).",
                         TitleOf(p.quest), Zone(p.zone), p.solo ? "alone, " : "",
                         PbpDungeons::Duration(uint32(std::chrono::duration_cast<std::chrono::seconds>(
                             now - p.phaseSince).count())));
            }
            NextQuest(p, now);
            return;
        }
        // The leader has its part done and someone has not: they lead, so the
        // party stays where the rest of it is to be done.
        if (leader->GetQuestStatus(p.quest) != QUEST_STATUS_INCOMPLETE && still && !p.solo)
        {
            PassLead(p, still);
            leader = InWorld(p.leader);
            if (!leader)
                return;
        }

        // Everyone down: that try failed; the next starts when they are up.
        bool anyUp = false;
        for (uint64 m : p.members)
            if (Player* bot = InWorld(m); bot && bot->IsAlive())
                anyUp = true;
        if (!anyUp)
        {
            if (!p.down)
            {
                p.down = true;
                FailTry(p, p.solo ? "it died" : "the party wiped");
            }
            return;
        }
        if (p.down)
        {
            bool allUp = true;
            for (uint64 m : p.members)
                if (Player* bot = InWorld(m); bot && !bot->IsAlive())
                    allUp = false;
            if (!allUp)
                return;
            p.down = p.walking = false;
            p.progressAt = now;
            if (leader->GetQuestStatus(p.quest) != QUEST_STATUS_INCOMPLETE)
            {
                Raise(PlayerbotsPlus::BotEventType::GroupQuestGiveUp, leader, nullptr, p);
                NextQuest(p, now);   // it let it go after its last try
                return;
            }
        }

        // Playerbots' questing finds a quest's place only within 1500 yards:
        // from further, the leader walks there first. The walk has
        // GroupQuests.WalkMinutes, and the try's clock starts once they are
        // there.
        GroupQuest const& q = g_quests[p.quest];
        bool const far = leader->GetMapId() == q.map && leader->GetExactDist2d(q.areaX, q.areaY) > kQuestReach;
        if (!far)
            p.walking = false;
        else if (!p.walking)
        {
            p.walking = true;
            p.walkSince = now;
        }
        else if (now - p.walkSince >= std::chrono::minutes(Cfg().walkMinutes))
        {
            p.walkSince = now;
            FailTry(p, "it could not get there");
            if (leader->GetQuestStatus(p.quest) != QUEST_STATUS_INCOMPLETE)
            {
                Raise(PlayerbotsPlus::BotEventType::GroupQuestGiveUp, leader, nullptr, p);
                NextQuest(p, now);
                return;
            }
        }

        // Moving forward: anyone's count of what it asks for went up.
        uint64 const progress = Progress(p);
        if (progress > p.progress || far)
            p.progressAt = now;
        p.progress = progress;
        if (now - p.progressAt >= std::chrono::minutes(Cfg().noProgressMinutes))
        {
            p.progressAt = now;
            FailTry(p, "nothing moved forward");
            if (leader->GetQuestStatus(p.quest) != QUEST_STATUS_INCOMPLETE)
            {
                Raise(PlayerbotsPlus::BotEventType::GroupQuestGiveUp, leader, nullptr, p);
                NextQuest(p, now);
                return;
            }
        }

        // The leader goes to it; the rest follow.
        PlayerbotAI* ai = PbpAiOf(leader);
        if (ai && leader->IsAlive() && !leader->IsInCombat())
        {
            if (far)
            {
                auto const* camp = std::get_if<NewRpgInfo::GoCamp>(&ai->rpgInfo.data);
                if (!camp || camp->pos.GetMapId() != q.map || camp->pos.GetExactDist2d(q.areaX, q.areaY) > 30.0f)
                {
                    float z = leader->GetMap()->GetHeight(leader->GetPhaseMask(), q.areaX, q.areaY, MAX_HEIGHT);
                    if (z <= INVALID_HEIGHT)
                        z = leader->GetPositionZ();
                    ai->rpgInfo.ChangeToGoCamp(WorldPosition(q.map, q.areaX, q.areaY, z));
                }
            }
            else
            {
                auto const* doing = std::get_if<NewRpgInfo::DoQuest>(&ai->rpgInfo.data);
                if (!doing || doing->questId != p.quest)
                    if (Quest const* quest = sObjectMgr->GetQuestTemplate(p.quest))
                        ai->rpgInfo.ChangeToDoQuest(p.quest, quest);
            }
        }
        if (!p.solo && now >= p.nextStrip)
        {
            p.nextStrip = now + std::chrono::seconds(10);
            for (uint64 m : p.members)
                if (m != p.leader)
                    if (Player* bot = InWorld(m))
                        Strip(bot);   // playerbots puts its own plans back on a reset
        }
    }

    void TickParties(Clock::time_point now)
    {
        std::vector<uint32> ids;
        for (auto const& [id, p] : g_parties)
            ids.push_back(id);
        for (uint32 id : ids)
        {
            auto it = g_parties.find(id);
            if (it == g_parties.end())
                continue;
            Party& p = it->second;
            switch (p.phase)
            {
                case Phase::Recruiting: Recruit(p, now); break;
                case Phase::Meeting:    Meeting(p, now); break;
                case Phase::Questing:   Questing(p, now); break;
            }
        }
    }

    // ---- who holds what --------------------------------------------------------------------

    // Every bot holding a group quest: marked so playerbots' questing does not
    // go off to do it alone (and unmarked once done, so it may go and hand it
    // in), grey ones let go, and who could come along for each noted.
    void Scan()
    {
        g_holders.clear();
        g_holding.clear();
        for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
        {
            Player* bot = it->second;
            PlayerbotAI* ai = bot ? PbpAiOf(bot) : nullptr;
            if (!bot || !ai || !bot->IsInWorld())
                continue;
            uint64 const key = bot->GetGUID().GetRawValue();
            Party const* party = PartyOf(key);
            uint32 const zone = bot->GetZoneId();
            uint32 const grey = GreyFor(bot);
            bool held = false;
            for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
            {
                uint32 const id = bot->GetQuestSlotQuestId(slot);
                auto const q = id ? g_quests.find(id) : g_quests.end();
                if (q == g_quests.end())
                    continue;
                QuestStatus const status = bot->GetQuestStatus(id);
                bool const doing = party && party->phase == Phase::Questing && party->quest == id;
                if (status == QUEST_STATUS_INCOMPLETE && PbpDungeons::Grey(q->second.level, grey) && !doing)
                {
                    Drop(bot, id);
                    ++g_stats.greyGone;
                    continue;
                }
                held = true;
                if (status != QUEST_STATUS_INCOMPLETE)
                {
                    ai->lowPriorityQuest.erase(id);
                    continue;
                }
                ai->lowPriorityQuest.insert(id);
                if (q->second.zone == zone && StillAt(bot, q->second))
                    g_holding[zone][id].push_back(key);
            }
            if (held)
                g_holders.insert(key);
        }
    }

    void KeepAll()
    {
        for (uint64 raw : g_holders)
            if (Player* bot = InWorld(raw))
                if (PlayerbotAI* ai = PbpAiOf(bot))
                    KeepRoom(bot, ai);
    }

    // A holder asks for help with one of its quests in the zone: the one most
    // others hold. Where a party for it is already looking for people (or one
    // is trying it alone), that one answers and it comes along.
    bool StartAsking(Player* bot, Clock::time_point now)
    {
        uint64 const key = bot->GetGUID().GetRawValue();
        if (auto c = g_cooldown.find(key); c != g_cooldown.end())
        {
            if (now < c->second)
                return false;
            g_cooldown.erase(c);
        }
        uint32 const zone = bot->GetZoneId();
        auto const zoneQuests = g_byZone.find(zone);
        if (zoneQuests == g_byZone.end())
            return false;
        auto const holding = g_holding.find(zone);
        std::vector<PbpGroupQuests::Held> held;
        for (uint32 id : zoneQuests->second)
        {
            GroupQuest const& q = g_quests[id];
            if (!StillAt(bot, q))
                continue;
            uint32 others = 0;
            if (holding != g_holding.end())
                if (auto h = holding->second.find(id); h != holding->second.end())
                    others = uint32(h->second.size()) - (std::find(h->second.begin(), h->second.end(), key) !=
                                                         h->second.end());
            Quest const* quest = sObjectMgr->GetQuestTemplate(id);
            held.push_back({ id, quest ? bot->GetQuestLevel(quest) : q.level, others });
        }
        uint32 const quest = PbpGroupQuests::QuestToAsk(held);
        if (!quest || !CanAsk(now))
            return false;

        for (auto& [id, p] : g_parties)
        {
            bool const open = p.phase == Phase::Recruiting || (p.phase == Phase::Questing && p.solo);
            if (p.zone != zone || p.quest != quest || !open || p.members.size() >= p.target)
                continue;
            Player* leader = InWorld(p.leader);
            if (!leader)
                continue;
            // It asks; the one already about it answers, and it comes along.
            g_recentAsks.push_back(now);
            ++g_stats.asks;
            Party asking;
            asking.quest = quest;
            asking.members = { key };
            Raise(PlayerbotsPlus::BotEventType::GroupQuestAsk, bot, nullptr, asking, p.target - 1);
            Join(p, bot);
            ++g_stats.answers;
            Raise(PlayerbotsPlus::BotEventType::GroupQuestInvite, leader, bot, p);
            if (p.phase == Phase::Questing)
            {
                // It was at it alone: now there are two of them.
                p.solo = false;
                if (PlayerbotAI* ai = PbpAiOf(leader))
                    ai->rpgInfo.ChangeToIdle();
                Meet(p, now);
            }
            return true;
        }
        Party& p = NewParty(bot, quest, now);
        Ask(p, bot, now);
        return true;
    }

    // A few idle bots are looked at: one to hand in, quests to take, or help
    // to ask for.
    void Look(Clock::time_point now)
    {
        while (!g_recentWalks.empty() && now - g_recentWalks.front() >= std::chrono::minutes(1))
            g_recentWalks.pop_front();

        std::vector<Player*> idle;
        for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
        {
            Player* bot = it->second;
            if (!bot || bot->GetLevel() < 10)
                continue;
            uint64 const key = bot->GetGUID().GetRawValue();
            if (g_walks.contains(key) || g_partyOf.contains(key))
                continue;
            if (auto looked = g_lookedAt.find(key);
                looked != g_lookedAt.end() && now - looked->second < std::chrono::minutes(Cfg().lookMinutes))
                continue;
            if (Idle(bot, PbpAiOf(bot)))
                idle.push_back(bot);
        }
        std::shuffle(idle.begin(), idle.end(), Rng());

        uint32 looked = 0;
        for (Player* bot : idle)
        {
            if (++looked > 100)
                break;
            g_lookedAt[bot->GetGUID().GetRawValue()] = now;
            PlayerbotAI* ai = PbpAiOf(bot);
            bool const walk = g_recentWalks.size() < Cfg().walksPerMinute;
            if (walk && (StartHandIn(bot, ai, now) || StartTake(bot, ai, now)))
                continue;
            StartAsking(bot, now);
        }

        for (auto it = g_lookedAt.begin(); it != g_lookedAt.end();)
            it = now - it->second >= std::chrono::minutes(Cfg().lookMinutes) ? g_lookedAt.erase(it) : std::next(it);
    }

    // Switched off: every party lets its members go, and the walks stop.
    void Stop()
    {
        Clock::time_point const now = Clock::now();
        std::vector<uint32> ids;
        for (auto const& [id, p] : g_parties)
            ids.push_back(id);
        for (uint32 id : ids)
            Dissolve(id, "group quests were switched off", false, now);
        PublishKeep();
        for (auto const& [raw, w] : g_walks)
        {
            Player* bot = InWorld(raw);
            EndWalk(bot, bot ? PbpAiOf(bot) : nullptr, w);
        }
        g_walks.clear();
        g_recentWalks.clear();
    }

    char const* PhaseText(Party const& p)
    {
        switch (p.phase)
        {
            case Phase::Recruiting: return "asking for help";
            case Phase::Meeting:    return "getting together";
            default:                return p.solo ? "at it alone" : "at it";
        }
    }
}

void PbpGroupQuests_Update(uint32_t diff)
{
    bool const on = g_Pbp.enable && g_Pbp.groupQuests.enable;
    if (!on)
    {
        if (g_wasOn)
        {
            Stop();
            g_wasOn = false;
        }
        return;
    }
    g_wasOn = true;
    Index();
    Clock::time_point const now = Clock::now();

    g_tickMs += diff;
    if (g_tickMs >= 1000)
    {
        g_tickMs = 0;
        Walks(now);
        KeepAll();
        TickParties(now);
        PublishKeep();
    }
    g_scanMs += diff;
    if (g_scanMs >= 10000)
    {
        g_scanMs = 0;
        Scan();
    }
    g_lookMs += diff;
    if (g_lookMs >= 10000)
    {
        g_lookMs = 0;
        Look(now);
    }
}

bool PbpGroupQuests_Busy(Player* bot)
{
    if (!bot)
        return false;
    uint64 const key = bot->GetGUID().GetRawValue();
    return g_partyOf.contains(key) || g_walks.contains(key);
}

bool PbpGroupQuests_IsOne(uint32_t questId)
{
    if (!g_Pbp.enable || !g_Pbp.groupQuests.enable)
        return false;
    Index();
    return g_quests.contains(questId);
}

std::vector<std::string> PbpGroupQuests_Status()
{
    std::vector<std::string> out;
    PbpGroupQuestSettings const& s = g_Pbp.groupQuests;
    bool const on = g_Pbp.enable && s.enable;
    if (on)
        Index();
    Clock::time_point const now = Clock::now();
    out.push_back(std::string("|cff00ff00[Group quests]|r ") + (on ? "on" : "off") + ": " +
                  std::to_string(g_quests.size()) + " known, given out at " + std::to_string(g_giverCount) +
                  " places; " + std::to_string(g_holders.size()) + " bots hold some; " +
                  std::to_string(g_parties.size()) + " parties now.");
    for (auto const& [id, p] : g_parties)
    {
        std::string names;
        for (uint64 m : p.members)
            if (Player* bot = InWorld(m))
                names += (names.empty() ? "" : ", ") + bot->GetName();
        out.push_back("  " + TitleOf(p.quest) + " in " + Zone(p.zone) + ": " + PhaseText(p) + " (" +
                      std::to_string(p.members.size()) + "/" + std::to_string(p.target) + ", " +
                      PbpDungeons::Duration(uint32(std::chrono::duration_cast<std::chrono::seconds>(
                          now - p.since).count())) +
                      (p.phase == Phase::Recruiting ? ", asked " + std::to_string(p.asks) + "x" : std::string()) +
                      ") -- " + names);
    }
    PbpGroupQuestsStats const st = PbpGroupQuests_GetStats();
    out.push_back("  Since start: " + std::to_string(st.sent) + " bots sent to take some (" +
                  std::to_string(st.taken) + " taken), " + std::to_string(st.handIns) + " to hand one in (" +
                  std::to_string(st.handedIn) + " handed in), " + std::to_string(st.walksLost) +
                  " walks given up; " + std::to_string(st.asks) + " asks for help, " + std::to_string(st.answers) +
                  " answered; " + std::to_string(st.parties) + " parties went, " + std::to_string(st.solos) +
                  " went alone; " + std::to_string(st.done) + " quests done, " + std::to_string(st.failedTries) +
                  " tries failed; let go: " + std::to_string(st.gaveUp) + " after their tries, " +
                  std::to_string(st.greyGone) + " grey; " + std::to_string(st.made) + " log slots made; " +
                  std::to_string(st.keptAt) + " teleports away from a party turned down.");
    return out;
}

bool PbpGroupQuestsTeleportScript::OnPlayerBeforeTeleport(Player* player, uint32 mapid, float x, float y,
                                                          float /*z*/, float /*orientation*/, uint32 /*options*/,
                                                          Unit* /*target*/)
{
    if (!player || !player->IsAlive())
        return true;
    if (mapid == player->GetMapId() && player->GetExactDist2d(x, y) < 300.0f)
        return true;
    bool say = false;
    {
        std::lock_guard<std::mutex> lock(g_keepMutex);
        uint64 const id = player->GetGUID().GetRawValue();
        if (!g_keep.contains(id))
            return true;
        auto const now = std::chrono::steady_clock::now();
        auto& said = g_keptSaid[id];
        if (said == std::chrono::steady_clock::time_point() || now - said >= std::chrono::minutes(1))
        {
            said = now;
            say = true;
        }
    }
    g_keptAt.fetch_add(1, std::memory_order_relaxed);
    if (say)
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] Group quests: kept {} (level {}) with its party -- "
                 "something tried to take it to map {} ({:.1f}, {:.1f}): playerbots' teleport on a level-up, most "
                 "likely.", player->GetName(), player->GetLevel(), mapid, x, y);
    return false;
}

PbpGroupQuestsStats PbpGroupQuests_GetStats()
{
    PbpGroupQuestsStats out = g_stats;
    out.keptAt = g_keptAt.load(std::memory_order_relaxed);
    out.quests = uint32(g_quests.size());
    out.givers = g_giverCount;
    out.partiesNow = uint32(g_parties.size());
    return out;
}
