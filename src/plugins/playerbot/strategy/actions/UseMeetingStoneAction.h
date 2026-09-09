#pragma once

#include "../Action.h"
#include "strategy/actions/MovementActions.h"

namespace ai
{
    class SummonAction : public MovementAction
    {
    public:
        SummonAction(PlayerbotAI* ai, string name = "summon") : MovementAction(ai, name) {}

        virtual bool Execute(Event event);

    protected:
        bool Teleport();
        bool TeleportBot(uint32 mapId, float x, float y, float z);
        bool CanBeSummonedBy(Player* master);
    };

    class UseMeetingStoneAction : public SummonAction
    {
    public:
        UseMeetingStoneAction(PlayerbotAI* ai) : SummonAction(ai, "use meeting stone") {}

        virtual bool Execute(Event event);
    };
}
