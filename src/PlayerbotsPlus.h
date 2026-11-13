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

class Player;

// 1: the market. 2: hangouts, bot events and OwnLine. 3: duels talked into.
// 4: group quests (BotEvent's GroupQuest* types, quest and need).
#define PLAYERBOTS_PLUS_API_VERSION 4

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
        uint32_t     have  = 0;   // GroupQuestAsk, Go, GiveUp: how many there are, it too
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
