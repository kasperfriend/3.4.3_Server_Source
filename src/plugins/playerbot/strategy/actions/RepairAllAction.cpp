#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/RepairAllAction.h"


using namespace ai;

bool RepairAllAction::Execute(Event event)
{
    list<ObjectGuid> npcs = AI_VALUE(list<ObjectGuid>, "nearest npcs");
    for (list<ObjectGuid>::iterator i = npcs.begin(); i != npcs.end(); i++)
    {
        Creature *unit = bot->GetNPCIfCanInteractWith(*i, UNIT_NPC_FLAG_REPAIR, UNIT_NPC_FLAG_2_NONE);
        if (!unit)
            continue;

        bot->SetFacingToObject(unit);
        float discountMod = bot->GetReputationPriceDiscount(unit);
        bot->DurabilityRepairAll(true, discountMod, false);

        ostringstream out;
        out << "Repair: " << unit->GetName();
        ai->TellMasterNoFacing(out.str());

        return true;
    }

    ai->TellMaster("Cannot find any npc to repair at");
    return false;
}
