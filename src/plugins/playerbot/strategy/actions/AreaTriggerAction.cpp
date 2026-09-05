#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/AreaTriggerAction.h"
#include "../../PlayerbotAIConfig.h"


using namespace ai;

bool ReachAreaTriggerAction::Execute(Event event)
{
    uint32 triggerId;
    WorldPacket p(event.getPacket());
    p.rpos(0);
    p >> triggerId;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    AreaTriggerStruct const* at = sObjectMgr->GetAreaTrigger(triggerId);
    if (!at)
    {
        WorldPackets::AreaTrigger::AreaTrigger packet{WorldPacket(CMSG_AREA_TRIGGER)};
        packet.AreaTriggerID = int32(triggerId);
        packet.Entered = true;
        packet.FromClient = true;
        bot->GetSession()->HandleAreaTriggerOpcode(packet);

        return true;
    }

    if (bot->GetMapId() != uint32(atEntry->ContinentID) || bot->GetDistance(atEntry->Pos.X, atEntry->Pos.Y, atEntry->Pos.Z) > sPlayerbotAIConfig.sightDistance)
    {
        ai->TellMaster("I won't follow: too far away");
        return true;
    }

    MotionMaster &mm = *bot->GetMotionMaster();
    mm.Clear();
	mm.MovePoint(uint32(atEntry->ContinentID), atEntry->Pos.X, atEntry->Pos.Y, atEntry->Pos.Z);
    float distance = bot->GetDistance(atEntry->Pos.X, atEntry->Pos.Y, atEntry->Pos.Z);
    float delay = 1000.0f * distance / bot->GetSpeed(MOVE_RUN) + sPlayerbotAIConfig.reactDelay;
    ai->TellMaster("Wait for me");
    ai->SetNextCheckDelay(delay);
    context->GetValue<LastMovement&>("last movement")->Get().lastAreaTrigger = triggerId;

    return true;
}



bool ai::AreaTriggerAction::Execute(Event event)
{
    LastMovement& movement = context->GetValue<LastMovement&>("last movement")->Get();

    uint32 triggerId = movement.lastAreaTrigger;
    movement.lastAreaTrigger = 0;

    AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(triggerId);
    if(!atEntry)
        return false;

    AreaTriggerStruct const* at = sObjectMgr->GetAreaTrigger(triggerId);
    if (!at)
        return true;

    WorldPackets::AreaTrigger::AreaTrigger packet{WorldPacket(CMSG_AREA_TRIGGER)};
    packet.AreaTriggerID = int32(triggerId);
    packet.Entered = true;
    packet.FromClient = true;
    bot->GetSession()->HandleAreaTriggerOpcode(packet);

    ai->TellMaster("Hello");
    return true;
}
