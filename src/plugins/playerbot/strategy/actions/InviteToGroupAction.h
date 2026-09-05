#pragma once

#include "../Action.h"
#include "Server/Packets/PartyPackets.h"

namespace ai
{
    class InviteToGroupAction : public Action
    {
    public:
        InviteToGroupAction(PlayerbotAI* ai) : Action(ai, "invite") {}

        virtual bool Execute(Event event)
        {
            Player* master = event.getOwner();
            if (!master)
                return false;

            WorldPackets::Party::PartyInviteClient invite(WorldPacket(CMSG_PARTY_INVITE));
            invite.TargetName = master->GetName();
            invite.TargetGUID = master->GetGUID();
            bot->GetSession()->HandlePartyInviteOpcode(invite);

            return true;
        }
    };

}
