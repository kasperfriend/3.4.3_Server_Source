#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/ChatShortcutActions.h"
#include "../../PlayerbotAIConfig.h"

using namespace ai;

bool FollowChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    ai->Reset();
    ai->ChangeStrategy("+follow,-passive", BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-follow,-passive", BOT_STATE_COMBAT);
    if (bot->GetMapId() != master->GetMapId() || bot->GetDistance(master) > sPlayerbotAIConfig.sightDistance)
    {
        ai->TellMaster("I will not follow you - too far away");
        return true;
    }
    ai->TellMaster("Following");
    return true;
}

bool StayChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    ai->Reset();
    ai->ChangeStrategy("+stay,-passive", BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-follow,-passive", BOT_STATE_COMBAT);
    ai->TellMaster("Staying");
    return true;
}

bool FleeChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    ai->Reset();
    ai->ChangeStrategy("+follow,+passive", BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("+follow,+passive", BOT_STATE_COMBAT);
    if (bot->GetMapId() != master->GetMapId() || bot->GetDistance(master) > sPlayerbotAIConfig.sightDistance)
    {
        ai->TellMaster("I will not flee with you - too far away");
        return true;
    }
    ai->TellMaster("Fleeing");
    return true;
}

bool GoawayChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    ai->Reset();
    ai->ChangeStrategy("+runaway", BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("+runaway", BOT_STATE_COMBAT);
    ai->TellMaster("Running away");
    return true;
}

bool GrindChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    ai->Reset();
    // Grind selects the target (assist strategy) while "move random" provides
    // the legs (movement strategy): grinding bots roam on their own and kill
    // everything they see. A leash in the grind strategy walks them back when
    // they wander out of the master's react range. Say "follow" or "stay"
    // after "grind" to change back to escorted or stationary grinding.
    ai->ChangeStrategy("+grind,+move random,-passive", BOT_STATE_NON_COMBAT);
    if (bot->GetMapId() != master->GetMapId() || bot->GetDistance(master) > sPlayerbotAIConfig.reactDistance)
    {
        ai->TellMaster("Grinding on my own where I stand - summon me to you, I am too far away");
        return true;
    }
    ai->TellMaster("Grinding on my own");
    return true;
}

bool TankAttackChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    if (!ai->IsTank(bot))
        return false;

    ai->Reset();
    ai->ChangeStrategy("-passive", BOT_STATE_NON_COMBAT);
    ai->ChangeStrategy("-passive", BOT_STATE_COMBAT);
    ai->TellMaster("Attacking");
    return true;
}

bool MaxDpsChatShortcutAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    ai->Reset();
    ai->ChangeStrategy("-threat,-conserve mana,-cast time,+dps debuff", BOT_STATE_COMBAT);
    ai->TellMaster("Max DPS");
    return true;
}
