#pragma once

#include "../Action.h"
#include "strategy/actions/InventoryAction.h"

namespace ai
{
    class SellAction : public InventoryAction {
    public:
        SellAction(PlayerbotAI* ai) : InventoryAction(ai, "sell") {}
        virtual bool Execute(Event event);

        void Sell(FindItemVisitor* visitor);
        void Sell(Item* item);

    };

}