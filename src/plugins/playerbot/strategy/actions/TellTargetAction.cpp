#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/TellTargetAction.h"


using namespace ai;

bool TellTargetAction::Execute(Event event)
{
    Unit* target = context->GetValue<Unit*>("current target")->Get();
    if (target)
    {
        ostringstream out;
		out << "Attacking " << target->GetName();
        ai->TellMaster(out);

        context->GetValue<Unit*>("old target")->Set(target);
    }
    return true;
}

bool TellAttackersAction::Execute(Event event)
{
    ai->TellMaster("--- Attackers ---");

    list<ObjectGuid> attackers = context->GetValue<list<ObjectGuid> >("attackers")->Get();
    for (list<ObjectGuid>::iterator i = attackers.begin(); i != attackers.end(); i++)
    {
        Unit* unit = ai->GetUnit(*i);
        if (!unit || !unit->IsAlive())
            continue;

        ai->TellMaster(unit->GetName());
    }

    ai->TellMaster("--- Threat ---");
    for (auto const& pair : bot->GetThreatManager().GetThreatenedByMeList())
    {
        Unit* unit = pair.second->GetOwner();
        if (!unit)
            continue;

        float threat = pair.second->GetThreat();

        ostringstream out; out << unit->GetName() << " (" << threat << ")";
        ai->TellMaster(out);
    }
    return true;
}
