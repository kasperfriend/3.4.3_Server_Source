#include "../../pchdef.h"
#include "../playerbot.h"
#include "strategy/Trigger.h"
#include "strategy/Action.h"

using namespace ai;

Event Trigger::Check()
{
	if (IsActive())
	{
		Event event(getName());
		return event;
	}
	Event event;
	return event;
}

Value<Unit*>* Trigger::GetTargetValue()
{
    return context->GetValue<Unit*>(GetTargetName());
}

Unit* Trigger::GetTarget()
{
    return GetTargetValue()->Get();
}
