#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/generic/StayStrategy.h"

using namespace ai;

NextAction** StayStrategy::getDefaultActions()
{
    return NextAction::array(0, new NextAction("stay", 1.0f), NULL);
}

