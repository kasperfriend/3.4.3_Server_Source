#pragma once

#include "Configuration/Config.h"
#include <map>
#include <set>
#include <string>

// Minimal subset of ike3's AhBotConfig used by the playerbot plugin.
// The real auction-house bot functionality is provided by the built-in
// AuctionHouseBot module in src/server/game/AuctionHouseBot.
class AhBotConfig
{
public:
    AhBotConfig() = default;
    static AhBotConfig& instance()
    {
        static AhBotConfig instance;
        return instance;
    }

public:
    bool Initialize();

    bool enabled = false;
    float priceMultiplier = 1.0f;
    float priceQualityMultiplier = 1.0f;
    uint32 defaultMinPrice = 0;
    uint32 maxItemLevel = 0;
    uint32 maxRequiredLevel = 0;
    float underPriceProbability = 0;
    std::set<uint32> ignoreItemIds;
};

#define sAhBotConfig AhBotConfig::instance()
