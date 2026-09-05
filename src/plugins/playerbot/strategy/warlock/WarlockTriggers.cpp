#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/warlock/WarlockTriggers.h"
#include "strategy/warlock/WarlockActions.h"

using namespace ai;

bool DemonArmorTrigger::IsActive() 
{
	Unit* target = GetTarget();
	return !ai->HasAura("demon skin", target) &&
		!ai->HasAura("demon armor", target) &&
		!ai->HasAura("fel armor", target);
}

bool SpellstoneTrigger::IsActive() 
{
    return BuffTrigger::IsActive() && AI_VALUE2(uint8, "item count", getName()) > 0;
}
