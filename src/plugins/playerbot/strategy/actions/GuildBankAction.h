#pragma once

#include "../Action.h"
#include "strategy/actions/InventoryAction.h"

namespace ai
{
    class GuildBankAction : public InventoryAction {
    public:
        GuildBankAction(PlayerbotAI* ai) : InventoryAction(ai, "guild bank") {}
        virtual bool Execute(Event event);

    private:
        bool Execute(string text, GameObject* bank);
        bool MoveFromCharToBank(Item* item, GameObject* bank);
    };

}
