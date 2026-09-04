#pragma once

#include "../Action.h"
#include "strategy/actions/GenericSpellActions.h"
#include "strategy/actions/ReachTargetActions.h"
#include "strategy/actions/ChooseTargetActions.h"
#include "strategy/actions/MovementActions.h"

namespace ai
{
    class MeleeAction : public AttackAction 
    {
    public:
        MeleeAction(PlayerbotAI* ai) : AttackAction(ai, "melee") {}

        virtual string GetTargetName() { return "current target"; }
    };

}