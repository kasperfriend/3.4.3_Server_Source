#pragma once

#include "../Action.h"

namespace ai
{
    class AcceptDuelAction : public Action
    {
    public:
        AcceptDuelAction(PlayerbotAI* ai) : Action(ai, "accept duel")
        {}

        virtual bool Execute(Event event)
        {
            WorldPacket p(event.getPacket());

            ObjectGuid flagGuid;
            p >> flagGuid;
            ObjectGuid playerGuid;
            p >> playerGuid;

            // CMSG_DUEL_RESPONSE in 3.4.3 (WorldPackets::Duel::DuelResponse) carries
            // the arbiter guid plus Accepted/Forfeited bits; the old guid-only
            // packet truncated the read and the bot could never answer a duel.
            WorldPacket* const packet = new WorldPacket(CMSG_DUEL_RESPONSE, 9);
            *packet << flagGuid;
            packet->WriteBit(true);                     // Accepted
            packet->WriteBit(false);                    // Forfeited
            packet->FlushBits();
            bot->GetSession()->QueuePacket(packet);

            ai->ResetStrategies();
            return true;
        }
    };

}
