#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/mage/MageActions.h"

using namespace ai;

Value<Unit*>* CastPolymorphAction::GetTargetValue()
{
    return context->GetValue<Unit*>("cc target", getName());
}
