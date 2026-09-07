#pragma once

#include "../Action.h"
#include "../../RandomPlayerbotMgr.h"
#include "Server/Packets/PartyPackets.h"
#include "Groups/Group.h"

namespace ai
{
    class LeaveGroupAction : public Action {
    public:
        LeaveGroupAction(PlayerbotAI* ai, string name = "leave") : Action(ai, name) {}

        virtual bool Execute(Event event)
        {
            if (!bot->GetGroup())
                return false;

            ai->TellMaster("Goodbye!", PLAYERBOT_SECURITY_TALK);

            WorldPackets::Party::LeaveGroup leave{WorldPacket(CMSG_LEAVE_GROUP)};
            bot->GetSession()->HandleLeaveGroupOpcode(leave);

            if (sRandomPlayerbotMgr.IsRandomBot(bot))
            {
                bot->GetPlayerbotAI()->SetMaster(NULL);
                sRandomPlayerbotMgr.ScheduleTeleport(bot->GetGUID().GetCounter());
                sRandomPlayerbotMgr.SetLootAmount(bot, 0);
            }

            ai->ResetStrategies();
            return true;
        }
    };

    class PartyCommandAction : public LeaveGroupAction {
    public:
        PartyCommandAction(PlayerbotAI* ai) : LeaveGroupAction(ai, "party command") {}

        virtual bool Execute(Event event)
        {
            // This fires for SMSG_PARTY_COMMAND_RESULT sent to the bot or snooped
            // from its master. The 3.4.3 packet is bit-packed (name/command/result
            // widths interleaved with byte fields), so the classic 3.3.5 parse
            // (u32 operation + name string) reads garbage and can spuriously match
            // PARTY_OP_LEAVE. What the action actually needs is group state: when
            // the master left or was removed, the bots should leave with him.
            Player* master = GetMaster();
            if (!master)
                return false;

            Group* group = bot->GetGroup();
            if (!group)
                return false;

            for (GroupReference* ref = group->GetFirstMember(); ref; ref = ref->next())
            {
                if (ref->GetSource() == master)
                    return false;               // master is still in the group
            }

            return LeaveGroupAction::Execute(event);
        }
    };

    class UninviteAction : public LeaveGroupAction {
    public:
        UninviteAction(PlayerbotAI* ai) : LeaveGroupAction(ai, "uninvite") {}

        virtual bool Execute(Event event)
        {
            // The event carries the master's raw CMSG_PARTY_UNINVITE
            // (WorldPackets::Party::PartyUninvite): a hasPartyIndex bit and an
            // 8-bit reason length precede the target guid, so the guid is not the
            // first 8 bytes anymore. Mirror the core reader for the fields we need.
            WorldPacket& p = event.getPacket();
            p.rpos(0);
            p.ReadBit();                        // hasPartyIndex
            p.ReadBits(8);                      // reason length
            p.ResetBitPos();

            ObjectGuid guid;
            p >> guid;                          // TargetGUID

            if (bot->GetGUID() == guid)
                return LeaveGroupAction::Execute(event);

            return false;
        }
    };

}
