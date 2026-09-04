#pragma once

#include "AhBotConfig.h"

// Minimal subset of ike3's AhBot used by the playerbot plugin: item pricing
// heuristics only. The actual auction-house bot is handled by the built-in
// AuctionHouseBot module.
namespace ahbot
{
    class AhBot
    {
    public:
        AhBot() = default;

        static AhBot& instance()
        {
            static AhBot instance;
            return instance;
        }

        void Init() {}
        void Update() {}

        int32 GetSellPrice(const ItemTemplate* proto);
        int32 GetBuyPrice(const ItemTemplate* proto);
        double GetRarityPriceMultiplier(const ItemTemplate* proto);
    };
}

#define auctionbot ahbot::AhBot::instance()
