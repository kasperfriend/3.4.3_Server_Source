#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/values/DuelTargetValue.h"

using namespace ai;

Unit* DuelTargetValue::Calculate()
{
    return bot->duel ? bot->duel->Opponent : NULL;
}
