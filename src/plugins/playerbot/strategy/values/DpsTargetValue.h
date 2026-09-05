#pragma once
#include "../Value.h"
#include "strategy/values/RtiTargetValue.h"
#include "strategy/values/TargetValue.h"

namespace ai
{
    class DpsTargetValue : public RtiTargetValue
	{
	public:
        DpsTargetValue(PlayerbotAI* ai) : RtiTargetValue(ai) {}

    public:
        Unit* Calculate();
    };
}
