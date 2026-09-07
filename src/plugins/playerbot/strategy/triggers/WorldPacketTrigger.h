#pragma once

#include "../Trigger.h"
#include "Log.h"
#include <deque>

namespace ai
{
    class WorldPacketTrigger : public Trigger {
    public:
        WorldPacketTrigger(PlayerbotAI* ai, string command) : Trigger(ai, command), checked(false) {}

        virtual void ExternalEvent(WorldPacket &packet, Player* owner = NULL)
        {
            if (pending.size() >= 256)
            {
                TC_LOG_WARN("playerbot", "Bot event queue '{}' full; discarding oldest event", getName());
                pending.pop_front();
                checked = false; // a previously checked front is already consumed
            }
            pending.emplace_back(getName(), packet, owner);
        }

        virtual Event Check()
        {
            if (pending.empty() || checked)
                return Event();
            checked = true;
            return pending.front();
        }

        virtual void Reset()
        {
            if (checked && !pending.empty())
                pending.pop_front();
            checked = false;
        }

    private:
        std::deque<Event> pending;
        bool checked;
    };
}
