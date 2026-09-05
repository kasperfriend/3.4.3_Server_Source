#include "../../pchdef.h"
#include "../playerbot.h"
#include "strategy/AiObjectContext.h"
#include "strategy/NamedObjectContext.h"
#include "strategy/StrategyContext.h"
#include "strategy/triggers/TriggerContext.h"
#include "strategy/actions/ActionContext.h"
#include "strategy/triggers/ChatTriggerContext.h"
#include "strategy/actions/ChatActionContext.h"
#include "strategy/triggers/WorldPacketTriggerContext.h"
#include "strategy/actions/WorldPacketActionContext.h"
#include "strategy/values/ValueContext.h"

using namespace ai;

AiObjectContext::AiObjectContext(PlayerbotAI* ai) : PlayerbotAIAware(ai)
{
    strategyContexts.Add(new StrategyContext());
    strategyContexts.Add(new MovementStrategyContext());
    strategyContexts.Add(new AssistStrategyContext());
    strategyContexts.Add(new QuestStrategyContext());

    actionContexts.Add(new ActionContext());
    actionContexts.Add(new ChatActionContext());
    actionContexts.Add(new WorldPacketActionContext());

    triggerContexts.Add(new TriggerContext());
    triggerContexts.Add(new ChatTriggerContext());
    triggerContexts.Add(new WorldPacketTriggerContext());

    valueContexts.Add(new ValueContext());
}

void AiObjectContext::Update()
{
    strategyContexts.Update();
    triggerContexts.Update();
    actionContexts.Update();
    valueContexts.Update();
}

void AiObjectContext::Reset()
{
    strategyContexts.Reset();
    triggerContexts.Reset();
    actionContexts.Reset();
    valueContexts.Reset();
}
