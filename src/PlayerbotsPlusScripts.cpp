#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusMarket.h"
#include "PlayerbotsPlusMarketPrice.h"

#include "AuctionHouseMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Log.h"
#include "Player.h"
#include "ScriptMgr.h"

using namespace Acore::ChatCommands;

namespace
{
    class PlayerbotsPlusWorldScript : public WorldScript
    {
    public:
        PlayerbotsPlusWorldScript()
            : WorldScript("PlayerbotsPlusWorldScript", { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE }) { }

        void OnAfterConfigLoad(bool reload) override
        {
            PlayerbotsPlus_LoadConfig(reload);
            if (reload)
                PbpMarket_Reset();
        }

        void OnUpdate(uint32 diff) override
        {
            PbpMarket_Update(diff);
        }
    };

    // A bot's auction sold, or came back unsold: the mail about it is the one
    // moment the game says so, whoever did the buying, the bot online or not.
    class PlayerbotsPlusAuctionScript : public AuctionHouseScript
    {
    public:
        PlayerbotsPlusAuctionScript()
            : AuctionHouseScript("PlayerbotsPlusAuctionScript",
                                 { AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_SEND_AUCTION_SUCCESSFUL_MAIL,
                                   AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_SEND_AUCTION_EXPIRED_MAIL }) { }

        void OnBeforeAuctionHouseMgrSendAuctionSuccessfulMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* auction,
                                                              Player* /*owner*/, uint32& ownerAccount,
                                                              uint32& /*profit*/, bool& /*sendNotification*/,
                                                              bool& /*updateAchievementCriteria*/,
                                                              bool& /*sendMail*/) override
        {
            PbpMarket_OnSaleMailed(auction, ownerAccount);
        }

        void OnBeforeAuctionHouseMgrSendAuctionExpiredMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* auction,
                                                           Player* /*owner*/, uint32& ownerAccount,
                                                           bool& /*sendNotification*/, bool& /*sendMail*/) override
        {
            PbpMarket_OnExpiryMailed(auction, ownerAccount);
        }
    };

    class PlayerbotsPlusCommandScript : public CommandScript
    {
    public:
        PlayerbotsPlusCommandScript() : CommandScript("PlayerbotsPlusCommandScript") { }

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable pbpTable =
            {
                { "status", HandleStatus, SEC_GAMEMASTER,    Console::Yes },
                { "visit",  HandleVisit,  SEC_GAMEMASTER,    Console::No  },
                { "reload", HandleReload, SEC_ADMINISTRATOR, Console::Yes },
            };
            static ChatCommandTable commandTable =
            {
                { "pbp", pbpTable },
            };
            return commandTable;
        }

        // .pbp status: what the module is doing.
        static bool HandleStatus(ChatHandler* handler)
        {
            PbpSettings const& s = g_Pbp;
            PbpMarketStats const st = PbpMarket_GetStats();
            using PbpPrice::MoneyText;

            handler->PSendSysMessage("|cff00ff00[Playerbots Plus]|r {}, auction house trading {}",
                                     s.enable ? "on" : "off", s.market.enable ? "on" : "off");
            handler->PSendSysMessage("Bots in a trading city: {}. Up for auction: {} by bots, of {} in all.",
                                     st.inCities, st.botAuctions, st.allAuctions);
            handler->PSendSysMessage("Visits: {}. Listed {} ({}), sold {} ({}), back unsold {}.",
                                     st.visits, st.listed, MoneyText(st.listedMoney), st.sold, MoneyText(st.soldMoney),
                                     st.expired);
            handler->PSendSysMessage("Bought {} ({}), put on after {} visit{}. Mail collected: {} ({}, {} item{}).",
                                     st.bought, MoneyText(st.boughtMoney), st.equipped, st.equipped == 1 ? "" : "s",
                                     st.mails, MoneyText(st.mailMoney), st.mailItems, st.mailItems == 1 ? "" : "s");
            if (st.tooCheap || st.bagsFull)
                handler->PSendSysMessage("Not listed, cheaper than a vendor already: {}. Mail left, bags full: {}.",
                                         st.tooCheap, st.bagsFull);
            handler->SendSysMessage("Select a random bot and type .pbp visit to send it to the auction house now.");
            return true;
        }

        // .pbp visit [name]: that bot (or the one selected) trades now.
        static bool HandleVisit(ChatHandler* handler, Optional<PlayerIdentifier> target)
        {
            if (!target)
                target = PlayerIdentifier::FromTargetOrSelf(handler);
            Player* bot = target ? target->GetConnectedPlayer() : nullptr;
            if (!bot)
            {
                handler->SendErrorMessage("Select a bot, or name one that is online.");
                return false;
            }

            std::string report;
            PbpMarket_VisitNow(bot, report);
            handler->SendSysMessage(report);
            return true;
        }

        // .pbp reload: re-read mod_playerbots_plus.conf.
        static bool HandleReload(ChatHandler* handler)
        {
            if (!sConfigMgr->Reload())
            {
                handler->SendErrorMessage("Could not reload the config files.");
                return false;
            }
            PlayerbotsPlus_LoadConfig(true);
            PbpMarket_Reset();
            handler->SendSysMessage("Playerbots Plus: settings reloaded.");
            return true;
        }
    };
}

void Addmod_playerbots_plusScripts()
{
    LOG_INFO("server.loading", "[Playerbots Plus] Registering scripts.");
    new PlayerbotsPlusWorldScript();
    new PlayerbotsPlusAuctionScript();
    new PlayerbotsPlusCommandScript();
}
