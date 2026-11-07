#include "PlayerbotsPlusConfig.h"

#include "Config.h"
#include "Log.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

PbpSettings g_Pbp;

namespace
{
    std::vector<std::string> Split(std::string const& raw, char sep)
    {
        std::vector<std::string> out;
        std::string cur;
        auto flush = [&]()
        {
            size_t b = 0, e = cur.size();
            while (b < e && std::isspace(static_cast<unsigned char>(cur[b])))
                ++b;
            while (e > b && std::isspace(static_cast<unsigned char>(cur[e - 1])))
                --e;
            if (e > b)
                out.push_back(cur.substr(b, e - b));
            cur.clear();
        };
        for (char c : raw)
        {
            if (c == sep)
                flush();
            else
                cur += c;
        }
        flush();
        return out;
    }

    bool ToUInt(std::string const& s, uint32_t& out)
    {
        if (s.empty() || !std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c) != 0; }))
            return false;
        try
        {
            const unsigned long v = std::stoul(s);
            out = uint32_t(std::min<unsigned long>(v, UINT32_MAX));
            return true;
        }
        catch (...)
        {
            return false;
        }
    }

    uint32_t U(char const* key, uint32_t def)
    {
        return sConfigMgr->GetOption<uint32_t>(key, def);
    }

    bool B(char const* key, bool def)
    {
        return sConfigMgr->GetOption<bool>(key, def);
    }

    // "40:120" -> 40, 120; "60" -> 60, 60.
    void Range(char const* key, uint32_t& lo, uint32_t& hi, uint32_t defLo, uint32_t defHi)
    {
        const std::string def = std::to_string(defLo) + ":" + std::to_string(defHi);
        const std::vector<std::string> parts = Split(sConfigMgr->GetOption<std::string>(key, def), ':');
        uint32_t a = defLo, b = defHi;
        if (parts.size() == 1 && ToUInt(parts[0], a))
            b = a;
        else if (parts.size() != 2 || !ToUInt(parts[0], a) || !ToUInt(parts[1], b))
        {
            LOG_ERROR("module.playerbotsplus", "[Playerbots Plus] {} should look like {}; using that", key, def);
            a = defLo;
            b = defHi;
        }
        lo = std::min(a, b);
        hi = std::max(a, b);
    }

    // "100,250,500" into an array, keeping the default for anything missing.
    template <size_t N>
    void List(char const* key, std::array<uint32_t, N>& out)
    {
        std::string def;
        for (size_t i = 0; i < N; ++i)
            def += (i ? "," : "") + std::to_string(out[i]);
        const std::vector<std::string> parts = Split(sConfigMgr->GetOption<std::string>(key, def), ',');
        for (size_t i = 0; i < N && i < parts.size(); ++i)
        {
            uint32_t v = 0;
            if (ToUInt(parts[i], v))
                out[i] = v;
            else
                LOG_ERROR("module.playerbotsplus", "[Playerbots Plus] {}: '{}' is not a number", key, parts[i]);
        }
    }

    std::unordered_set<uint32_t> IdSet(char const* key, std::string const& def)
    {
        std::unordered_set<uint32_t> out;
        for (std::string const& s : Split(sConfigMgr->GetOption<std::string>(key, def), ','))
        {
            uint32_t v = 0;
            if (ToUInt(s, v) && v)
                out.insert(v);
            else
                LOG_ERROR("module.playerbotsplus", "[Playerbots Plus] {}: '{}' is not an id", key, s);
        }
        return out;
    }
}

void PlayerbotsPlus_LoadConfig(bool reload)
{
    PbpSettings s;   // starts from the defaults in the header

    s.enable = B("PlayerbotsPlus.Enable", true);

    PbpMarketSettings& m = s.market;
    m.enable            = B("PlayerbotsPlus.Market.Enable", true);
    m.intervalMs        = std::max<uint32_t>(1, U("PlayerbotsPlus.Market.IntervalSeconds", 10)) * 1000;
    m.visitsPerInterval = std::clamp<uint32_t>(U("PlayerbotsPlus.Market.VisitsPerInterval", 1), 1, 20);
    Range("PlayerbotsPlus.Market.VisitCooldownMinutes", m.cooldownMinMinutes, m.cooldownMaxMinutes, 40, 120);
    m.minLevel          = U("PlayerbotsPlus.Market.MinLevel", 10);
    m.extraZones        = IdSet("PlayerbotsPlus.Market.ExtraZoneOrAreaIds", "3703,4395");

    m.sell                   = B("PlayerbotsPlus.Market.Sell.Enable", true);
    m.sellMinQuality         = std::min<uint32_t>(U("PlayerbotsPlus.Market.Sell.MinQuality", 1), 6);
    m.sellMaxQuality         = std::min<uint32_t>(U("PlayerbotsPlus.Market.Sell.MaxQuality", 4), 6);
    m.maxListingsPerVisit    = U("PlayerbotsPlus.Market.Sell.MaxPerVisit", 3);
    m.maxAuctionsPerBot      = U("PlayerbotsPlus.Market.Sell.MaxAuctionsPerBot", 8);
    m.maxBotAuctionsPerHouse = U("PlayerbotsPlus.Market.Sell.MaxBotAuctionsPerHouse", 2000);
    m.maxRelists             = U("PlayerbotsPlus.Market.Sell.MaxRelists", 2);
    List("PlayerbotsPlus.Market.Sell.DurationWeights", m.durationWeights);
    m.maxBuyoutGold          = std::max<uint32_t>(1, U("PlayerbotsPlus.Market.Sell.MaxBuyoutGold", 50000));

    m.buy                   = B("PlayerbotsPlus.Market.Buy.Enable", true);
    m.buyUpgrades           = B("PlayerbotsPlus.Market.Buy.Upgrades", true);
    m.buyMaterials          = B("PlayerbotsPlus.Market.Buy.Materials", true);
    m.maxBuysPerVisit       = U("PlayerbotsPlus.Market.Buy.MaxPerVisit", 2);
    m.buySharePct           = std::min<uint32_t>(U("PlayerbotsPlus.Market.Buy.MaxShareOfGoldPct", 25), 100);
    m.reservePerLevel       = U("PlayerbotsPlus.Market.Buy.ReserveCopperPerLevel", 2000);
    m.upgradeMinQuality     = std::min<uint32_t>(U("PlayerbotsPlus.Market.Buy.UpgradeMinQuality", 2), 6);
    m.upgradeLevelWindow    = U("PlayerbotsPlus.Market.Buy.UpgradeLevelWindow", 6);
    m.materialsOverValuePct = U("PlayerbotsPlus.Market.Buy.MaterialsMaxPctOfValue", 200);
    m.maxUsageChecks        = std::max<uint32_t>(1, U("PlayerbotsPlus.Market.MaxItemChecksPerVisit", 30));

    m.collectMail = B("PlayerbotsPlus.Market.CollectMail", true);
    m.equipBought = B("PlayerbotsPlus.Market.EquipBought", true);
    m.logTrades   = B("PlayerbotsPlus.Market.LogTrades", false);

    PbpPrice::Settings& p = m.price;
    List("PlayerbotsPlus.Market.Price.GearValuePct", p.gearValuePct);
    p.goodsValuePct     = U("PlayerbotsPlus.Market.Price.GoodsValuePct", 300);
    p.floorPct          = std::max<uint32_t>(U("PlayerbotsPlus.Market.Price.FloorPctOfVendor", 110), 100);
    p.wealthBaseCopper  = sConfigMgr->GetOption<float>("PlayerbotsPlus.Market.Price.WealthBaseCopper", 10000.0f);
    p.wealthGrowth      = sConfigMgr->GetOption<float>("PlayerbotsPlus.Market.Price.WealthGrowthLevels", 10.5f);
    List("PlayerbotsPlus.Market.Price.GearCapPctOfWealth", p.gearCapPct);
    p.stackCapPct       = U("PlayerbotsPlus.Market.Price.StackCapPctOfWealth", 20);
    Range("PlayerbotsPlus.Market.Price.UndercutPct", p.undercutMinPct, p.undercutMaxPct, 1, 5);
    p.undercutMaxPct    = std::min<uint32_t>(p.undercutMaxPct, 50);
    p.undercutMinPct    = std::min(p.undercutMinPct, p.undercutMaxPct);
    p.relistDiscountPct = std::min<uint32_t>(U("PlayerbotsPlus.Market.Price.RelistDiscountPct", 15), 50);

    if (m.sellMinQuality > m.sellMaxQuality)
        std::swap(m.sellMinQuality, m.sellMaxQuality);

    g_Pbp = std::move(s);

    LOG_INFO(reload ? "module.playerbotsplus" : "server.loading",
             "[Playerbots Plus] {} settings: {}, market {} (a bot every {}s, again after {}-{} min, sells {}, buys {})",
             reload ? "Reloaded" : "Loaded", g_Pbp.enable ? "on" : "off", g_Pbp.market.enable ? "on" : "off",
             g_Pbp.market.intervalMs / 1000, g_Pbp.market.cooldownMinMinutes, g_Pbp.market.cooldownMaxMinutes,
             g_Pbp.market.sell ? "yes" : "no", g_Pbp.market.buy ? "yes" : "no");
}
