# mod-playerbots-plus

Random bots doing what real players do, without an LLM. An AzerothCore module
for servers running [mod-playerbots](https://github.com/mod-playerbots/mod-playerbots).

It starts with the auction house. The features of
[mod-ollama-chat](https://github.com/kamakazedan/mod-ollama-chat) (branch `dangaming`)
that don't need a language model are moving here next.

## The auction house

Random bots in a capital city go to the auction house now and then, the way
players do:

- **They collect their auction mail:** money from sales, items that came back
  unsold, and things they bought.
- **They list what they have no use for:** cloth, leather, herbs and ore they
  have no profession for, and gear they will never wear. That's whatever
  playerbots itself would take to the auction house rather than a vendor.
- **They buy:** an upgrade they can afford (playerbots decides what counts as
  an upgrade), or materials their profession uses to skill up. They put the
  upgrade on straight away.

The auctions are real ones. Each one is in the bot's faction auction house,
under the bot's own name. Real players can buy them, and the bots buy real
players' auctions. The money is real too: the bot pays the deposit, the house
takes its cut, and the sale is paid by mail an hour later, the same as for
anyone.

How a bot prices one item:

1. If someone else already has one up, it asks a little under the cheapest
   one (1 to 5%). That includes the auction house bot's listings.
2. If nobody does, it asks what the item is worth: a multiple of the vendor
   price, higher for better quality.
3. It never asks more than someone of the item's level could sensibly pay.
   Wealth by level is about 2g at 10, 17g at 30, 300g at 60 and 2000g at 80.
4. It never asks so little that a vendor would pay more. If the market is
   already below that, the bot keeps the item for a vendor.
5. If an item comes back unsold, the bot asks 15% less next time. After a
   couple of tries it gives up on that item.

Bots don't walk to the auctioneer: being anywhere in the city is enough.
Shattrath and Dalaran count as cities too. Neither has an auction house, so a
bot there uses its faction's, as a player would after flying over.

### With other modules

- **[mod-ah-bot-plus](https://github.com/NathanHandley/mod-ah-bot-plus):**
  recommended alongside. It fills the auction house, and its prices are what
  the bots undercut. Its buyer buys the bots' auctions as it buys players'.
- **mod-ollama-chat:** when it is in the same build, the bots talk about their
  trading in General: what sold, what they bought, what came back unsold.
  This works through the `PlayerbotsPlus.h` interface.

## Install

```bash
cd modules
git clone https://github.com/kamakazedan/mod-playerbots-plus.git
```

Copy `conf/mod_playerbots_plus.conf.dist` to your module config folder as
`mod_playerbots_plus.conf`, then rebuild. The module needs no SQL.

## Commands (GM)

| Command | What it does |
|---|---|
| `.pbp status` | What it is doing: bots in cities, auctions up, sales, purchases |
| `.pbp visit [name]` | That bot, or the one selected, trades now, wherever it is, and says what it did |
| `.pbp reload` | Re-reads the config. `.reload config` does too |

Every setting is documented in `conf/mod_playerbots_plus.conf.dist`.

## Development

See `CLAUDE.md` for the rules. The pricing tests run without a server:

```bash
bash tests/build.sh
```

## Licence

GPL v2, as AzerothCore.
