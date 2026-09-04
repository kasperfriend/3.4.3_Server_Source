#include "AhBot.h"
#include "Entities/Item/ItemTemplate.h"
#include <cmath>

double ahbot::AhBot::GetRarityPriceMultiplier(const ItemTemplate* proto)
{
    if (!proto)
        return 1.0;
    // Exponential scaling by quality, capped so epic/legendary items are
    // priced realistically for bots to buy/sell.
    double multiplier = std::pow(2.0, int32(proto->GetQuality()) - 1) * sAhBotConfig.priceQualityMultiplier;
    if (multiplier > 100.0)
        multiplier = 100.0;
    return multiplier;
}

int32 ahbot::AhBot::GetSellPrice(const ItemTemplate* proto)
{
    if (!proto)
        return 0;

    int32 base = proto->GetSellPrice() ? int32(proto->GetSellPrice()) : int32(proto->GetBuyPrice());
    if (!base)
    {
        // Estimate price from item level/quality when the template carries no price.
        int32 level = std::max(1, int32(proto->GetItemLevel()));
        base = int32(level * level * 0.02f) * (int32(proto->GetQuality()) + 1);
    }

    double price = double(base) * GetRarityPriceMultiplier(proto) * sAhBotConfig.priceMultiplier;
    if (price < sAhBotConfig.defaultMinPrice)
        price = sAhBotConfig.defaultMinPrice;

    return int32(price);
}

int32 ahbot::AhBot::GetBuyPrice(const ItemTemplate* proto)
{
    if (!proto)
        return 0;

    int32 sell = GetSellPrice(proto);
    if (!sell)
        return 0;

    return int32(sell * 1.5);
}
