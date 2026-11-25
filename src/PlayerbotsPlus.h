#ifndef PLAYERBOTS_PLUS_H
#define PLAYERBOTS_PLUS_H

// --------------------------------------------------------------------------
// mod-playerbots-plus: what other modules in the same build can use.
//
// Modules are compiled into one worldserver with each other's src/ on the
// include path, so a module that wants this asks for it without depending on
// it:
//
//     #if __has_include("PlayerbotsPlus.h")
//     #include "PlayerbotsPlus.h"
//     #endif
//
// This module makes bots do things and never has them talk about it. A chat
// module that wants them to listens for the events below and says it in its
// own words.
//
// Everything here is called on the world thread and calls back on it, except
// UsingBench and OwnLine, which work on any thread.
// --------------------------------------------------------------------------

#include "ObjectGuid.h"

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

class Item;
class Player;

// 1: the market. 2: hangouts, bot events and OwnLine. 3: duels talked into.
// 4: group quests (BotEvent's GroupQuest* types, quest and need).
// 5: city life (CityBusy, CitySpoke, CityGather; BotEvent's City* types).
// 6: city life by temperament (SetTemperamentProvider), showing off
// (BotEventType::CityShowOff, CityCalmDown).
// 7: what things are worth (ItemWorth) and how rarely they drop (DropChance).
// 8: where things drop (DropSourcesOf).
// 9: only its own loot is sold (LootedByBot).
// 10: group quests talked into (GroupQuestFor, GroupQuestTogether).
// 11: what a bot made itself with its professions is its own to sell too
//     (LootedByBot).
// 12: disenchanting (CanDisenchant, DisenchantWorth, BoughtToDisenchant).
#define PLAYERBOTS_PLUS_API_VERSION 12

namespace PlayerbotsPlus
{
    // ---- Random bots trading on the auction house (PlayerbotsPlus.Market.*)

    enum class MarketEventType : uint8_t
    {
        Listed,    // a bot put something up for auction
        Sold,      // ...and somebody bought it (the bot may be offline by then)
        Expired,   // ...and nobody did: it went back to the bot by mail
        Bought     // a bot bought something there
    };

    struct MarketEvent
    {
        MarketEventType type = MarketEventType::Listed;
        ObjectGuid      bot;          // the random bot
        uint32_t        itemId = 0;
        uint32_t        count  = 0;   // stack size
        uint64_t        money  = 0;   // copper: the buyout asked (Listed, Expired), paid (Sold, Bought)
        ObjectGuid      other;        // who bought it (Sold) or sold it (Bought), when known
    };

    using MarketListener = std::function<void(MarketEvent const&)>;

    // Called for every market event from now on, on the world thread. For
    // modules that want the bots to talk about their trading.
    void AddMarketListener(MarketListener listener);

    // What one of it is worth with nobody selling one on the auction house:
    // what a bot asks for it there, before shading it a little up or down
    // (PlayerbotsPlus.Market.Price.*) -- a multiple of its vendor price by
    // quality, far more the rarer it drops, within what someone of its level
    // would pay (someone richer, the rarer it is). 0: no such item.
    uint64_t ItemWorth(uint32_t itemId);

    // How rarely it drops, in percent: per kill of a creature that can drop
    // it, over all the world has of them -- or, for a thing only chests give,
    // the best chest's. 0 when nothing drops it. From the world's own loot
    // tables, worked out at startup.
    double DropChance(uint32_t itemId);

    // Where it drops, for someone asking where to find it: how many kinds of
    // creature drop it and how many of them the world has, the levels most of
    // those are, the likeliest three to (a creature of each, with the chance
    // a kill of it gives it), and whether a chest gives it too. False when
    // nothing drops it.
    struct DropSources
    {
        uint32_t kinds    = 0;        // kinds of creature (loot tables) that drop it
        uint32_t spawns   = 0;        // how many of those creatures the world has
        uint32_t minLevel = 0;        // the levels most of them are; 0 when none is out in the world
        uint32_t maxLevel = 0;
        uint32_t creature[3]  = {};   // creature entries, the likeliest first; 0: no more
        double   chancePct[3] = {};   // ...the chance a kill of each gives it, percent
        double   bestPct  = 0.0;      // the most any one creature or chest gives it, percent
        int32_t  onlyMap  = -1;       // the map nine in ten of those creatures are on (a dungeon's, say); -1: none is
        bool     chests   = false;    // a chest or a fishing hole gives it too
    };
    bool DropSourcesOf(uint32_t itemId, DropSources& out);

    // ---- A bot sells only what it looted or made itself (PlayerbotsPlusLooted)

    // Did `bot` loot this stack itself -- from a kill, a chest, a herb or an
    // ore vein, a skinned beast, fishing, or won on a roll -- or make it with
    // its professions (from API 11), with nothing but more of its own added
    // to it since? A bot sells only such a stack, wherever it sells: never
    // what playerbots handed it for nothing (its gear, ammo, food, potions,
    // reagents, stones and oils), nor anything it bought, was given or got
    // from a quest. `item` is one in its bags.
    bool LootedByBot(Player* bot, Item* item);

    // ---- Disenchanting (PlayerbotsPlusCrafting), from API 12

    // Could `bot` disenchant one of these: an enchanter with the skill for
    // it, where the game's own loot tables say what it would give?
    bool CanDisenchant(Player* bot, uint32_t itemId);

    // What disenchanting one gives is worth on average, in copper, by
    // ItemWorth's own sums over what the loot tables say comes out. 0:
    // nothing known.
    uint64_t DisenchantWorth(uint32_t itemId);

    // `item`, now in `bot`'s bags, was bought to be disenchanted (a deal in
    // Trade chat): it may be, for the next few hours, though the bot neither
    // looted nor made it -- it breaks down only its own otherwise, never
    // what playerbots hands it -- and the bot gets to it as soon as it is
    // free. What comes out is its own, like anything it breaks down.
    void BoughtToDisenchant(Player* bot, Item* item);

    // ---- City hangouts (PlayerbotsPlus.Hangout.*)

    // `doing` is said to the bot, for a prompt: "sitting on a bench", "sat on
    // your mount", "sitting on the ground" or "standing about".
    struct HangoutState
    {
        std::string spot;               // "the Trade District in Stormwind"
        std::string doing;
        bool        away     = false;   // AFK right now (the flag the hangout set)
        bool        justBack = false;   // was AFK there, and came back in the last few minutes
    };

    // Settled at a hangout spot right now: where, and how. False otherwise.
    bool GetHangout(Player* bot, HangoutState& out);

    // AFK at a hangout. A bot that has since been invited, traded with or
    // attacked is let go here and now, and is not.
    bool IsAway(Player* bot);

    // Someone talked to it, or it is about to talk: back at the keyboard.
    void ComeBack(Player* bot);

    // True while a hanging-out bot is sitting down on a bench on this thread,
    // so "someone used an object" does not count as news. Any thread.
    bool UsingBench();

    // ---- Duels at the city gates (PlayerbotsPlus.Duels.*)

    // Whether two bots could go out to the gates and duel there, now: two of
    // playerbots' random bots of a side, both of Duels.MinLevel or more and
    // within Duels.LevelGap of each other, on their own and free, near a duel
    // spot (Duels.Spots) they could walk out to. `where`, for a prompt: the
    // spot's name ("outside the gates of Stormwind"). A chat module that has
    // two bots talk themselves into a duel asks this first.
    bool CanGoDuel(Player* a, Player* b, std::string& where);

    // ...and once they have: both walk out to the spot, and when both are
    // there one challenges the other, and they fight it out like the crowd
    // there does. If the crowd is at its room, two of it head off to make
    // room. False when they cannot go after all.
    bool GoDuel(Player* a, Player* b);

    // ---- Group quests talked into (PlayerbotsPlus.GroupQuests.*)

    // A group quest of the zone `a` and `b` stand in that they could go and
    // do together now: two of playerbots' random bots of a side within 30
    // yards of each other, out of a group and wanted by nothing else, neither
    // walking for one, with others for one already or resting from one, both
    // holding it -- not done, not given up on, not grey, able to try it again
    // -- and up to it (an elite one at its level or above). The one nearest
    // their level; 0 for none. A chat module that has two bots talk each
    // other into doing one together asks this first.
    uint32_t GroupQuestFor(Player* a, Player* b);

    // ...and once they have: a party for `quest`, `a` leading, `b` in it.
    // They get together where they stand and go and do it, as any party for
    // one does -- with no ask in General, and no answer. False when they
    // cannot after all.
    bool GroupQuestTogether(Player* a, Player* b, uint32_t quest);

    // ---- City life (PlayerbotsPlus.CityLife.*): what the crowd does while it
    // hangs out -- shifting about, looking round, hopping, dancing, errands,
    // fishing. What a chat module does there it says here, so nobody walks off
    // in the middle of a sentence.

    // `bot` is talking or emoting for `seconds`: hanging out, it stays where
    // it is meanwhile. 0: it is done.
    void CityBusy(Player* bot, uint32_t seconds);

    // `speaker` said something in /say: those hanging out near them may look
    // round at them.
    void CitySpoke(Player* speaker);

    // These are about to talk in /say for up to `seconds`: those hanging out
    // walk over to the first, to talk face to face, and stay put till
    // CityBusy(bot, 0).
    void CityGather(std::vector<Player*> const& bots, uint32_t seconds);

    // How lively each bot is in a city, by the kind of person it is: a chat
    // module that gives its bots personalities says which temperament
    // (PlayerbotsPlus.CityLife.Temperament.<Name>, matched without regard to
    // case) a bot is. Asked on the world thread, once each time a bot gets to
    // a hangout spot. "default" is the CityLife.* settings themselves; an
    // empty answer is no answer, and the bot gets one by
    // CityLife.TemperamentShares, as without a provider.
    using TemperamentProvider = std::function<std::string(Player*)>;
    void SetTemperamentProvider(TemperamentProvider provider);

    // Someone told `bot` to stop showing off (BotEventType::CityShowOff): it
    // stops -- now and then with one last go, for cheek -- and does not start
    // again for a good while.
    void CityCalmDown(Player* bot);

    // ---- What bots did, for a chat module to have them say

    enum class BotEventType : uint8_t
    {
        StuckMount,      // gave up on something after getting on and off its mount on the spot
        SapNotLearned,   // the star went on a mob for it to sap, and it has no Sap yet
        SapWrongType,    // ...on a mob Sap does not work on
        SapNoStealth,    // ...and it could not go into stealth in time, or lost it on the way
        SapFailed,       // ...and it tried, and the sap did not work
        // Group quests (PlayerbotsPlus.GroupQuests.*). Said in the zone's
        // General channel unless it says otherwise; `quest` is the quest.
        GroupQuestAsk,     // it wants help with the quest: `need` more of them (`have` so far, itself too)
        GroupQuestAnswer,  // ...one who has it too says it will come (`other`: the one who asked)
        GroupQuestInvite,  // ...the one leading a party for it tells one who asked to come along
                           //    (`other`: who asked; `have`: how many it has now, the one asking too)
        GroupQuestGo,      // a party for it has got together and goes (`have` of them): its leader
        GroupQuestDone,    // ...and did it: its leader
        GroupQuestGiveUp,  // ...or gave up on it after its tries (alone, `have` is 1): its leader
        GroupQuestSolo,    // nobody came for it in the time it asked: it tries it alone
        // City life (PlayerbotsPlus.CityLife.*), in a city, near a real player.
        CityMountShown,    // someone got on a mount few people have, and a few hanging out nearby came over
                           //    to look: `bot` is one of them, `other` the rider, `what` the mount
                           //    ("Swift Spectral Tiger"), `detail` how rare ("nobody else about has one"),
                           //    `have` how many came over, it too
        CityDance,         // a bot hanging out started dancing (`other` empty), or joined `other` dancing
        CityShowOff,       // a bot hanging out (a temperament that shows off) has been making its mount rear up
                           //    and roar over and over -- on foot, hopping about -- near a real player for a while:
                           //    `what` its mount ("Swift Brown Wolf"; empty on foot), `verb` "rear" or "hop", `have`
                           //    how many times so far. It goes on for a minute or so; CityCalmDown stops it.
    };

    struct BotEvent
    {
        BotEventType type = BotEventType::StuckMount;
        ObjectGuid   bot;      // in the world when this is raised
        ObjectGuid   other;    // Sap*: who put the star on, when it was a real player in its group
        std::string  what;     // StuckMount: "a Copper Vein", "the body of a Boar"; Sap*: the mob's name
        std::string  verb;     // StuckMount: "mine", "pick", "skin", "open" or "loot"
        std::string  zone;     // where it happened
        std::string  detail;   // Sap*: why, said to the bot ("you have not trained Sap", "it is immune to Sap")
        uint32_t     quest = 0;   // GroupQuest*: the quest
        uint32_t     need  = 0;   // GroupQuestAsk: how many more it wants
        uint32_t     have  = 0;   // GroupQuestAsk, Go, GiveUp: how many there are, it too;
                                  // CityMountShown: how many came over to look, it too
    };

    using BotEventListener = std::function<void(BotEvent const&)>;

    // Called for every bot event from now on, on the world thread, while the
    // bot is still where it happened.
    void AddBotEventListener(BotEventListener listener);

    // ---- Muted stock lines (PlayerbotsPlus.Mute.*)

    // While one of these is alive on a thread, the lines that thread sends for
    // a bot are the caller's own -- a chat module speaking for it -- and are
    // never muted, even when one happens to read "Hi" or "Following".
    struct OwnLine
    {
        OwnLine();
        ~OwnLine();
        OwnLine(OwnLine const&) = delete;
        OwnLine& operator=(OwnLine const&) = delete;
    };
}

#endif // PLAYERBOTS_PLUS_H
