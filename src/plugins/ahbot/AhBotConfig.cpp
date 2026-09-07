#include <algorithm>
#include <cmath>
#include <sstream>
#include "Log.h"
#include "AhBotConfig.h"

bool AhBotConfig::Initialize()
{
    enabled = sConfigMgr->GetBoolDefault("AhBot.Enabled", false);
    priceMultiplier = sConfigMgr->GetFloatDefault("AhBot.PriceMultiplier", 1.0f);
    priceQualityMultiplier = sConfigMgr->GetFloatDefault("AhBot.PriceQualityMultiplier", 1.0f);
    defaultMinPrice = uint32(std::max(0, sConfigMgr->GetIntDefault("AhBot.DefaultMinPrice", 0)));
    maxItemLevel = uint32(std::max(0, sConfigMgr->GetIntDefault("AhBot.MaxItemLevel", 0)));
    maxRequiredLevel = uint32(std::max(0, sConfigMgr->GetIntDefault("AhBot.MaxRequiredLevel", 0)));
    underPriceProbability = sConfigMgr->GetFloatDefault("AhBot.UnderPriceProbability", 0.0f);

    auto positiveMultiplier = [](float& value, char const* name)
    {
        if (!std::isfinite(value) || value <= 0.0f)
        {
            TC_LOG_ERROR("playerbot", "AhBotConfig: {} ({}) must be finite and positive; using 1.0", name, value);
            value = 1.0f;
        }
    };
    positiveMultiplier(priceMultiplier, "AhBot.PriceMultiplier");
    positiveMultiplier(priceQualityMultiplier, "AhBot.PriceQualityMultiplier");

    ignoreItemIds.clear();
    if (std::string ignored = sConfigMgr->GetStringDefault("AhBot.IgnoreItems", ""); !ignored.empty())
    {
        std::stringstream ss(ignored);
        std::string item;
        while (std::getline(ss, item, ','))
            ignoreItemIds.insert(uint32(strtoul(item.c_str(), nullptr, 10)));
    }
    return true;
}
