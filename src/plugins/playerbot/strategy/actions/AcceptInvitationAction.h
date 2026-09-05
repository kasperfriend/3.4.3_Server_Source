#pragma once

#include "../Action.h"
#include "Globals/ObjectMgr.h"
#include "Server/Packets/PartyPackets.h"

namespace ai
{
    class AcceptInvitationAction : public Action {
    public:
        AcceptInvitationAction(PlayerbotAI* ai) : Action(ai, "accept invitation") {}

        virtual bool Execute(Event event)
        {
            Player* master = GetMaster();

            Group* grp = bot->GetGroupInvite();
            if (!grp)
                return false;

            Player* inviter = ObjectAccessor::FindPlayer(grp->GetLeaderGUID());
            if (!inviter)
                return false;

            if (!ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, inviter))
            {
                WorldPackets::Party::PartyInviteResponse decline{WorldPacket(CMSG_PARTY_INVITE_RESPONSE)};
                decline.Accept = false;
                bot->GetSession()->HandlePartyInviteResponseOpcode(decline);
                return false;
            }

            WorldPackets::Party::PartyInviteResponse response{WorldPacket(CMSG_PARTY_INVITE_RESPONSE)};
            response.Accept = true;
            bot->GetSession()->HandlePartyInviteResponseOpcode(response);

            if (sRandomPlayerbotMgr.IsRandomBot(bot))
                bot->GetPlayerbotAI()->SetMaster(inviter);

            ai->ResetStrategies();
            ai->TellMaster("Hello");
            return true;
        }
    };

}
