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
#include <functional>
#include <map>
#include <mutex>
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
    static_assert(Race::Human == RACE_HUMAN && Race::Orc == RACE_ORC && Race::Dwarf == RACE_DWARF &&
                      Race::NightElf == RACE_NIGHTELF && Race::Undead == RACE_UNDEAD_PLAYER &&
                      Race::Tauren == RACE_TAUREN && Race::Gnome == RACE_GNOME && Race::Troll == RACE_TROLL &&
                      Race::BloodElf == RACE_BLOODELF && Race::Draenei == RACE_DRAENEI,
                  "PbpCraft's races are the game's");

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
    std::unordered_set<uint32_t>           g_newPair;    // bots playerbots gave a profession they never had
    std::function<std::string(Player*)>    g_leaningProvider;   // PlayerbotsPlus::SetLeaningProvider
    std::map<std::string, std::map<std::string, uint64_t>> g_roundLeanings;   // this round's, as they stand
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
            if (!n.from && !g_fresh.contains(key) && IsPrimary(n.skill) &&
                IsRandomBot(ObjectAccessor::FindPlayerByLowGUID(n.guid)))
                g_newPair.insert(n.guid);   // one it never had: a new bot's pair, playerbots' roll
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

    // The kind of person it is, as the chat module says (Skills.Leanings):
    // null for none.
    Leaning const* LeaningOf(Player* bot)
    {
        PbpSkillSettings const& s = g_Pbp.skills;
        if (!s.leanings || !g_leaningProvider || !bot)
            return nullptr;
        auto it = s.leaningsByName.find(LeaningKey(g_leaningProvider(bot)));
        return it == s.leaningsByName.end() ? nullptr : &it->second;
    }

    // Its two professions, or fewer: playerbots tops one up, or has it as it wants.
    std::vector<uint32_t> PairOf(Player* bot)
    {
        std::vector<uint32_t> mine;
        for (uint32_t skill : kHeld)
            if (IsPrimary(skill) && bot->HasSkill(skill))
                mine.push_back(skill);
        return mine;
    }

    // A bot without a leaning goes by its class's pairs alone.
    Leaning const& ClassOnly()
    {
        static const Leaning l = []
        {
            Leaning c;
            c.classShare = 100;
            return c;
        }();
        return l;
    }

    // Its two professions a pair that suits it, or made one. Changed: a pair
    // that suits neither its class nor its leaning (Skills.ClassPairs), or
    // has a profession its leaning will never have; and a pair of its
    // class's where its leaning's own pick for it -- rolled the same for it
    // every time -- is one of the leaning's, for a bot playerbots has just
    // given its pair (a new bot) or with Skills.LeaningsChangeExisting. Of
    // its two, the one that fits is kept where it can be, and each new one
    // starts at the skill of the one it replaces.
    bool Suit(Player* bot, bool newPair)
    {
        const std::vector<uint32_t> mine = PairOf(bot);
        if (mine.size() != 2)
            return false;
        PbpSkillSettings const& s = g_Pbp.skills;
        const uint8_t cls = bot->getClass(), race = bot->getRace();
        if (ClassPairs(cls).empty())
            return false;
        const uint32_t a = mine[0], b = mine[1];
        Leaning const* leaning = LeaningOf(bot);
        Leaning const& l = leaning ? *leaning : ClassOnly();
        const Match match = MatchOf(l, cls, race, a, b);
        const std::vector<Pick> picks = Picks(l, cls, race);
        // Its own roll, not the temperament's (city life rolls by the guid too).
        const uint64_t seed = bot->GetGUID().GetRawValue() ^ 0x50524F46u;
        const uint32_t va = bot->GetPureSkillValue(a), vb = bot->GetPureSkillValue(b);

        Pick to;
        uint32_t keep = 0;
        if (match == Match::None || match == Match::Never)
        {
            if (match == Match::None && !s.classPairs)
                return false;
            // The one of its two its picks want the more -- none want one it
            // will never have -- the higher skill when that is the same.
            auto weightWith = [&picks](uint32_t skill)
            {
                uint32_t w = 0;
                for (Pick const& p : picks)
                    if (p.first == skill || p.second == skill)
                        w += p.weight;
                return w;
            };
            const uint32_t wa = weightWith(a), wb = weightWith(b);
            if (wa || wb)
                keep = wa > wb || (wa == wb && va >= vb) ? a : b;
            to = PickOne(picks, keep, StableRoll(seed, PicksWeight(picks, keep)));
        }
        else if (leaning && match == Match::Class && (newPair || s.changeExisting))
        {
            to = PickOne(picks, 0, StableRoll(seed, PicksWeight(picks, 0)));
            if (to.from == Match::Class)
                return false;   // its own pick is its class's: the class's pair it has will do
            keep = to.first == a || to.second == a ? a : to.first == b || to.second == b ? b : 0;
        }
        else
            return false;
        const uint32_t first = to.first, second = to.second;
        if (!first || !second || (first == a && second == b) || (first == b && second == a))
            return false;
        if (keep && first != keep && second != keep)
            keep = 0;   // its picks had none with it after all

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
        const std::string pair = Name(first) + " and " + Name(second);
        if (match == Match::None)
        {
            ++g_stats.switched;
            ++g_stats.switchedTo[pair];
        }
        else
        {
            // For its leaning: its own pick, or none it would never have.
            ++(match == Match::Never ? g_stats.refused : g_stats.leaned);
            ++g_stats.leanedTo[(leaning ? leaning->name : std::string("none")) + ": " + pair];
        }
        if (pbp::g_PbpDebug)
            LOG_INFO("module.playerbotsplus", "[Playerbots Plus] {} ({}, {}) changed {} and {} for {} and {} ({}).",
                     bot->GetName(), uint32(cls), leaning ? leaning->name : std::string("no leaning"), Name(a),
                     Name(b), Name(first), Name(second), MatchName(match));
        return true;
    }

    // How it stands with its leaning, for this round's counts: the leaning,
    // and where its pair stands with it.
    void Count(Player* bot)
    {
        Leaning const* leaning = LeaningOf(bot);
        const std::vector<uint32_t> mine = PairOf(bot);
        if (!leaning || mine.size() != 2)
            return;
        const Match m = MatchOf(*leaning, bot->getClass(), bot->getRace(), mine[0], mine[1]);
        ++g_roundLeanings[leaning->name][MatchName(m)];
    }

    // A few bots a look: what they have first seen, a pair for their class,
    // and every skill held where it should be.
    void LookOver()
    {
        if (g_roundAt >= g_round.size())
        {
            // The round just done: how the bots stood with their leanings.
            if (!g_round.empty())
                g_stats.byLeaning = std::move(g_roundLeanings);
            g_roundLeanings.clear();
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
            const uint32_t guid = g_round[g_roundAt];
            Player* bot = ObjectAccessor::FindPlayerByLowGUID(guid);
            if (!IsRandomBot(bot))
                continue;
            // A new bot's pair waits for a look with a change to spare.
            if ((s.classPairs || s.leanings) && switches < kSwitchesAtOnce && Suit(bot, g_newPair.erase(guid) > 0))
                ++switches;
            Count(bot);
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

void PbpSkills_SetLeaningProvider(std::function<std::string(Player*)> provider)
{
    g_leaningProvider = std::move(provider);
}

PbpCraft::Leaning const* PbpSkills_LeaningOf(Player* bot)
{
    return LeaningOf(bot);
}

std::string PbpSkills_LeaningMatch(Player* bot)
{
    Leaning const* leaning = LeaningOf(bot);
    if (!leaning)
        return {};
    const std::vector<uint32_t> mine = PairOf(bot);
    if (mine.size() != 2)
        return {};
    const Match m = MatchOf(*leaning, bot->getClass(), bot->getRace(), mine[0], mine[1]);
    return m == Match::None || m == Match::Never ? std::string() : std::string(MatchName(m));
}

std::string PbpSkills_PairKey(Player* bot)
{
    std::vector<uint32_t> mine = PairOf(bot);
    if (mine.size() != 2)
        return {};
    if (Gathering(mine[1]) && !Gathering(mine[0]))
        std::swap(mine[0], mine[1]);   // the gathering one first, as the class pairs are
    return Name(mine[0]) + "+" + Name(mine[1]);
}

PbpBrief PbpSkills_Brief(bool full)
{
    PbpBrief b;
    PbpSkillsStats const s = PbpSkills_GetStats();
    PbpSkillSettings const& set = g_Pbp.skills;
    const bool on = g_Pbp.enable && set.enable;
    const char* leanings = !set.leanings ? "off" : !g_leaningProvider ? "no chat module to say who leans which way"
                                                 : set.changeExisting ? "every bot" : "new bots";
    b.lines.push_back(fmt::format("Professions: {} -- earned, not handed out: {} kept ({} first seen since "
                                  "startup); {} points the game gave since startup; playerbots' hand-outs put back {} "
                                  "times ({} points); {} pairs changed that suited neither their class nor their "
                                  "leaning.",
                                  on ? "on" : "OFF", s.kept, s.firstSeen, s.points, s.putBack, s.takenBack,
                                  s.switched));
    b.lines.push_back(fmt::format("Leanings ({}): {} pairs changed to the kind of person a bot is, {} for one it would "
                                  "never have.",
                                  leanings, s.leaned, s.refused));
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
            b.lines.push_back("Pairs given for one that suited neither: " + to + ".");
        std::string leaned;
        for (auto const& [pair, n] : s.leanedTo)
            leaned += fmt::format("{}{} {}", leaned.empty() ? "" : ", ", pair, n);
        if (!leaned.empty())
            b.lines.push_back("Pairs given for a leaning: " + leaned + ".");
        // The last full round over the bots online: each leaning, and where
        // its bots' pairs stand with it (own: one it names; armour, race,
        // rare: what those stand for; class: its class's; none; never).
        for (auto const& [name, matches] : s.byLeaning)
        {
            uint64_t all = 0;
            std::string how;
            for (auto const& [match, n] : matches)
            {
                all += n;
                how += fmt::format("{}{} {}", how.empty() ? "" : ", ", match, n);
            }
            b.lines.push_back(fmt::format("Leaning {}: {} bots online ({}).", name, all, how));
        }
        b.lines.push_back(fmt::format("Written to the database {} times; {} notes dropped.", s.writes, s.dropped));
    }
    if (on && s.dropped)
        b.issues.push_back(fmt::format("Professions: {} skill notes dropped, too many at once.", s.dropped));
    return b;
}
