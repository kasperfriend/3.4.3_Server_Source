#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/values/RtiValue.h"

using namespace ai;

RtiValue::RtiValue(PlayerbotAI* ai)
    : ManualSetValue<string>(ai, "skull")
{
}
