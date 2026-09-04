#pragma once

#include "../Action.h"
#include "strategy/actions/InventoryAction.h"

namespace ai
{
    class TradeAction : public InventoryAction {
    public:
        TradeAction(PlayerbotAI* ai) : InventoryAction(ai, "trade") {}
        virtual bool Execute(Event event);

    private:
        bool TradeItem(const Item& item, int8 slot);

        static map<string, uint32> slots;
    };

}
