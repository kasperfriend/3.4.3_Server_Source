#pragma once
#include "../Value.h"
#include "strategy/values/TargetValue.h"

namespace ai
{
    class LeastHpTargetValue : public TargetValue
	{
	public:
        LeastHpTargetValue(PlayerbotAI* ai) : TargetValue(ai) {}

    public:
        Unit* Calculate();
    };
}
