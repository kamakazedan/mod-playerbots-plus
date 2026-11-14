#include "PlayerbotsPlusReport.h"
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

#include "GameTime.h"
#include "Log.h"
#include "Timer.h"

#include <fmt/core.h>

#include <chrono>
#include <ctime>
#include <fstream>

using namespace pbp;

namespace
{
    using Clock = std::chrono::steady_clock;

    uint32_t                 g_reportMs   = 0;
    bool                     g_lastFailed = false;
    std::vector<std::string> g_economy;      // the economy section, as of g_economyAt: it reads the database
    Clock::time_point        g_economyAt{};

    char const* OnOff(bool on)
    {
        return on ? "on" : "off";
    }

    std::string Uptime()
    {
        const uint64_t s = uint64_t(GameTime::GetUptime().count());
        if (s < 3600)
            return fmt::format("{}m", s / 60);
        if (s < 86400)
            return fmt::format("{}h {}m", s / 3600, s % 3600 / 60);
        return fmt::format("{}d {}h", s / 86400, s % 86400 / 3600);
    }

    // The auction house, in a line.
    std::string MarketLine()
    {
        PbpMarketStats const st = PbpMarket_GetStats();
        using PbpPrice::MoneyText;
        return fmt::format("Market: {} -- {} of the {} auctions up are bots'; {} visits: listed {} ({}), sold {} ({}), "
                           "bought {} ({}), {} back unsold.",
                           OnOff(g_Pbp.enable && g_Pbp.market.enable), st.botAuctions, st.allAuctions, st.visits,
                           st.listed, MoneyText(st.listedMoney), st.sold, MoneyText(st.soldMoney), st.bought,
                           MoneyText(st.boughtMoney), st.expired);
    }

    // The fixes that keep bots looking right, in a line.
    std::string FixesLine()
    {
        PbpStuckStats const k = PbpStuck_GetStats();
        PbpFireStats const f = PbpFire_GetStats();
        PbpSpaceStats const sp = PbpSpace_GetStats();
        PbpIndoorsStats const in = PbpIndoors_GetStats();
        PbpReviveStats const r = PbpRevive_GetStats();
        PbpMarksStats const m = PbpMarks_GetStats();
        PbpPetsStats const p = PbpPets_GetStats();
        PbpDuelsStats const d = PbpDuels_GetStats();
        PbpHangoutStats const h = PbpHangout_GetStats();
        return fmt::format("Fixes: {} off stuck mounts, {} out of fires, {} given room to stand, {} off their mount "
                           "indoors, {} released and {} ran back; marks: {} crowd control, {} saps; {} pets named; {} "
                           "duels; {} stock lines muted.",
                           k.stuck, f.stepped, sp.stepped, in.dismounted, r.released, r.returned, m.assigned, m.saps,
                           p.named, h.duels + d.pactsHeld, PbpMute_GetDropped());
    }

    // Everything counted since start, every part (what .pbp status said in
    // full before it was made short).
    std::vector<std::string> Detail()
    {
        std::vector<std::string> out;
        PbpSettings const& s = g_Pbp;
        PbpMarketStats const st = PbpMarket_GetStats();
        using PbpPrice::MoneyText;

        out.push_back(fmt::format("Playerbots Plus {}, auction house trading {}", OnOff(s.enable),
                                  OnOff(s.market.enable)));
        out.push_back(fmt::format("Bots in a trading city: {}. Up for auction: {} by bots, of {} in all.", st.inCities,
                                  st.botAuctions, st.allAuctions));
        out.push_back(fmt::format("Visits: {}. Listed {} ({}), sold {} ({}), back unsold {}.", st.visits, st.listed,
                                  MoneyText(st.listedMoney), st.sold, MoneyText(st.soldMoney), st.expired));
        out.push_back(fmt::format("Bought {} ({}), put on after {} visit{}. Mail collected: {} ({}, {} item{}).",
                                  st.bought, MoneyText(st.boughtMoney), st.equipped, st.equipped == 1 ? "" : "s",
                                  st.mails, MoneyText(st.mailMoney), st.mailItems, st.mailItems == 1 ? "" : "s"));
        out.push_back(fmt::format("Not listed, cheaper than a vendor already: {}. Taken down, not their own loot: {}. "
                                  "Mail left, bags full: {}.",
                                  st.tooCheap, st.takenDown, st.bagsFull));

        PbpHangoutStats const h = PbpHangout_GetStats();
        out.push_back(fmt::format("City hangouts: {} - {} hanging out ({} AFK), {} on their way, {} spots ({} with an "
                                  "inn to arrive at). Sent {}, fetched from elsewhere {} ({} gone back), settled {}, "
                                  "never got there {} ({} no room at the spot, {} too slow, {} given something else "
                                  "to do, {} went elsewhere, {} never landed), sent on to another spot in the city {}, "
                                  "spots found full {} times, moved on {}, cut short {}, back from AFK {}.",
                                  OnOff(g_HangoutEnable), h.hanging, h.away, h.onTheWay, h.spots, h.withInn, h.sent,
                                  h.broughtIn, h.sentBack, h.settled, h.gaveUp, h.lostNoRoom, h.lostSlow, h.lostBusy,
                                  h.lostElsewhere, h.lostLanding, h.redirected, h.foundFull, h.movedOn, h.cutShort,
                                  h.cameBack));
        out.push_back(fmt::format("City life: {} - steps {}, looks round {}, hops {}, mounts reared {} ({} done back); "
                                  "rare mounts looked at {} (by {}); dance parties {} ({} joined in); walked over to "
                                  "talk {}; errands {} ({} got there; {} bankers, auctioneers and mailboxes known); "
                                  "flew in {} ({} landed), flew out {}; fishing {} ({} casts, {} places to fish from).",
                                  OnOff(g_LifeEnable), h.steps, h.turns, h.hops, h.rears, h.rearedBack, h.gawks,
                                  h.gawkers, h.dances, h.danced, h.gathered, h.errands, h.errandsDone, h.services,
                                  h.flewIn, h.landed, h.flewOut, h.fished, h.casts, h.fishPlaces));

        PbpStuckStats const k = PbpStuck_GetStats();
        out.push_back(fmt::format("Stuck on a mount: {} - {} mounts noted, {} bots stuck ({} kept off what they were "
                                  "after, {} with nothing to loot, {} kept on foot); {} held off now, {} on foot now.",
                                  OnOff(g_StuckEnable), k.mounts, k.stuck, k.hinted, k.nothing, k.walked, k.holding,
                                  k.walking));
        PbpFireStats const f = PbpFire_GetStats();
        out.push_back(fmt::format("Out of the fire: {} - {} bots burned standing in a fire, {} found standing in one "
                                  "that burns nobody (a bonfire), {} walked out ({} of them hanging out, given a new "
                                  "place; {} in one again within a minute and sent off to do something else), {} with "
                                  "nowhere to step to.",
                                  OnOff(g_FireEnable), f.burns, f.seen, f.stepped, f.moved, f.sentOn, f.nowhere));
        PbpIndoorsStats const in = PbpIndoors_GetStats();
        out.push_back(fmt::format("On foot indoors: {} - {} bots got off their mount under a roof, {} riding indoors "
                                  "right now (off once standing still, after {}s).",
                                  OnOff(g_IndoorsEnable), in.dismounted, in.watching, g_IndoorsSeconds));
        PbpSpaceStats const sp = PbpSpace_GetStats();
        out.push_back(fmt::format("Room to stand: {} - {} bots found standing in someone, {} stepped aside ({} of them "
                                  "hanging out, given a new place), {} with nowhere to step to just then.",
                                  OnOff(g_SpaceEnable), sp.crowded, sp.stepped, sp.moved, sp.nowhere));
        PbpReviveStats const r = PbpRevive_GetStats();
        out.push_back(fmt::format("Getting back up: {} - {} released, {} ran back in, {} stopped waiting for a rez. "
                                  "Muted bot lines: {}.",
                                  OnOff(g_ReviveEnable), r.released, r.returned, r.gaveUp, PbpMute_GetDropped()));
        PbpMarksStats const m = PbpMarks_GetStats();
        std::string const lastFail = m.lastFail ? fmt::format(", last cast result {}", m.lastFail) : "";
        out.push_back(fmt::format("Raid marks: {} - {} crowd-control marks given out, the skull moved {} to the cross "
                                  "and {} to the next mob; {} saps ({} would not cast{}, {} did not take, {} given up, "
                                  "{} could not sap it at all).",
                                  OnOff(g_MarksEnable), m.assigned, m.moved, m.picked, m.saps, m.sapFails, lastFail,
                                  m.sapMissed, m.sapGaveUp, m.sapCant));
        PbpPetsStats const p = PbpPets_GetStats();
        out.push_back(fmt::format("Hunters' pets: {} - {} given a name, {} left as they were tamed. Companions: {} - "
                                  "{} on the lists ({} left out), {} learned ({} near you waiting for their first "
                                  "until nobody in their guild is on), {} brought out, {} swapped for another, {} put "
                                  "away.",
                                  OnOff(g_PetNamesEnable), p.named, p.keptName, OnOff(g_CompanionsEnable),
                                  p.companions, p.badEntries, p.learned, p.guildWait, p.summoned, p.switched,
                                  p.putAway));
        PbpDuelsStats const d = PbpDuels_GetStats();
        std::string spots;
        for (std::string const& key : g_DuelsSpots)
            spots += (spots.empty() ? "" : ", ") + key;
        out.push_back(fmt::format("Duels at the gates ({}): {} - {} challenges ({} to a real player), {} fought "
                                  "without leaving the crowd, {} duelling now, {} walking over now, {} given up; {} "
                                  "pairs talked into one went out to the gates ({} fought it out, {} never did, {} on "
                                  "their way now).",
                                  spots.empty() ? std::string("nowhere") : spots, OnOff(g_DuelsEnable), d.challenged,
                                  d.asked, h.duels, h.duelling, d.walking, d.gaveUp, d.pacts, d.pactsHeld,
                                  d.pactsLost, d.pactsNow));
        PbpLedgerStats const lg = PbpLedger_GetStats();
        out.push_back(fmt::format("Economy ledger: {} - {} things noted since startup ({} dropped, too many at once), "
                                  "written {} times; last look at the bots: {}.",
                                  OnOff(s.ledger.enable), lg.notes, lg.dropped, lg.writes,
                                  lg.lookedAt.empty() ? std::string("not yet")
                                                      : fmt::format("{} bots at {}", lg.looked, lg.lookedAt)));
        PbpDungeonsStats const bd = PbpDungeons_GetStats();
        out.push_back(fmt::format("Bot dungeons: {} - {} parties in the queue, {} runs going on; {} runs since startup "
                                  "({} cleared, {} failed).",
                                  OnOff(s.enable && s.dungeons.enable), bd.queuedNow, bd.runsNow, bd.runs, bd.cleared,
                                  bd.failed));
        PbpGroupQuestsStats const gq = PbpGroupQuests_GetStats();
        out.push_back(fmt::format("Group quests: {} - {} parties now; {} went since startup ({} alone), {} quests "
                                  "done.",
                                  OnOff(s.enable && s.groupQuests.enable), gq.partiesNow, gq.parties, gq.solos,
                                  gq.done));
        return out;
    }

    bool Write(bool fresh, std::string& where, uint32_t& lines)
    {
        std::vector<std::string> doc;
        auto section = [&doc](std::string const& title, std::vector<std::string> const& body)
        {
            doc.push_back(std::string());
            doc.push_back("== " + title + " ==");
            doc.insert(doc.end(), body.begin(), body.end());
        };
        const Seconds wall(std::time(nullptr));
        doc.push_back(fmt::format("Playerbots Plus, the full report: {} (the server up {}).",
                                  Acore::Time::TimeToTimestampStr(wall), Uptime()));
        doc.push_back(g_Pbp.report.minutes
                          ? fmt::format("Written every {} min and whenever .pbp report asks; .pbp status in game is the "
                                        "short of it.",
                                        g_Pbp.report.minutes)
                          : std::string("Written whenever .pbp report asks; .pbp status in game is the short of it."));
        section("In brief (.pbp status)", PbpReport_Overview());
        section("Everything counted since start", Detail());

        std::vector<std::string> hangouts = PbpHangout_Brief(true).lines;
        for (std::string& line : PbpHangout_SpotReport())
            hangouts.push_back(std::move(line));
        section("City hangouts: by city, then each spot (who is there, its room, and how the places looked at for "
                "it went)",
                hangouts);

        std::vector<std::string> dungeons = PbpDungeons_Status();
        for (std::string& line : PbpDungeons_Bands())
            dungeons.push_back(std::move(line));
        section("Bot dungeons: every queue and run", dungeons);
        section("Group quests: every party", PbpGroupQuests_Status());

        // The economy reads the ledger's table: fresh when asked for, and
        // at most once an hour on the clock.
        const Clock::time_point now = Clock::now();
        if (fresh || g_economy.empty() || now - g_economyAt >= std::chrono::hours(1))
        {
            g_economy = PbpLedger_Report(7);
            g_economyAt = now;
        }
        const uint32_t age = uint32_t(std::chrono::duration_cast<std::chrono::minutes>(now - g_economyAt).count());
        section(age ? fmt::format("The economy, last 7 days (as of {} min ago)", age)
                    : std::string("The economy, last 7 days"),
                g_economy);

        const std::string path = PbpReport_Path();
        std::ofstream file(path, std::ios::out | std::ios::trunc);
        if (!file)
        {
            where = "could not open " + path + " to write";
            return false;
        }
        for (std::string const& line : doc)
            file << PbpReport_Plain(line) << '\n';
        file.close();
        if (file.fail())
        {
            where = "could not write all of " + path;
            return false;
        }
        where = path;
        lines = uint32_t(doc.size());
        return true;
    }
}

std::string PbpReport_Plain(std::string const& line)
{
    std::string out;
    out.reserve(line.size());
    for (size_t i = 0; i < line.size(); ++i)
    {
        if (line[i] == '|' && i + 1 < line.size())
        {
            if (line[i + 1] == 'r')
            {
                ++i;
                continue;
            }
            if (line[i + 1] == 'c' && i + 9 < line.size())
            {
                i += 9;   // |cffrrggbb
                continue;
            }
        }
        out += line[i];
    }
    return out;
}

std::string PbpReport_Path()
{
    std::string const& file = g_Pbp.report.file;
    if (!file.empty() && (file.front() == '/' || file.find(':') != std::string::npos))
        return file;   // a whole path
    return sLog->GetLogsDir() + file;
}

std::vector<std::string> PbpReport_Overview()
{
    std::vector<std::string> lines, issues;
    auto take = [&lines, &issues](PbpBrief b, size_t most)
    {
        for (size_t i = 0; i < b.lines.size() && i < most; ++i)
            lines.push_back(std::move(b.lines[i]));
        for (std::string& issue : b.issues)
            issues.push_back(std::move(issue));
    };
    take(PbpHangout_Brief(false), 2);   // the hangouts, and city life
    lines.push_back(MarketLine());
    take(PbpDungeons_Brief(false), 1);
    take(PbpGroupQuests_Brief(false), 1);
    lines.push_back(FixesLine());
    if (PbpLedgerStats const lg = PbpLedger_GetStats(); g_Pbp.ledger.enable && lg.dropped)
        issues.push_back(fmt::format("The economy ledger dropped {} things it was told of, too many at once.",
                                     lg.dropped));

    std::vector<std::string> out;
    if (!g_Pbp.enable)
        out.push_back("|cff00ff00[Playerbots Plus]|r |cffff0000OFF|r (PlayerbotsPlus.Enable): nothing runs.");
    else if (issues.empty())
        out.push_back("|cff00ff00[Playerbots Plus]|r on -- everything running, nothing that looks wrong.");
    else
        out.push_back(fmt::format("|cff00ff00[Playerbots Plus]|r on -- |cffffd000{} thing{} to look at|r, below.",
                                  issues.size(), issues.size() == 1 ? "" : "s"));
    for (std::string& line : lines)
        out.push_back(std::move(line));
    for (size_t i = 0; i < issues.size() && i < 6; ++i)
        out.push_back("|cffff8000 ! " + issues[i] + "|r");
    if (issues.size() > 6)
        out.push_back(fmt::format("|cffff8000 ! ...and {} more, in the report.|r", issues.size() - 6));
    return out;
}

bool PbpReport_Write(std::string& where, uint32_t& lines)
{
    return Write(true, where, lines);
}

void PbpReport_Update(uint32_t diff)
{
    if (!g_Pbp.report.minutes)
    {
        g_reportMs = 0;
        return;
    }
    g_reportMs += diff;
    if (g_reportMs < g_Pbp.report.minutes * 60000u)
        return;
    g_reportMs = 0;
    std::string where;
    uint32_t lines = 0;
    const bool ok = Write(false, where, lines);
    // Said once when it starts failing, and once when it works again.
    if (!ok && !g_lastFailed)
        LOG_ERROR("module.playerbotsplus", "[Playerbots Plus] The full report was not written: {}.", where);
    else if (ok && g_lastFailed)
        LOG_INFO("module.playerbotsplus", "[Playerbots Plus] The full report is written again: {}.", where);
    g_lastFailed = !ok;
}
