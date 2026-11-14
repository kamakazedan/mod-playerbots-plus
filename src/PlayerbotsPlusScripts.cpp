#include "PlayerbotsPlusConfig.h"
#include "PlayerbotsPlusDuels.h"
#include "PlayerbotsPlusDungeons.h"
#include "PlayerbotsPlusFire.h"
#include "PlayerbotsPlusGroupQuests.h"
#include "PlayerbotsPlusHangout.h"
#include "PlayerbotsPlusIndoors.h"
#include "PlayerbotsPlusLedger.h"
#include "PlayerbotsPlusMarket.h"
#include "PlayerbotsPlusMarketPrice.h"
#include "PlayerbotsPlusMarks.h"
#include "PlayerbotsPlusMute.h"
#include "PlayerbotsPlusPets.h"
#include "PlayerbotsPlusReport.h"
#include "PlayerbotsPlusRevive.h"
#include "PlayerbotsPlusSpace.h"
#include "PlayerbotsPlusStuck.h"

#include "AuctionHouseMgr.h"
#include "Chat.h"
#include "ChatCommand.h"
#include "Config.h"
#include "Log.h"
#include "Opcodes.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "WorldPacket.h"
#include "WorldSession.h"

using namespace Acore::ChatCommands;

namespace
{
    class PlayerbotsPlusWorldScript : public WorldScript
    {
    public:
        PlayerbotsPlusWorldScript()
            : WorldScript("PlayerbotsPlusWorldScript",
                          { WORLDHOOK_ON_AFTER_CONFIG_LOAD, WORLDHOOK_ON_UPDATE, WORLDHOOK_ON_SHUTDOWN }) { }

        void OnAfterConfigLoad(bool reload) override
        {
            PlayerbotsPlus_LoadConfig(reload);
            PbpLedger_Load(reload);
            if (reload)
            {
                PbpMarket_Reset();
                PbpPets_Reset();
            }
        }

        void OnUpdate(uint32 diff) override
        {
            // First: what the game paid the bots is all counted before anyone
            // looks at what they have (the ledger), and anything this tick
            // pays them is counted next tick, before the next look.
            PbpLedger_Update(diff);
            // These two also run with the module switched off: they let go
            // whoever they are holding (sat down, AFK, kept on foot). The
            // fires' notes are taken either way, so none pile up.
            PbpHangout_Update(diff);
            // Bot dungeons as well: switched off, the queues they made are
            // left, and the runs going on finish and let their party go.
            PbpDungeons_Update(diff);
            // Group quests: switched off, their parties let their members go.
            PbpGroupQuests_Update(diff);
            PbpStuck_Update(diff);
            PbpIndoors_Update(diff);
            PbpFire_Update(diff);
            PbpSpace_Update(diff);
            // The full report, on the clock: switched off, it says so.
            PbpReport_Update(diff);
            if (!g_Pbp.enable)
                return;
            PbpMarket_Update(diff);
            PbpMute_Update(diff);
            PbpRevive_Update(diff);
            PbpMarks_Update(diff);
            PbpPets_Update(diff);
            PbpDuels_Update(diff);
        }

        void OnShutdown() override
        {
            PbpLedger_Shutdown();
        }
    };

    // A real player reared their mount (jump, standing still on it): those
    // hanging out near them may do it back (city life). Every packet anyone
    // sends comes through here, on whatever thread handles it: the opcode
    // first, and then only a note of who, under a mutex.
    class PbpRearScript : public ServerScript
    {
    public:
        PbpRearScript() : ServerScript("PbpRearScript", { SERVERHOOK_CAN_PACKET_RECEIVE }) { }

        bool CanPacketReceive(WorldSession* session, WorldPacket const& packet) override
        {
            if (packet.GetOpcode() == CMSG_MOUNTSPECIAL_ANIM && session)
                PbpHangout_NoteRear(session->GetPlayer());
            return true;
        }
    };

    // A bot's auction sold, or came back unsold: the mail about it is the one
    // moment the game says so, whoever did the buying (the AH bot's buyouts
    // too), the bot online or not. And anyone's auction put up, for the ledger.
    class PlayerbotsPlusAuctionScript : public AuctionHouseScript
    {
    public:
        PlayerbotsPlusAuctionScript()
            : AuctionHouseScript("PlayerbotsPlusAuctionScript",
                                 { AUCTIONHOUSEHOOK_ON_AUCTION_ADD,
                                   AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_SEND_AUCTION_SUCCESSFUL_MAIL,
                                   AUCTIONHOUSEHOOK_ON_BEFORE_AUCTIONHOUSEMGR_SEND_AUCTION_EXPIRED_MAIL }) { }

        void OnAuctionAdd(AuctionHouseObject* /*ah*/, AuctionEntry* entry) override
        {
            PbpLedger_OnAuctionAdded(entry);
        }

        void OnBeforeAuctionHouseMgrSendAuctionSuccessfulMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* auction,
                                                              Player* /*owner*/, uint32& ownerAccount,
                                                              uint32& /*profit*/, bool& /*sendNotification*/,
                                                              bool& /*updateAchievementCriteria*/,
                                                              bool& /*sendMail*/) override
        {
            PbpLedger_OnSaleMailed(auction);
            PbpMarket_OnSaleMailed(auction, ownerAccount);
        }

        void OnBeforeAuctionHouseMgrSendAuctionExpiredMail(AuctionHouseMgr* /*mgr*/, AuctionEntry* auction,
                                                           Player* /*owner*/, uint32& ownerAccount,
                                                           bool& /*sendNotification*/, bool& /*sendMail*/) override
        {
            PbpLedger_OnExpiryMailed(auction);
            PbpMarket_OnExpiryMailed(auction, ownerAccount);
        }
    };

    class PlayerbotsPlusCommandScript : public CommandScript
    {
    public:
        PlayerbotsPlusCommandScript() : CommandScript("PlayerbotsPlusCommandScript") { }

        ChatCommandTable GetCommands() const override
        {
            static ChatCommandTable dungeonsTable =
            {
                { "",      HandleDungeons,      SEC_GAMEMASTER, Console::Yes },
                { "bands", HandleDungeonsBands, SEC_GAMEMASTER, Console::Yes },
                { "send",  HandleDungeonsSend,  SEC_GAMEMASTER, Console::Yes },
            };
            static ChatCommandTable pbpTable =
            {
                { "status", HandleStatus, SEC_GAMEMASTER,    Console::Yes },
                { "report", HandleReport, SEC_GAMEMASTER,    Console::Yes },
                { "dungeons", dungeonsTable },
                { "groupquests", HandleGroupQuests, SEC_GAMEMASTER, Console::Yes },
                { "economy", HandleEconomy, SEC_GAMEMASTER,  Console::Yes },
                { "visit",  HandleVisit,  SEC_GAMEMASTER,    Console::No  },
                { "fires",  HandleFires,  SEC_GAMEMASTER,    Console::No  },
                { "hangouts", HandleHangouts, SEC_GAMEMASTER, Console::Yes },
                { "reload", HandleReload, SEC_ADMINISTRATOR, Console::Yes },
            };
            static ChatCommandTable commandTable =
            {
                { "pbp", pbpTable },
            };
            return commandTable;
        }

        // Where the rest of it is: the report file, written by .pbp report
        // (and on the clock).
        static std::string More(char const* what)
        {
            std::string const every = g_Pbp.report.minutes ? fmt::format(", also every {} min", g_Pbp.report.minutes)
                                                           : std::string();
            return fmt::format("{}: .pbp report ({}{}).", what, g_Pbp.report.file, every);
        }

        // A part in brief, as the chat window takes it: its lines, what looks
        // wrong, and where the rest is.
        static void Brief(ChatHandler* handler, PbpBrief const& b, char const* more)
        {
            for (size_t i = 0; i < b.lines.size(); ++i)
                handler->SendSysMessage(i ? b.lines[i] : "|cff00ff00[Playerbots Plus]|r " + b.lines[i]);
            for (std::string const& issue : b.issues)
                handler->SendSysMessage("|cffff8000 ! " + issue + "|r");
            handler->SendSysMessage(More(more));
        }

        // .pbp status: a line for each part, and what looks wrong.
        static bool HandleStatus(ChatHandler* handler)
        {
            for (std::string const& line : PbpReport_Overview())
                handler->SendSysMessage(line);
            handler->SendSysMessage("More: .pbp hangouts | dungeons | groupquests | economy. " +
                                    More("Every counter, spot, run and party"));
            return true;
        }

        // .pbp report: the whole of it written to the file now.
        static bool HandleReport(ChatHandler* handler)
        {
            std::string where;
            uint32_t lines = 0;
            if (!PbpReport_Write(where, lines))
            {
                handler->SendErrorMessage("Playerbots Plus: the report was not written -- " + where + ".", false);
                return false;
            }
            handler->SendSysMessage(fmt::format("Playerbots Plus: the full report, {} lines, is in {}", lines, where));
            return true;
        }

        // .pbp groupquests: random bots getting parties together for group quests.
        static bool HandleGroupQuests(ChatHandler* handler)
        {
            Brief(handler, PbpGroupQuests_Brief(true), "Every party");
            return true;
        }

        // .pbp dungeons: random bots running dungeons on their own.
        static bool HandleDungeons(ChatHandler* handler)
        {
            Brief(handler, PbpDungeons_Brief(true), "Every queue and run");
            return true;
        }

        // .pbp dungeons bands: the dungeons in each level band, and its runs.
        static bool HandleDungeonsBands(ChatHandler* handler)
        {
            for (std::string const& line : PbpDungeons_Bands())
                handler->PSendSysMessage("{}", line);
            return true;
        }

        // .pbp dungeons send <dungeon>: a party queued for it now (its dungeon
        // finder id, or part of its name), to see one through.
        static bool HandleDungeonsSend(ChatHandler* handler, Tail which)
        {
            if (which.empty())
            {
                handler->SendErrorMessage("Which dungeon? Its dungeon finder id, or part of its name: "
                                          ".pbp dungeons send deadmines");
                return false;
            }
            handler->SendSysMessage(PbpDungeons_Send(std::string(which)));
            return true;
        }

        // .pbp economy [days]: the random bots' economy over the last days (7).
        static bool HandleEconomy(ChatHandler* handler, Optional<uint32> days)
        {
            Brief(handler, PbpLedger_Brief(days ? *days : 7), "Every figure, the last 7 days");
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

        // .pbp hangouts: how many are where, city life, and what looks wrong.
        static bool HandleHangouts(ChatHandler* handler)
        {
            Brief(handler, PbpHangout_Brief(true), "Each spot, and how the places looked at for it went");
            return true;
        }

        // .pbp fires: the fires about you, how near anyone may stand, and
        // who is in one.
        static bool HandleFires(ChatHandler* handler)
        {
            Player* me = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
            if (!me)
                return false;
            for (std::string const& line : PbpFire_Describe(me))
                handler->PSendSysMessage("{}", line);
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
            PbpPets_Reset();
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
    new PbpLedgerPlayerScript();
    new PbpStuckScript();
    new PbpFireScript();
    new PbpMuteScript();
    new PbpRearScript();
    new PbpGroupQuestsTeleportScript();
#if PBP_BOT_DUNGEONS
    new PbpDungeonsPacketScript();
    new PbpDungeonsTeleportScript();
#endif
}
