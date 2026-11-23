#ifndef PLAYERBOTS_PLUS_SKILLS_H
#define PLAYERBOTS_PLUS_SKILLS_H

// --------------------------------------------------------------------------
// Professions are earned, never handed out.
//
// Playerbots sets every random bot's professions to five times its level
// each time it looks the bot over -- every level gained, every refresh,
// every re-roll (PlayerbotFactory::InitSkills: skill and cap both level x 5)
// -- so no bot ever had to gather or craft to be at the top of its trade.
// Here (Skills.*) a random bot's two professions, its cooking and its first
// aid rise only by the game's own skill-ups, from gathering and crafting
// (PlayerbotsPlusCrafting); what playerbots hands it is taken back:
//
//   * what a bot has the first time this sees it is its own, and it earns
//     on from there (the user's call); so is what playerbots gives one it
//     makes over from scratch (a full re-roll: its professions taken away
//     and given again, at a new level);
//   * every point the game gives it (a herb picked, an ore mined, a beast
//     skinned, a craft) is added to what it has earned;
//   * whenever playerbots sets one, it is put back to what the bot earned,
//     and its cap to the rank a player of its level with that skill would
//     have trained (PbpCraft::RankFor);
//   * a bot whose two professions are not a pair that suits its class
//     (Skills.ClassPairs: playerbots gave most bots made before mid-November
//     2026 any two) gets one that does, keeping whichever of its two fits,
//     the new one starting at the skill of the one it replaces; playerbots
//     is told the new pair (firstSkill, secondSkill), so it keeps it.
//
// Fishing is left as playerbots has it: nothing is made with it, and city
// life's fishing needs it.
//
// What each bot has earned is kept in the characters database
// (playerbots_plus_skills). The hooks run on map threads and only note what
// happened under a mutex; the world tick does the rest.
// --------------------------------------------------------------------------

#include "PlayerbotsPlusReport.h"
#include "ScriptMgr.h"

#include <cstdint>
#include <map>
#include <string>

class Player;

class PbpSkillsPlayerScript : public PlayerScript
{
public:
    PbpSkillsPlayerScript();

    void OnPlayerUpdateSkill(Player* player, uint32 skillId, uint32 value, uint32 max, uint32 step,
                             uint32 newValue) override;
    void OnPlayerSetSkill(Player* player, uint32 skillId, uint32 value, uint32 max, uint32 step,
                          uint32 newValue) override;
};

void PbpSkills_Load();                  // startup: what bots have earned
void PbpSkills_Update(uint32_t diff);   // world tick
void PbpSkills_Shutdown();              // what changed, written

struct PbpSkillsStats
{
    size_t   kept       = 0;   // professions of bots whose skill is kept now
    uint64_t firstSeen  = 0;   // ...first seen since startup: what they had, kept
    uint64_t points     = 0;   // points the game gave them since startup
    uint64_t putBack    = 0;   // times playerbots set one and it was put back
    uint64_t takenBack  = 0;   // ...points it had handed out, taken back
    uint64_t switched   = 0;   // pairs that did not suit the class, changed
    uint64_t dropped    = 0;   // notes dropped, too many at once
    uint64_t writes     = 0;   // times written to the database
    std::map<std::string, uint64_t> pointsBySkill;   // "Mining" -> points since startup
    std::map<std::string, uint64_t> switchedTo;      // "Mining and Blacksmithing" -> bots given it
};
PbpSkillsStats PbpSkills_GetStats();

// .pbp status and the report.
PbpBrief PbpSkills_Brief(bool full);

#endif // PLAYERBOTS_PLUS_SKILLS_H
