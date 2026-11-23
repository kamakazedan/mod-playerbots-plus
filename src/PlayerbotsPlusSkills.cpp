#include "PlayerbotsPlusSkills.h"
#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusCraftText.h"

#include "DatabaseEnv.h"
#include "Log.h"
#include "ObjectAccessor.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldSession.h"

#include "PlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"

#include <fmt/core.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <mutex>
#include <random>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace
{
    using namespace PbpCraft;

    static_assert(Skill::FirstAid == SKILL_FIRST_AID && Skill::Blacksmithing == SKILL_BLACKSMITHING &&
                  Skill::Leatherworking == SKILL_LEATHERWORKING && Skill::Alchemy == SKILL_ALCHEMY &&
                  Skill::Herbalism == SKILL_HERBALISM && Skill::Cooking == SKILL_COOKING &&
                  Skill::Mining == SKILL_MINING && Skill::Tailoring == SKILL_TAILORING &&
                  Skill::Engineering == SKILL_ENGINEERING && Skill::Enchanting == SKILL_ENCHANTING &&
                  Skill::Fishing == SKILL_FISHING && Skill::Skinning == SKILL_SKINNING &&
                  Skill::Jewelcrafting == SKILL_JEWELCRAFTING && Skill::Inscription == SKILL_INSCRIPTION,
                  "PbpCraft's skill lines are the game's");
    static_assert(Class::Warrior == CLASS_WARRIOR && Class::Paladin == CLASS_PALADIN && Class::Hunter == CLASS_HUNTER &&
                      Class::Rogue == CLASS_ROGUE && Class::Priest == CLASS_PRIEST &&
                      Class::DeathKnight == CLASS_DEATH_KNIGHT && Class::Shaman == CLASS_SHAMAN &&
                      Class::Mage == CLASS_MAGE && Class::Warlock == CLASS_WARLOCK && Class::Druid == CLASS_DRUID,
                  "PbpCraft's classes are the game's");

    // What is held: the two professions, and the secondary skills something
    // is made with. Not fishing (see the header).
    constexpr std::array<uint32_t, 13> kHeld = {
        Skill::Alchemy,     Skill::Blacksmithing, Skill::Enchanting,     Skill::Engineering, Skill::Herbalism,
        Skill::Inscription, Skill::Jewelcrafting, Skill::Leatherworking, Skill::Mining,      Skill::Skinning,
        Skill::Tailoring,   Skill::Cooking,       Skill::FirstAid };

    bool Held(uint32_t skill)
    {
        return std::find(kHeld.begin(), kHeld.end(), skill) != kHeld.end();
    }

    bool Gathering(uint32_t skill)
    {
        return skill == Skill::Herbalism || skill == Skill::Mining || skill == Skill::Skinning;
    }

    // ---- notes, from whatever thread a hook runs on ------------------------------------

    struct Note
    {
        uint32_t guid  = 0;      // the character's
        uint16_t skill = 0;
        uint16_t from  = 0;
        uint16_t to    = 0;
        bool     set   = false;  // set by someone (playerbots), not a skill-up the game rolled
    };

    std::mutex            g_noteMutex;
    std::vector<Note>     g_notes;          // under g_noteMutex
    uint64_t              g_dropped = 0;    // under g_noteMutex
    constexpr size_t      kMaxNotes = 100000;
    std::atomic<bool>     g_on{ false };    // notes taken at all (Skills.Enable, PlayerbotsPlus.Enable)

    // Set while this module sets a skill itself: its own SetSkill is no news.
    thread_local bool t_ours = false;

    void Push(Note const& note)
    {
        std::lock_guard<std::mutex> lock(g_noteMutex);
        if (g_notes.size() < kMaxNotes)
            g_notes.push_back(note);
        else
            ++g_dropped;
    }

    // A playerbots bot: nobody at its session. Whether it is a random one is
    // the world tick's to say.
    bool IsBot(Player* player)
    {
        WorldSession* session = player ? player->GetSession() : nullptr;
        return session && session->IsHeadless();
    }

    // ---- the world tick's own, from here on ----------------------------------------------

    constexpr uint32_t kWriteMs        = 1 * MINUTE * IN_MILLISECONDS;
    constexpr size_t   kRowsPerQuery   = 500;
    constexpr uint32_t kSwitchesAtOnce = 5;   // pairs changed a look: each teaches a trade's recipes

    uint64_t Key(uint32_t guid, uint32_t skill)
    {
        return (uint64_t(guid) << 16) | (skill & 0xFFFF);
    }

    std::unordered_map<uint64_t, uint16_t> g_earned;     // Key -> the skill it has earned
    std::unordered_set<uint64_t>           g_changed;    // written next time; gone ones deleted
    std::unordered_set<uint32_t>           g_putRight;   // bots playerbots set a skill of
    std::unordered_set<uint64_t>           g_fresh;      // Keys taken away: given again, they are as given
    std::vector<uint32_t>                  g_round;      // the bots being looked over
    size_t                                 g_roundAt    = 0;
    uint32_t                               g_timer      = 0;
    uint32_t                               g_writeTimer = 0;
    bool                                   g_loaded     = false;
    PbpSkillsStats                         g_stats;

    void Earned(uint64_t key, uint16_t value)
    {
        g_fresh.erase(key);
        auto [it, fresh] = g_earned.emplace(key, value);
        if (!fresh && it->second == value)
            return;
        it->second = value;
        g_changed.insert(key);
    }

    void Forget(uint64_t key)
    {
        if (g_earned.erase(key))
            g_changed.insert(key);
    }

    std::mt19937& Rng()
    {
        static std::mt19937 rng{ std::random_device{}() };
        return rng;
    }

    // One of playerbots' random bots, about (world thread).
    bool IsRandomBot(Player* bot)
    {
        return bot && bot->IsInWorld() && IsBot(bot) && sRandomPlayerbotMgr.IsRandomBot(bot);
    }

    void Drain()
    {
        std::vector<Note> notes;
        {
            std::lock_guard<std::mutex> lock(g_noteMutex);
            notes.swap(g_notes);
            g_stats.dropped = g_dropped;
        }
        for (Note const& n : notes)
        {
            const uint64_t key = Key(n.guid, n.skill);
            auto it = g_earned.find(key);
            if (!n.set)
            {
                // A skill-up the game rolled: earned. A skill not seen yet is
                // taken as it stands when it is, this point in it.
                if (it != g_earned.end() && n.to > n.from)
                {
                    Earned(key, uint16_t(std::min<uint32_t>(it->second + (n.to - n.from), 450)));
                    g_stats.points += n.to - n.from;
                    g_stats.pointsBySkill[Name(n.skill)] += n.to - n.from;
                }
                continue;
            }
            if (!n.to)
            {
                // Taken away: a trade it has no more, or playerbots making the
                // bot over from scratch (a full re-roll, at a new level), which
                // gives it back.
                Forget(key);
                g_fresh.insert(key);
                continue;
            }
            if (!n.from || g_fresh.contains(key))
                continue;   // given from nothing, or again: taken as it stands when it is seen
            if (it == g_earned.end())
            {
                // Not seen yet, and playerbots is setting it now: what it had
                // just before is its own (a random bot's: someone's own bot
                // is left as it is).
                if (!IsRandomBot(ObjectAccessor::FindPlayerByLowGUID(n.guid)))
                    continue;
                Earned(key, n.from);
                ++g_stats.firstSeen;
            }
            g_putRight.insert(n.guid);
        }
    }

    void SetSkill(Player* bot, uint32_t skill, Rank const& rank, uint32_t value)
    {
        t_ours = true;
        bot->SetSkill(uint16(skill), rank.step, uint16(value), uint16(rank.cap));
        t_ours = false;
    }

    // Each skill it has that is held, at what it earned and the cap of the
    // rank it would have trained. A skill not seen before is its own as it is.
    void Hold(Player* bot)
    {
        const uint32_t guid = bot->GetGUID().GetCounter();
        for (uint32_t skill : kHeld)
        {
            const uint64_t key = Key(guid, skill);
            const uint32_t value = bot->GetPureSkillValue(skill);
            if (!value)
            {
                Forget(key);
                continue;
            }
            auto it = g_earned.find(key);
            if (it == g_earned.end())
            {
                Earned(key, uint16_t(value));
                ++g_stats.firstSeen;
                it = g_earned.find(key);
            }
            const Rank rank = RankFor(bot->GetLevel(), it->second);
            const uint32_t want = std::min<uint32_t>(it->second, rank.cap);
            if (value == want && bot->GetPureMaxSkillValue(skill) == rank.cap)
                continue;
            if (value > want)
                g_stats.takenBack += value - want;
            ++g_stats.putBack;
            SetSkill(bot, skill, rank, want);
        }
    }

    // How much of a class's pairs have `skill` in them.
    uint32_t WeightWith(uint8_t cls, uint32_t skill)
    {
        uint32_t w = 0;
        for (Pair const& p : ClassPairs(cls))
            if (p.first == skill || p.second == skill)
                w += p.weight;
        return w;
    }

    // Its two professions a pair that suits its class, or made one: the one
    // of its two that fits kept, the other (or both) changed for what the
    // pair wants, each new one at the skill of the one it replaces.
    bool SuitClass(Player* bot)
    {
        std::vector<uint32_t> mine;
        for (uint32_t skill : kHeld)
            if (IsPrimary(skill) && bot->HasSkill(skill))
                mine.push_back(skill);
        if (mine.size() != 2)
            return false;   // playerbots tops one up, or has it as it wants
        const uint8_t cls = bot->getClass();
        const uint32_t a = mine[0], b = mine[1];
        if (PairFits(cls, a, b) || ClassPairs(cls).empty())
            return false;

        const uint32_t va = bot->GetPureSkillValue(a), vb = bot->GetPureSkillValue(b);
        const uint32_t wa = WeightWith(cls, a), wb = WeightWith(cls, b);
        uint32_t keep = 0;
        if (wa || wb)
            keep = wa > wb || (wa == wb && va >= vb) ? a : b;
        const uint32_t total = WeightOf(cls, keep);
        if (!total)
            return false;
        const auto [first, second] = PickPair(cls, keep, std::uniform_int_distribution<uint32_t>(1, total)(Rng()));
        if (!first || !second)
            return false;

        // Which old one each new one takes the place of: the one kept stays;
        // a gathering trade gives way to a gathering trade.
        std::vector<std::pair<uint32_t, uint32_t>> wanted;   // new, the skill it starts at
        std::vector<uint32_t> gone;
        if (keep)
        {
            const uint32_t fresh = first == keep ? second : first;
            const uint32_t old   = keep == a ? b : a;
            wanted.emplace_back(fresh, old == a ? va : vb);
            gone.push_back(old);
        }
        else
        {
            const bool aGathers = Gathering(a), bGathers = Gathering(b);
            uint32_t forFirst = std::max(va, vb), forSecond = std::min(va, vb);
            if (aGathers != bGathers)
            {
                forFirst  = aGathers == Gathering(first) ? va : vb;
                forSecond = forFirst == va ? vb : va;
            }
            wanted.emplace_back(first, forFirst);
            wanted.emplace_back(second, forSecond);
            gone.push_back(a);
            gone.push_back(b);
        }

        const uint32_t guid = bot->GetGUID().GetCounter();
        t_ours = true;
        for (uint32_t old : gone)
        {
            bot->SetSkill(uint16(old), 0, 0, 0);
            Forget(Key(guid, old));
        }
        for (auto const& [skill, value] : wanted)
            if (const uint32_t spell = ApprenticeSpell(skill))
                bot->learnSpell(spell, false);
        t_ours = false;
        for (auto const& [skill, value] : wanted)
        {
            const uint32_t start = std::max<uint32_t>(1, value);
            SetSkill(bot, skill, RankFor(bot->GetLevel(), start), start);
            Earned(Key(guid, skill), uint16_t(start));
        }
        // Its recipes and ranks up to the skill it has, as playerbots teaches
        // a bot at its trainer; then its skill as earned again (a rank it
        // learns sets its cap).
        t_ours = true;
        PlayerbotFactory(bot, bot->GetLevel()).InitAvailableSpells();
        t_ours = false;
        for (auto const& [skill, value] : wanted)
        {
            const uint32_t start = std::max<uint32_t>(1, value);
            SetSkill(bot, skill, RankFor(bot->GetLevel(), start), start);
        }

        // Playerbots keeps the pair it is told of (InitTradeSkills), and
        // chooses for the class from now on.
        sRandomPlayerbotMgr.SetValue(bot, "firstSkill", first);
        sRandomPlayerbotMgr.SetValue(bot, "secondSkill", second);
        sRandomPlayerbotMgr.SetValue(bot, "professionRollType", 2);   // PlayerbotFactory's Class
        ++g_stats.switched;
        ++g_stats.switchedTo[Name(first) + " and " + Name(second)];
        if (pbp::g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} ({}) changed {} and {} for {} and {}.",
                     bot->GetName(), uint32(cls), Name(a), Name(b), Name(first), Name(second));
        return true;
    }

    // A few bots a look: what they have first seen, a pair for their class,
    // and every skill held where it should be.
    void LookOver()
    {
        if (g_roundAt >= g_round.size())
        {
            g_round.clear();
            g_roundAt = 0;
            for (auto it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
                if (it->second)
                    g_round.push_back(it->second->GetGUID().GetCounter());
        }
        PbpSkillSettings const& s = g_Pbp.skills;
        const size_t end = std::min(g_round.size(), g_roundAt + s.botsPerInterval);
        uint32_t switches = 0;
        for (; g_roundAt < end; ++g_roundAt)
        {
            Player* bot = ObjectAccessor::FindPlayerByLowGUID(g_round[g_roundAt]);
            if (!IsRandomBot(bot))
                continue;
            if (s.classPairs && switches < kSwitchesAtOnce && SuitClass(bot))
                ++switches;
            Hold(bot);
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
                trans->Append("REPLACE INTO playerbots_plus_skills (guid, skill, earned) VALUES " + kept);
            kept.clear();
            keptRows = 0;
        };
        auto writeGone = [&]()
        {
            if (goneRows)
                trans->Append("DELETE FROM playerbots_plus_skills WHERE " + gone);
            gone.clear();
            goneRows = 0;
        };
        for (uint64_t key : g_changed)
        {
            const uint32_t guid = uint32_t(key >> 16), skill = uint32_t(key & 0xFFFF);
            if (auto it = g_earned.find(key); it != g_earned.end())
            {
                kept += fmt::format("{}({},{},{})", keptRows ? "," : "", guid, skill, it->second);
                if (++keptRows >= kRowsPerQuery)
                    writeKept();
            }
            else
            {
                gone += fmt::format("{}(guid={} AND skill={})", goneRows ? " OR " : "", guid, skill);
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

PbpSkillsPlayerScript::PbpSkillsPlayerScript()
    : PlayerScript("PbpSkillsPlayerScript", { PLAYERHOOK_ON_UPDATE_SKILL, PLAYERHOOK_ON_SET_SKILL })
{
}

void PbpSkillsPlayerScript::OnPlayerUpdateSkill(Player* player, uint32 skillId, uint32 value, uint32 /*max*/,
                                                uint32 /*step*/, uint32 newValue)
{
    if (!g_on.load(std::memory_order_relaxed) || !Held(skillId) || newValue <= value || !IsBot(player))
        return;
    Push({ player->GetGUID().GetCounter(), uint16_t(skillId), uint16_t(value), uint16_t(newValue), false });
}

void PbpSkillsPlayerScript::OnPlayerSetSkill(Player* player, uint32 skillId, uint32 value, uint32 /*max*/,
                                             uint32 /*step*/, uint32 newValue)
{
    if (t_ours || !g_on.load(std::memory_order_relaxed) || !Held(skillId) || !IsBot(player))
        return;
    Push({ player->GetGUID().GetCounter(), uint16_t(skillId), uint16_t(value), uint16_t(newValue), true });
}

// ---- the world thread -------------------------------------------------------------------

void PbpSkills_Load()
{
    // The SQL in data/sql makes it too; this is for a server whose updater is off.
    CharacterDatabase.DirectExecute(
        "CREATE TABLE IF NOT EXISTS `playerbots_plus_skills` ("
        "`guid` INT UNSIGNED NOT NULL, "
        "`skill` SMALLINT UNSIGNED NOT NULL, "
        "`earned` SMALLINT UNSIGNED NOT NULL DEFAULT 0, "
        "PRIMARY KEY (`guid`, `skill`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COMMENT='mod-playerbots-plus: the profession skill random bots have "
        "earned'");
    // Characters gone since: forgotten.
    CharacterDatabase.DirectExecute(
        "DELETE s FROM playerbots_plus_skills AS s LEFT JOIN characters AS c ON c.guid = s.guid WHERE c.guid IS NULL");

    g_earned.clear();
    g_changed.clear();
    if (QueryResult result = CharacterDatabase.Query("SELECT guid, skill, earned FROM playerbots_plus_skills"))
    {
        do
        {
            Field* f = result->Fetch();
            g_earned[Key(f[0].Get<uint32>(), f[1].Get<uint16>())] = f[2].Get<uint16>();
        } while (result->NextRow());
    }
    g_loaded = true;
    LOG_INFO("server.loading", "[Playerbots Plus] Professions earned, not handed out: {} bots' professions known.",
             g_earned.size());
}

void PbpSkills_Update(uint32_t diff)
{
    if (!g_loaded)
        return;
    const bool on = g_Pbp.enable && g_Pbp.skills.enable;
    g_on.store(on, std::memory_order_relaxed);
    if (!on)
    {
        // Off: nothing held, nothing counted; what they earned is kept.
        std::lock_guard<std::mutex> lock(g_noteMutex);
        g_notes.clear();
        g_putRight.clear();
    }
    else
    {
        // What the game gave and playerbots set first, then put right at once
        // whoever playerbots just set a skill of.
        Drain();
        for (uint32_t guid : g_putRight)
        {
            Player* bot = ObjectAccessor::FindPlayerByLowGUID(guid);
            if (IsRandomBot(bot))
                Hold(bot);
        }
        g_putRight.clear();

        g_timer += diff;
        if (g_timer >= g_Pbp.skills.intervalMs)
        {
            g_timer = 0;
            LookOver();
        }
    }

    g_writeTimer += diff;
    if (g_writeTimer >= kWriteMs)
    {
        g_writeTimer = 0;
        Write(false);
    }
}

void PbpSkills_Shutdown()
{
    if (!g_loaded)
        return;
    if (g_on.load(std::memory_order_relaxed))
        Drain();
    Write(true);
}

PbpSkillsStats PbpSkills_GetStats()
{
    PbpSkillsStats s = g_stats;
    s.kept = g_earned.size();
    std::lock_guard<std::mutex> lock(g_noteMutex);
    s.dropped = g_dropped;
    return s;
}

PbpBrief PbpSkills_Brief(bool full)
{
    PbpBrief b;
    PbpSkillsStats const s = PbpSkills_GetStats();
    const bool on = g_Pbp.enable && g_Pbp.skills.enable;
    b.lines.push_back(fmt::format("Professions: {} -- earned, not handed out: {} kept ({} first seen since "
                                  "startup); {} points the game gave since startup; playerbots' hand-outs put back {} "
                                  "times ({} points); {} pairs changed to suit their class.",
                                  on ? "on" : "OFF", s.kept, s.firstSeen, s.points, s.putBack, s.takenBack,
                                  s.switched));
    if (full)
    {
        std::string by;
        for (auto const& [name, n] : s.pointsBySkill)
            by += fmt::format("{}{} {}", by.empty() ? "" : ", ", name, n);
        b.lines.push_back("Points earned since startup, by profession: " + (by.empty() ? std::string("none yet") : by) +
                          ".");
        std::string to;
        for (auto const& [pair, n] : s.switchedTo)
            to += fmt::format("{}{} {}", to.empty() ? "" : ", ", pair, n);
        if (!to.empty())
            b.lines.push_back("Pairs given to suit the class: " + to + ".");
        b.lines.push_back(fmt::format("Written to the database {} times; {} notes dropped.", s.writes, s.dropped));
    }
    if (on && s.dropped)
        b.issues.push_back(fmt::format("Professions: {} skill notes dropped, too many at once.", s.dropped));
    return b;
}
