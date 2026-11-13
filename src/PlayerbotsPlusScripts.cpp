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
#include "PlayerbotsPlusRevive.h"
#include "PlayerbotsPlusSpace.h"
#include "PlayerbotsPlusStuck.h"

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

        // .pbp status: what the module is doing.
        static bool HandleStatus(ChatHandler* handler)
        {
            PbpSettings const& s = g_Pbp;
            PbpMarketStats const st = PbpMarket_GetStats();
            using PbpPrice::MoneyText;
            auto onOff = [](bool on) { return on ? "on" : "off"; };

            handler->PSendSysMessage("|cff00ff00[Playerbots Plus]|r {}, auction house trading {}",
                                     s.enable ? "on" : "|cffff0000OFF|r", onOff(s.market.enable));
            handler->PSendSysMessage("Bots in a trading city: {}. Up for auction: {} by bots, of {} in all.",
                                     st.inCities, st.botAuctions, st.allAuctions);
            handler->PSendSysMessage("Visits: {}. Listed {} ({}), sold {} ({}), back unsold {}.",
                                     st.visits, st.listed, MoneyText(st.listedMoney), st.sold, MoneyText(st.soldMoney),
                                     st.expired);
            handler->PSendSysMessage("Bought {} ({}), put on after {} visit{}. Mail collected: {} ({}, {} item{}).",
                                     st.bought, MoneyText(st.boughtMoney), st.equipped, st.equipped == 1 ? "" : "s",
                                     st.mails, MoneyText(st.mailMoney), st.mailItems, st.mailItems == 1 ? "" : "s");
            if (st.tooCheap || st.bagsFull || st.takenDown)
                handler->PSendSysMessage("Not listed, cheaper than a vendor already: {}. Taken down, not their own "
                                         "loot: {}. Mail left, bags full: {}.", st.tooCheap, st.takenDown,
                                         st.bagsFull);

            PbpHangoutStats const h = PbpHangout_GetStats();
            handler->PSendSysMessage("City hangouts: {} - {} hanging out ({} AFK), {} on their way, {} spots ({} with "
                                     "an inn to arrive at). Sent {}, fetched from elsewhere {} ({} gone back), "
                                     "settled {}, never got there {} ({} no room at the spot, {} too slow, {} given "
                                     "something else to do, {} went elsewhere, {} never landed), sent on to another "
                                     "spot in the city {}, spots found full {} times, moved on {}, cut short {}, back "
                                     "from AFK {}.",
                                     onOff(pbp::g_HangoutEnable), h.hanging, h.away, h.onTheWay, h.spots, h.withInn,
                                     h.sent, h.broughtIn, h.sentBack, h.settled, h.gaveUp, h.lostNoRoom, h.lostSlow,
                                     h.lostBusy, h.lostElsewhere, h.lostLanding, h.redirected, h.foundFull, h.movedOn,
                                     h.cutShort, h.cameBack);
            std::string cities;
            for (PbpHangoutCity const& c : PbpHangout_Cities())
            {
                if (!c.there && !c.onTheWay && !c.share)
                    continue;
                std::string more;
                auto add = [&more](std::string const& what) { more += (more.empty() ? "" : ", ") + what; };
                if (c.hot)
                    add(fmt::format("{} at the hot spots", c.hot));
                if (c.onTheWay)
                    add(fmt::format("{} on the way", c.onTheWay));
                if (c.full)
                    add(fmt::format("{} spot{} full", c.full, c.full == 1 ? "" : "s"));
                if (c.room < c.share)
                    add(fmt::format("{} of its {} places at other cities' spots", c.share - c.room, c.share));
                else if (c.room > c.share)
                    add(fmt::format("{} more from cities that are full", c.room - c.share));
                cities += fmt::format("{}{} {} of {}{}", cities.empty() ? "" : ", ", c.name, c.there, c.room,
                                      more.empty() ? std::string() : " (" + more + ")");
            }
            handler->PSendSysMessage("Hanging out by city: {}.", cities.empty() ? std::string("nobody yet") : cities);

            PbpStuckStats const k = PbpStuck_GetStats();
            handler->PSendSysMessage("Stuck on a mount: {} - {} mounts noted, {} bots stuck ({} kept off what they "
                                     "were after, {} with nothing to loot, {} kept on foot); {} held off now, {} on "
                                     "foot now.",
                                     onOff(pbp::g_StuckEnable), k.mounts, k.stuck, k.hinted, k.nothing, k.walked,
                                     k.holding, k.walking);

            PbpFireStats const f = PbpFire_GetStats();
            handler->PSendSysMessage("Out of the fire: {} - {} bots burned standing in a fire, {} found standing in "
                                     "one that burns nobody (a bonfire), {} walked out ({} of them hanging out, given "
                                     "a new place; {} in one again within a minute and sent off to do something "
                                     "else), {} with nowhere to step to.",
                                     onOff(pbp::g_FireEnable), f.burns, f.seen, f.stepped, f.moved, f.sentOn,
                                     f.nowhere);

            PbpIndoorsStats const in = PbpIndoors_GetStats();
            handler->PSendSysMessage("On foot indoors: {} - {} bots got off their mount under a roof, {} riding "
                                     "indoors right now (off once standing still, after {}s).",
                                     onOff(pbp::g_IndoorsEnable), in.dismounted, in.watching, pbp::g_IndoorsSeconds);

            PbpSpaceStats const sp = PbpSpace_GetStats();
            handler->PSendSysMessage("Room to stand: {} - {} bots found standing in someone, {} stepped aside ({} of "
                                     "them hanging out, given a new place), {} with nowhere to step to just then.",
                                     onOff(pbp::g_SpaceEnable), sp.crowded, sp.stepped, sp.moved, sp.nowhere);

            PbpReviveStats const r = PbpRevive_GetStats();
            handler->PSendSysMessage("Getting back up: {} - {} released, {} ran back in, {} stopped waiting for a "
                                     "rez. Muted bot lines: {}.",
                                     onOff(pbp::g_ReviveEnable), r.released, r.returned, r.gaveUp,
                                     PbpMute_GetDropped());

            PbpMarksStats const m = PbpMarks_GetStats();
            std::string const lastFail = m.lastFail ? fmt::format(", last cast result {}", m.lastFail) : "";
            handler->PSendSysMessage("Raid marks: {} - {} crowd-control marks given out, the skull moved {} to the "
                                     "cross and {} to the next mob; {} saps ({} would not cast{}, {} did not take, {} "
                                     "given up, {} could not sap it at all).",
                                     onOff(pbp::g_MarksEnable), m.assigned, m.moved, m.picked, m.saps, m.sapFails,
                                     lastFail, m.sapMissed, m.sapGaveUp, m.sapCant);

            PbpPetsStats const p = PbpPets_GetStats();
            handler->PSendSysMessage("Hunters' pets: {} - {} given a name, {} left as they were tamed. Companions: "
                                     "{} - {} on the lists ({} left out), {} learned ({} near you waiting for their "
                                     "first until nobody in their guild is on), {} brought out, {} swapped for "
                                     "another, {} put away.",
                                     onOff(pbp::g_PetNamesEnable), p.named, p.keptName,
                                     onOff(pbp::g_CompanionsEnable), p.companions, p.badEntries, p.learned,
                                     p.guildWait, p.summoned, p.switched, p.putAway);

            PbpDuelsStats const d = PbpDuels_GetStats();
            std::string spots;
            for (std::string const& key : pbp::g_DuelsSpots)
                spots += (spots.empty() ? "" : ", ") + key;
            handler->PSendSysMessage("Duels at the gates ({}): {} - {} challenges ({} to a real player), {} fought "
                                     "without leaving the crowd, {} duelling now, {} walking over now, {} given up; "
                                     "{} pairs talked into one went out to the gates ({} fought it out, {} never "
                                     "did, {} on their way now).",
                                     spots.empty() ? std::string("nowhere") : spots, onOff(pbp::g_DuelsEnable),
                                     d.challenged, d.asked, h.duels, h.duelling, d.walking, d.gaveUp, d.pacts,
                                     d.pactsHeld, d.pactsLost, d.pactsNow);

            PbpLedgerStats const lg = PbpLedger_GetStats();
            handler->PSendSysMessage("Economy ledger: {} - {} things noted since startup ({} dropped, too many at "
                                     "once), written {} times; last look at the bots: {}. .pbp economy [days] shows "
                                     "it.",
                                     onOff(s.ledger.enable), lg.notes, lg.dropped, lg.writes,
                                     lg.lookedAt.empty() ? std::string("not yet")
                                                         : fmt::format("{} bots at {}", lg.looked, lg.lookedAt));

            PbpDungeonsStats const bd = PbpDungeons_GetStats();
            handler->PSendSysMessage("Bot dungeons: {} - {} parties in the queue, {} runs going on; {} runs since "
                                     "startup ({} cleared, {} failed). .pbp dungeons shows them.",
                                     onOff(s.enable && s.dungeons.enable), bd.queuedNow, bd.runsNow, bd.runs,
                                     bd.cleared, bd.failed);

            PbpGroupQuestsStats const gq = PbpGroupQuests_GetStats();
            handler->PSendSysMessage("Group quests: {} - {} parties now; {} went since startup ({} alone), {} "
                                     "quests done. .pbp groupquests shows them.",
                                     onOff(s.enable && s.groupQuests.enable), gq.partiesNow, gq.parties, gq.solos,
                                     gq.done);

            handler->SendSysMessage("Select a random bot and type .pbp visit to send it to the auction house now.");
            return true;
        }

        // .pbp groupquests: random bots getting parties together for group quests.
        static bool HandleGroupQuests(ChatHandler* handler)
        {
            for (std::string const& line : PbpGroupQuests_Status())
                handler->PSendSysMessage("{}", line);
            return true;
        }

        // .pbp dungeons: random bots running dungeons on their own.
        static bool HandleDungeons(ChatHandler* handler)
        {
            for (std::string const& line : PbpDungeons_Status())
                handler->PSendSysMessage("{}", line);
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
            for (std::string const& line : PbpLedger_Report(days ? *days : 7))
                handler->PSendSysMessage("{}", line);
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
        // The fires about you, how near anyone may stand, and who is in one.
        // .pbp hangouts: each spot -- who is there, its room, and how the
        // places looked at for it went (what was turned down, and why).
        static bool HandleHangouts(ChatHandler* handler)
        {
            for (std::string const& line : PbpHangout_SpotReport())
                handler->PSendSysMessage("{}", line);
            return true;
        }

        static bool HandleFires(ChatHandler* handler)
        {
            Player* me = handler->GetSession() ? handler->GetSession()->GetPlayer() : nullptr;
            if (!me)
                return false;
            for (std::string const& line : PbpFire_Describe(me))
                handler->PSendSysMessage("{}", line);
            return true;
        }

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
    new PbpGroupQuestsTeleportScript();
#if PBP_BOT_DUNGEONS
    new PbpDungeonsPacketScript();
    new PbpDungeonsTeleportScript();
#endif
}
