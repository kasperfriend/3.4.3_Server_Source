#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/generic/GrindingStrategy.h"

using namespace ai;


NextAction** GrindingStrategy::getDefaultActions()
{
    return NULL;
}

void GrindingStrategy::InitTriggers(std::list<TriggerNode*> &triggers)
{
    triggers.push_back(new TriggerNode(
        "no target",
        NextAction::array(0,
        new NextAction("attack anything", 5.0f), NULL)));

    // Leash: grinding bots roam on their own ("move random" movement), but if
    // they wander too far from the master they walk back into react range
    // instead of roaming across the zone.
    triggers.push_back(new TriggerNode(
        "out of react range",
        NextAction::array(0,
        new NextAction("follow", 6.0f), NULL)));
}

