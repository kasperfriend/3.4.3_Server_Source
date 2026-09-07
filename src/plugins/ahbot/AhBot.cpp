#include "../pchdef.h"
#include "AhBot.h"
#include "Entities/Item/ItemTemplate.h"
#include <cmath>
#include <limits>

namespace
{
    int32 ClampBotPrice(double price)
    {
        // Prices enter signed-int32 trade/guild-task APIs. Never convert NaN,
        // infinity or an out-of-range value directly to an integer.
        if (!(price > 0.0))
            return 0;
        double maximum = std::numeric_limits<int32>::max();
        return price >= maximum ? std::numeric_limits<int32>::max() : int32(price);
    }
}

double ahbot::AhBot::GetRarityPriceMultiplier(const ItemTemplate* proto)
{
    if (!proto)
        return 1.0;
    // Exponential scaling by quality, capped so epic/legendary items are
    // priced realistically for bots to buy/sell.
    double multiplier = std::pow(2.0, double(proto->GetQuality()) - 1.0) * sAhBotConfig.priceQualityMultiplier;
    if (!std::isfinite(multiplier) || multiplier <= 0.0)
        return 1.0;
    return std::min(multiplier, 100.0);
}

int32 ahbot::AhBot::GetSellPrice(const ItemTemplate* proto)
{
    if (!proto)
        return 0;

    double base = proto->GetSellPrice() ? double(proto->GetSellPrice()) : double(proto->GetBuyPrice());
    if (!base)
    {
        // Estimate price from item level/quality when the template carries no price.
        double level = std::max(1.0, double(proto->GetItemLevel()));
        base = std::floor(level * level * 0.02) * (double(proto->GetQuality()) + 1.0);
    }

    double price = double(base) * GetRarityPriceMultiplier(proto) * sAhBotConfig.priceMultiplier;
    if (price < sAhBotConfig.defaultMinPrice)
        price = sAhBotConfig.defaultMinPrice;

    return ClampBotPrice(price);
}

int32 ahbot::AhBot::GetBuyPrice(const ItemTemplate* proto)
{
    if (!proto)
        return 0;

    int32 sell = GetSellPrice(proto);
    if (!sell)
        return 0;

    return ClampBotPrice(double(sell) * 1.5);
}
