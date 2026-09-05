#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/rogue/RogueTriggers.h"
#include "strategy/rogue/RogueMultipliers.h"
#include "strategy/rogue/GenericRogueNonCombatStrategy.h"
#include "strategy/rogue/RogueActions.h"

using namespace ai;

void GenericRogueNonCombatStrategy::InitTriggers(std::list<TriggerNode*> &triggers)
{
    NonCombatStrategy::InitTriggers(triggers);
        
}
