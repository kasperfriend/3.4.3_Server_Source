#pragma once

#include "../Action.h"

namespace ai
{
    class AcceptResurrectAction : public Action {
    public:
        AcceptResurrectAction(PlayerbotAI* ai) : Action(ai, "accept resurrect") {}

        virtual bool Execute(Event event)
        {
            if (bot->IsAlive())
                return false;

            WorldPacket p(event.getPacket());
            p.rpos(0);
            ObjectGuid guid;
            p >> guid;

            // CMSG_RESURRECT_RESPONSE in 3.4.3 (WorldPackets::Misc::ResurrectResponse):
            // resurrecter guid + uint32 response where 0 = accept (1 = decline,
            // 2 = timeout). The old single accept byte made the bot decline or
            // truncate the read.
            WorldPacket* const packet = new WorldPacket(CMSG_RESURRECT_RESPONSE, 8+4);
            *packet << guid;
            *packet << uint32(0);                       // accept
            bot->GetSession()->QueuePacket(packet);   // queue the packet to get around race condition

            ai->ChangeEngine(BOT_STATE_NON_COMBAT);
            return true;
        }
    };

}
