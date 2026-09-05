#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/TaxiAction.h"
#include "../values/LastMovementValue.h"

using namespace ai;

bool TaxiAction::Execute(Event event)
{
    ai->RemoveShapeshift();

    LastMovement& movement = context->GetValue<LastMovement&>("last movement")->Get();

    WorldPacket& p = event.getPacket();
	if (!p.empty() && p.GetOpcode() == CMSG_MOVE_SPLINE_DONE)
    {
        WorldPackets::Movement::MoveSplineDone splineDone(WorldPacket(p));
        splineDone.Read();
        bot->GetSession()->HandleMoveSplineDoneOpcode(splineDone);
        movement.taxiNodes.clear();
        movement.Set(NULL);
        return true;
    }

    list<ObjectGuid> units = *context->GetValue<list<ObjectGuid> >("nearest npcs");
    for (list<ObjectGuid>::iterator i = units.begin(); i != units.end(); i++)
    {
        Creature *npc = bot->GetNPCIfCanInteractWith(*i, UNIT_NPC_FLAG_FLIGHTMASTER, UNIT_NPC_FLAG_2_NONE);
        if (!npc)
            continue;

        if (movement.taxiNodes.empty())
        {
            ostringstream out;
            out << "I will order the taxi from " << npc->GetName() << ". Please start flying, then instruct me again";
            ai->TellMaster(out);
            return true;
        }

        if (!bot->ActivateTaxiPathTo(movement.taxiNodes, npc))
        {
            ai->TellMaster("I can't fly with you");
            return false;
        }

        return true;
    }

    ai->TellMaster("Cannot find any flightmaster to talk");
    return false;
}
