#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/shaman/ShamanMultipliers.h"
#include "strategy/shaman/TotemsShamanStrategy.h"

using namespace ai;

TotemsShamanStrategy::TotemsShamanStrategy(PlayerbotAI* ai) : GenericShamanStrategy(ai)
{
}

void TotemsShamanStrategy::InitTriggers(std::list<TriggerNode*> &triggers)
{
    GenericShamanStrategy::InitTriggers(triggers);

    triggers.push_back(new TriggerNode(
        "windfury totem",
        NextAction::array(0, new NextAction("windfury totem", 16.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "mana spring totem",
        NextAction::array(0, new NextAction("mana spring totem", 19.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "strength of earth totem",
        NextAction::array(0, new NextAction("strength of earth totem", 18.0f), NULL)));

    triggers.push_back(new TriggerNode(
        "flametongue totem",
        NextAction::array(0, new NextAction("flametongue totem", 17.0f), NULL)));
}
