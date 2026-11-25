#ifndef PLAYERBOTS_PLUS_CRAFT_TEXT_H
#define PLAYERBOTS_PLUS_CRAFT_TEXT_H

// --------------------------------------------------------------------------
// Professions' sums, with no game types in them so they can be tested
// without a server (tests/craft_test.cpp): how likely a craft is to raise a
// bot's skill, which rank of a profession a bot of a level and a skill has
// trained, which profession pairs suit a class, what a bot crafts from what
// it has, what breaking something down gives (disenchanting, milling,
// prospecting), and what it is short of on its way to a point.
//
// The game side is PlayerbotsPlusCrafting (the crafting) and
// PlayerbotsPlusSkills (skill earned, never handed out).
// --------------------------------------------------------------------------

#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace PbpCraft
{
    // Skill lines, as numbers (SkillLine.dbc). PlayerbotsPlusSkills checks
    // them against the game's own enum when it is compiled.
    namespace Skill
    {
        constexpr uint32_t FirstAid       = 129;
        constexpr uint32_t Blacksmithing  = 164;
        constexpr uint32_t Leatherworking = 165;
        constexpr uint32_t Alchemy        = 171;
        constexpr uint32_t Herbalism      = 182;
        constexpr uint32_t Cooking        = 185;
        constexpr uint32_t Mining         = 186;
        constexpr uint32_t Tailoring      = 197;
        constexpr uint32_t Engineering    = 202;
        constexpr uint32_t Enchanting     = 333;
        constexpr uint32_t Fishing        = 356;
        constexpr uint32_t Skinning       = 393;
        constexpr uint32_t Jewelcrafting  = 755;
        constexpr uint32_t Inscription    = 773;
    }

    // Classes, as numbers (ChrClasses.dbc).
    namespace Class
    {
        constexpr uint8_t Warrior     = 1;
        constexpr uint8_t Paladin     = 2;
        constexpr uint8_t Hunter      = 3;
        constexpr uint8_t Rogue       = 4;
        constexpr uint8_t Priest      = 5;
        constexpr uint8_t DeathKnight = 6;
        constexpr uint8_t Shaman      = 7;
        constexpr uint8_t Mage        = 8;
        constexpr uint8_t Warlock     = 9;
        constexpr uint8_t Druid       = 11;
    }

    // One of the two professions a character may have (not cooking, first
    // aid, fishing or riding).
    bool IsPrimary(uint32_t skill);

    // Its name, for the log and the report: "Blacksmithing". Empty: not one.
    std::string Name(uint32_t skill);

    // The spell that teaches a profession at Apprentice (Blacksmithing
    // 2018...). 0: not a profession.
    uint32_t ApprenticeSpell(uint32_t skill);

    // ---- skill-ups ----------------------------------------------------------

    // In percent, the chance one craft raises the skill, the way this core
    // works it out (CraftSkillGainChance, with SkillChance.Orange 100 and
    // SkillChance.Grey 0): certain up to `yellow` (TrivialSkillLineRankLow),
    // none from `grey` (TrivialSkillLineRankHigh), falling evenly between.
    uint32_t PointChance(uint32_t skill, uint32_t yellow, uint32_t grey);

    // ---- ranks ---------------------------------------------------------------

    // A profession's rank: Apprentice (1) to Grand Master (6), the skill it
    // goes up to, and what a character needs to train it.
    struct Rank
    {
        uint8_t  step     = 1;
        uint32_t cap      = 75;
        uint32_t minLevel = 1;
        uint32_t minSkill = 0;
    };

    // The highest rank a character of `level` with `skill` would have
    // trained, going to the trainer as soon as it could: Journeyman from
    // level 10 with 50 skill, Expert from 20 with 125, Artisan from 35 with
    // 200, Master from 50 with 275, Grand Master from 65 with 350.
    Rank RankFor(uint32_t level, uint32_t skill);

    // ---- pairs for a class ---------------------------------------------------

    struct Pair
    {
        uint32_t first  = 0;   // the gathering one, or the first
        uint32_t second = 0;
        uint32_t weight = 0;
    };

    // The pairs that suit a class, and how often each is taken: playerbots'
    // own (PlayerbotFactory::GetClassProfessionPairs), so a bot it rolls for
    // its class and one put right here take the same.
    std::vector<Pair> ClassPairs(uint8_t cls);

    // Is {a, b}, either way round, one of the class's pairs?
    bool PairFits(uint8_t cls, uint32_t a, uint32_t b);

    // A pair for a bot of `cls` whose pair does not fit: one with `keep` in
    // it if any has (keep: a profession it has, 0 for none), weighted; `roll`
    // from 1 to the weights' total (WeightOf). {0, 0} with none.
    std::pair<uint32_t, uint32_t> PickPair(uint8_t cls, uint32_t keep, uint32_t roll);

    // The total of the weights PickPair rolls over.
    uint32_t WeightOf(uint8_t cls, uint32_t keep);

    // ---- what to craft -------------------------------------------------------

    // One recipe as it stands for a bot just now.
    struct Option
    {
        uint32_t spell       = 0;
        uint32_t pointChance = 0;      // percent: a skill point from one craft (PointChance)
        uint32_t canMake     = 0;      // crafts what it has, and can buy where it is, allow
        uint64_t costEach    = 0;      // copper: what goes into one craft is worth
        uint64_t worthEach   = 0;      // copper: what one craft makes is worth
        bool     upgrade     = false;  // what it makes is better than what it wears
        bool     sellable    = true;   // what it makes could go on the market at all
        uint32_t steps       = 0;      // what it gives is wanted for a craft that would give a point, this many
                                       // steps on (Wanted); 0: not
    };

    enum class Why : uint8_t
    {
        None,
        Use,        // an upgrade for itself
        Skill,      // a chance at a point
        Materials,  // what a craft that would give a point is short of
        Sell        // worth more than what goes into it
    };

    struct Choice
    {
        size_t   index = SIZE_MAX;   // into the options
        uint32_t casts = 0;
        Why      why   = Why::None;
    };

    struct ChoiceSettings
    {
        uint32_t maxCasts      = 10;    // crafts at one go
        uint32_t minChance     = 25;    // percent: a skill-up worth the materials
        uint32_t sellMarginPct = 130;   // made to sell: worth at least this share of what goes into it
    };

    // What it crafts: an upgrade for itself first, one craft; else the
    // cheapest chance at a point (the least copper of materials for each
    // point it can expect), as many as it can up to maxCasts; else what a
    // craft that would give a point is short of (`steps`: the nearest to the
    // point first, the cheaper then) -- bars for the smith, ink for the
    // scribe, dust for the enchanter; else what makes it the most over its
    // materials. Nothing worth doing: Why::None.
    Choice Choose(std::vector<Option> const& options, ChoiceSettings const& s);

    char const* WhyName(Why why);   // "use", "skill", "materials", "sell", "none"

    // ---- on the way to a point -------------------------------------------------

    // A craft a bot knows, as it stands: what a point from it is worth
    // asking, what it makes, and what one takes.
    struct Known
    {
        uint32_t pointChance = 0;   // percent
        uint32_t product     = 0;   // 0: nothing to carry (an enchant on its gear)
        std::vector<std::pair<uint32_t, uint32_t>> needs;   // item, how many for one: reagents, a tool
    };

    // What it is short of on its way to a point, each thing with how many
    // steps from the point it is: 1 for what a craft with `minChance` or
    // more at a point takes and it has too few of, 2 for what a craft that
    // makes one of those takes, and so on to `maxSteps`. `have`: what it
    // carries, by item.
    std::unordered_map<uint32_t, uint32_t> Wanted(std::vector<Known> const& known,
                                                  std::unordered_map<uint32_t, uint32_t> const& have,
                                                  uint32_t minChance, uint32_t maxSteps = 3);

    // ---- breaking things down ---------------------------------------------------

    // A row of a loot table (disenchant_, milling_, prospecting_ and
    // reference_loot_template), as the core reads it.
    struct LootRow
    {
        uint32_t item      = 0;
        uint32_t reference = 0;      // another table rolled instead, 0 for none
        float    chance    = 0.0f;   // percent; 0 in a group: one of the rest of it, all alike
        uint8_t  group     = 0;      // 0: rolled by itself
        uint8_t  minCount  = 1;
        uint8_t  maxCount  = 1;      // a reference: how many times its table is rolled
    };
    using LootTable  = std::vector<LootRow>;
    using LootTables = std::unordered_map<uint32_t, LootTable>;   // by entry
    using Yields     = std::unordered_map<uint32_t, double>;      // item -> how many, on average

    // What one go at a table gives on average, the way the core rolls it: a
    // row outside any group by itself at its chance; a group once, its rows
    // with a chance first, in order, then one of the rest alike; a reference
    // its whole table, `maxCount` times.
    Yields Expected(LootTable const& table, LootTables const& references, uint32_t depth = 0);

    // In percent, the chance milling or prospecting something that asks for
    // `required` skill raises a skill of `skill`, the way this core works it
    // out where it is switched on (UpdateGatherSkill, with the stock
    // SkillChance.*): certain within 25 of it, 75 within 50, 25 within 100,
    // none from there.
    uint32_t GatherChance(uint32_t skill, uint32_t required);
}

#endif // PLAYERBOTS_PLUS_CRAFT_TEXT_H
