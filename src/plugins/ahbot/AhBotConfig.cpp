#include <sstream>
#include "AhBotConfig.h"

bool AhBotConfig::Initialize()
{
    enabled = sConfigMgr->GetBoolDefault("AhBot.Enabled", false);
    priceMultiplier = sConfigMgr->GetFloatDefault("AhBot.PriceMultiplier", 1.0f);
    priceQualityMultiplier = sConfigMgr->GetFloatDefault("AhBot.PriceQualityMultiplier", 1.0f);
    defaultMinPrice = uint32(sConfigMgr->GetIntDefault("AhBot.DefaultMinPrice", 0));
    maxItemLevel = uint32(sConfigMgr->GetIntDefault("AhBot.MaxItemLevel", 0));
    maxRequiredLevel = uint32(sConfigMgr->GetIntDefault("AhBot.MaxRequiredLevel", 0));
    underPriceProbability = sConfigMgr->GetFloatDefault("AhBot.UnderPriceProbability", 0.0f);

    if (std::string ignored = sConfigMgr->GetStringDefault("AhBot.IgnoreItems", ""); !ignored.empty())
    {
        std::stringstream ss(ignored);
        std::string item;
        while (std::getline(ss, item, ','))
            ignoreItemIds.insert(uint32(strtoul(item.c_str(), nullptr, 10)));
    }
    return true;
}
