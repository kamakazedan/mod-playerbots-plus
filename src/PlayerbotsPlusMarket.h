#ifndef PLAYERBOTS_PLUS_MARKET_H
#define PLAYERBOTS_PLUS_MARKET_H

// --------------------------------------------------------------------------
// The market: random bots trading on the auction house like players.
//
// Every Market.IntervalSeconds, one of the random bots standing in a capital
// (or in Shattrath or Dalaran, Market.ExtraZoneOrAreaIds) that has not been
// for a while (Market.VisitCooldownMinutes) goes to the auction house:
//
//   - it collects its auction mail: what it sold, what came back unsold,
//     what it bought;
//   - it lists what it has no use for -- what playerbots itself would carry
//     to the auction house rather than a vendor: cloth, leather, herbs and
//     ore it has no profession for, gear it will never wear -- a little under
//     the cheapest one already listed, or for what it is worth if there is
//     none, never more than someone of that level could pay and never so
//     little that a vendor would pay more;
//   - it buys an upgrade it can afford, or what its profession uses, and
//     puts the upgrade on.
//
// The auctions are real ones, in the house of its faction, under its own
// name: a real player can buy them, and a real player's are bought. The money
// is real too: the deposit, the house's cut, and sales paid by mail an hour
// later, as for anyone.
//
// World thread only.
// --------------------------------------------------------------------------

#include "ObjectGuid.h"

#include <cstdint>
#include <string>

struct AuctionEntry;
class Player;

void PbpMarket_Update(uint32_t diff);

// From the auction house hooks: one of a bot's auctions sold, or came back.
void PbpMarket_OnSaleMailed(AuctionEntry const* auction, uint32_t ownerAccount);
void PbpMarket_OnExpiryMailed(AuctionEntry const* auction, uint32_t ownerAccount);

// .pbp visit: `bot` goes to the auction house now, wherever it is.
// What it did, or why it could not, in `report`.
bool PbpMarket_VisitNow(Player* bot, std::string& report);

void PbpMarket_Reset();

struct PbpMarketStats
{
    uint64_t visits        = 0;
    uint64_t listed        = 0;
    uint64_t listedMoney   = 0;   // buyouts asked
    uint64_t sold          = 0;
    uint64_t soldMoney     = 0;
    uint64_t expired       = 0;
    uint64_t bought        = 0;
    uint64_t boughtMoney   = 0;
    uint64_t equipped      = 0;   // visits where something bought was put on
    uint64_t mails         = 0;   // auction mails collected
    uint64_t mailMoney     = 0;
    uint64_t mailItems     = 0;
    uint64_t tooCheap      = 0;   // not listed: cheaper than a vendor already
    uint64_t bagsFull      = 0;   // mail left: no room for it
    uint32_t botAuctions   = 0;   // up right now, all houses (as of the last look)
    uint32_t allAuctions   = 0;
    uint32_t inCities      = 0;   // random bots in a trading city at the last tick
};
PbpMarketStats PbpMarket_GetStats();

#endif // PLAYERBOTS_PLUS_MARKET_H
