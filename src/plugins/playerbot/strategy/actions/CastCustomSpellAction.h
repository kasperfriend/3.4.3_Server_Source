#pragma once

#include "../Action.h"
#include "strategy/actions/QuestAction.h"

namespace ai
{
    class CastCustomSpellAction : public Action
    {
    public:
        CastCustomSpellAction(PlayerbotAI* ai) : Action(ai, "cast custom spell") {}
        virtual bool Execute(Event event);
    };
}
