#pragma once

#include "strategy/mage/GenericMageStrategy.h"

namespace ai
{
    class ArcaneMageStrategy : public GenericMageStrategy
    {
    public:
        ArcaneMageStrategy(PlayerbotAI* ai);

    public:
        virtual void InitTriggers(std::list<TriggerNode*> &triggers);
        virtual string getName() { return "arcane"; }
        virtual NextAction** getDefaultActions();
    };

}
