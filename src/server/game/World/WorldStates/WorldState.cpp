#include "Log.h"
#include "Player.h"
#include "SharedDefines.h"
#include "WorldState.h"

WorldState* WorldState::instance()
{
    static WorldState instance;
    return &instance;
}

WorldState::WorldState()
{
    _transportStates[WORLD_STATE_CONDITION_THE_IRON_EAGLE]      = WORLD_STATE_CONDITION_STATE_NONE;
    _transportStates[WORLD_STATE_CONDITION_THE_PURPLE_PRINCESS] = WORLD_STATE_CONDITION_STATE_NONE;
    _transportStates[WORLD_STATE_CONDITION_THE_THUNDERCALLER]   = WORLD_STATE_CONDITION_STATE_NONE;
}

WorldState::~WorldState()
{
}

bool WorldState::IsConditionFulfilled(WorldStateCondition conditionId, WorldStateConditionState state) const
{
    switch (conditionId)
    {
        case WORLD_STATE_CONDITION_THE_IRON_EAGLE:
        case WORLD_STATE_CONDITION_THE_PURPLE_PRINCESS:
        case WORLD_STATE_CONDITION_THE_THUNDERCALLER:
            return _transportStates.at(conditionId) == state;
        default:
            TC_LOG_ERROR("scripts", "WorldState::IsConditionFulfilled: Unhandled WorldStateCondition {}", conditionId);
            return false;
    }
}

void WorldState::HandleConditionStateChange(WorldStateCondition conditionId, WorldStateConditionState state)
{
    _transportStates[conditionId] = state;
}
