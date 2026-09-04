#pragma once
#include "../Value.h"
#include "strategy/values/TargetValue.h"

namespace ai
{
   
    class TankTargetValue : public TargetValue
	{
	public:
        TankTargetValue(PlayerbotAI* ai) : TargetValue(ai) {}

    public:
        Unit* Calculate();
    };
}
