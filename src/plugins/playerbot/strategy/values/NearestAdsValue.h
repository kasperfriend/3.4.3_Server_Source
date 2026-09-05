#pragma once
#include "../Value.h"
#include "strategy/values/NearestUnitsValue.h"
#include "../../PlayerbotAIConfig.h"
#include "strategy/values/PossibleTargetsValue.h"

namespace ai
{
    class NearestAdsValue : public PossibleTargetsValue
	{
	public:
        NearestAdsValue(PlayerbotAI* ai, float range = sPlayerbotAIConfig.tooCloseDistance) :
            PossibleTargetsValue(ai, range) {}

    protected:
        bool AcceptUnit(Unit* unit);
	};
}
