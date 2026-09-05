#pragma once

#include "../Action.h"
#include "Server/Packets/PartyPackets.h"

namespace ai
{
    class PassLeadershipToMasterAction : public Action {
    public:
        PassLeadershipToMasterAction(PlayerbotAI* ai) : Action(ai, "leader") {}

        virtual bool Execute(Event event)
        {
            Player* master = GetMaster();
            if (master && bot->GetGroup() && bot->GetGroup()->IsMember(master->GetGUID()))
            {
                WorldPackets::Party::SetPartyLeader setLeader{WorldPacket(CMSG_SET_PARTY_LEADER)};
                setLeader.TargetGUID = master->GetGUID();
                bot->GetSession()->HandleSetPartyLeaderOpcode(setLeader);
                return true;
            }

            return false;
        }
    };

}
