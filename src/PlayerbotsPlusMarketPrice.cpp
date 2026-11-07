#include "PlayerbotsPlusMarketPrice.h"

#include <algorithm>
#include <cmath>

namespace PbpPrice
{
    namespace
    {
        uint32_t QualityIndex(uint32_t quality)
        {
            return std::min<uint32_t>(quality, kQualities - 1);
        }

        uint64_t Percent(uint64_t amount, uint64_t pct)
        {
            // Large amounts first divided, so nothing overflows.
            if (amount > (UINT64_MAX / 100) / std::max<uint64_t>(pct, 1))
                return amount / 100 * pct;
            return amount * pct / 100;
        }
    }

    uint32_t LevelOf(ItemFacts const& item)
    {
        const uint32_t level = item.requiredLevel ? item.requiredLevel : item.itemLevel;
        return std::clamp<uint32_t>(level, 1, 80);
    }

    uint64_t Wealth(Settings const& s, uint32_t level)
    {
        const double growth = s.wealthGrowth > 0.1 ? s.wealthGrowth : 0.1;
        const double wealth =
            std::max(0.0, s.wealthBaseCopper) * std::exp(double(std::min<uint32_t>(level, 100)) / growth);
        return wealth >= 1e15 ? uint64_t(1e15) : uint64_t(wealth);
    }

    uint64_t Value(Settings const& s, ItemFacts const& item)
    {
        const uint32_t pct = item.gear ? s.gearValuePct[QualityIndex(item.quality)] : s.goodsValuePct;
        return std::max<uint64_t>(Percent(item.vendor, pct), uint64_t(item.vendor) + 1);
    }

    uint64_t Cap(Settings const& s, ItemFacts const& item, uint32_t level)
    {
        const uint64_t wealth = Wealth(s, level);
        if (item.gear)
            return std::max<uint64_t>(Percent(wealth, s.gearCapPct[QualityIndex(item.quality)]), 1);
        return std::max<uint64_t>(Percent(wealth, s.stackCapPct) / std::max<uint32_t>(item.maxStack, 1), 1);
    }

    uint64_t Floor(Settings const& s, ItemFacts const& item)
    {
        return std::max<uint64_t>(Percent(item.vendor, s.floorPct), 1);
    }

    uint64_t ListUnit(Settings const& s, ItemFacts const& item, uint64_t marketUnit, uint32_t expiries,
                      double roll1, double roll2)
    {
        roll1 = std::clamp(roll1, 0.0, 0.999999);
        roll2 = std::clamp(roll2, 0.0, 0.999999);

        const uint64_t floor = Floor(s, item);
        const uint64_t cap   = std::max(Cap(s, item, LevelOf(item)), floor);

        uint64_t price = 0;
        if (marketUnit)
        {
            const uint32_t lo  = std::min(s.undercutMinPct, s.undercutMaxPct);
            const uint32_t hi  = std::max(s.undercutMinPct, s.undercutMaxPct);
            const uint32_t pct = std::min<uint32_t>(lo + uint32_t(roll1 * double(hi - lo + 1)), 90);
            price = Percent(marketUnit, 100 - pct);
            if (price >= marketUnit && marketUnit > 1)
                price = marketUnit - 1;   // a copper under it, at least
            if (price < floor)
                return 0;                 // cheaper than a vendor already: not worth it
            price = std::min(price, cap);
        }
        else
        {
            // 95% to 115% of what it is worth, so not every bot asks the same.
            price = Percent(Value(s, item), 95 + uint64_t(roll2 * 21.0));
            price = std::clamp(price, floor, cap);
        }

        if (expiries)
        {
            const uint64_t off = std::min<uint64_t>(uint64_t(s.relistDiscountPct) * expiries, 90);
            price = std::max(Percent(price, 100 - off), floor);
        }
        return price;
    }

    uint64_t GearBudget(Settings const& s, ItemFacts const& item, uint32_t botLevel, uint64_t spendable)
    {
        return std::min(Cap(s, item, std::clamp<uint32_t>(botLevel, 1, 80)), spendable);
    }

    uint64_t GoodsBudgetUnit(Settings const& s, ItemFacts const& item, uint32_t overValuePct)
    {
        return Percent(Value(s, item), overValuePct);
    }

    std::string MoneyText(uint64_t copper)
    {
        const uint64_t gold = copper / 10000, silver = (copper / 100) % 100, rest = copper % 100;
        std::string out;
        auto add = [&out](uint64_t n, char unit)
        {
            if (!out.empty())
                out += ' ';
            out += std::to_string(n);
            out += unit;
        };
        if (gold)
            add(gold, 'g');
        if (silver)
            add(silver, 's');
        if (rest || out.empty())
            add(rest, 'c');
        return out;
    }
}
