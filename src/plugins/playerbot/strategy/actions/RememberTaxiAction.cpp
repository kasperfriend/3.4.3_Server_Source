#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/RememberTaxiAction.h"
#include "../values/LastMovementValue.h"

using namespace ai;

bool RememberTaxiAction::Execute(Event event)
{
    WorldPacket const& source = event.getPacket();
    if (source.GetOpcode() != CMSG_ACTIVATE_TAXI)
        return false;

    WorldPackets::Taxi::ActivateTaxi packet{WorldPacket(source)};
    packet.Read();

    LastMovement& movement = context->GetValue<LastMovement&>("last movement")->Get();
    movement.taxiMaster = packet.Vendor;
    movement.taxiNodes.clear();
    movement.taxiNodes.push_back(packet.Node);
    return true;
}
