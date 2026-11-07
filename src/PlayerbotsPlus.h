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
// Everything here is called on the world thread and calls back on it.
// --------------------------------------------------------------------------

#include "ObjectGuid.h"

#include <cstdint>
#include <functional>

#define PLAYERBOTS_PLUS_API_VERSION 1

namespace PlayerbotsPlus
{
    // Random bots trading on the auction house (PlayerbotsPlus.Market.*).
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
}

#endif // PLAYERBOTS_PLUS_H
