#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/mage/MageTriggers.h"
#include "strategy/mage/MageActions.h"

using namespace ai;

bool MageArmorTrigger::IsActive()
{
    Unit* target = GetTarget();
    return !ai->HasAura("ice armor", target) &&
        !ai->HasAura("frost armor", target) &&
        !ai->HasAura("molten armor", target) &&
        !ai->HasAura("mage armor", target);
}
