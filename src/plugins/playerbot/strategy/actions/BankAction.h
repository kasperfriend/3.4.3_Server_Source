#pragma once

#include "../Action.h"
#include "strategy/actions/InventoryAction.h"

namespace ai
{
    class BankAction : public InventoryAction {
    public:
        BankAction(PlayerbotAI* ai) : InventoryAction(ai, "bank") {}
        virtual bool Execute(Event event);

    private:
        bool Execute(string text, Unit* bank);
        void ListItems();
        bool Withdraw(const uint32 itemid);
        bool Deposit(Item* pItem);
        Item* FindItemInBank(uint32 ItemId);
    };

}
