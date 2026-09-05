#pragma once
#include "../Trigger.h"
#include "../values/LastMovementValue.h"

namespace ai
{
    class WithinAreaTrigger : public Trigger {
    public:
        WithinAreaTrigger(PlayerbotAI* ai) : Trigger(ai, "within area trigger") {}

        virtual bool IsActive()
		{


            LastMovement& movement = context->GetValue<LastMovement&>("last movement")->Get();
            if (!movement.lastAreaTrigger)
                return false;

            AreaTriggerEntry const* atEntry = sAreaTriggerStore.LookupEntry(movement.lastAreaTrigger);
            if(!atEntry)
                return false;

            AreaTrigger const* at = sObjectMgr->GetAreaTrigger(movement.lastAreaTrigger);
            if (!at)
                return false;

            return IsPointInAreaTriggerZone(atEntry, bot->GetMapId(), bot->GetPositionX(), bot->GetPositionY(), bot->GetPositionZ(), 0.5f);
        }

    private:
        bool IsPointInAreaTriggerZone(AreaTriggerEntry const* atEntry, uint32 mapid, float x, float y, float z, float delta)
        {
            if (mapid != atEntry->mapid)
                return false;

            if (atEntry->radius > 0)
            {
                // if we have radius check it
                float dist2 = (x - atEntry->Pos.X) * (x - atEntry->Pos.X) + (y - atEntry->Pos.Y) * (y - atEntry->Pos.Y) + (z - atEntry->Pos.Z) * (z - atEntry->Pos.Z);
                if (dist2 > (atEntry->radius + delta) * (atEntry->radius + delta))
                    return false;
            }
            else
            {
                // we have only extent

                // rotate the players position instead of rotating the whole cube, that way we can make a simplified
                // is-in-cube check and we have to calculate only one point instead of 4

                // 2PI = 360, keep in mind that ingame orientation is counter-clockwise
                double rotation = 2 * M_PI - atEntry->box_orientation;
                double sinVal = sin(rotation);
                double cosVal = cos(rotation);

                float playerBoxDistX = x - atEntry->Pos.X;
                float playerBoxDistY = y - atEntry->Pos.Y;

                float rotPlayerX = float(atEntry->Pos.X + playerBoxDistX * cosVal - playerBoxDistY * sinVal);
                float rotPlayerY = float(atEntry->Pos.Y + playerBoxDistY * cosVal + playerBoxDistX * sinVal);

                // box edges are parallel to coordiante axis, so we can treat every dimension independently :D
                float dz = z - atEntry->Pos.Z;
                float dx = rotPlayerX - atEntry->Pos.X;
                float dy = rotPlayerY - atEntry->Pos.Y;
                if ((fabs(dx) > atEntry->box_x / 2 + delta) ||
                        (fabs(dy) > atEntry->box_y / 2 + delta) ||
                        (fabs(dz) > atEntry->box_z / 2 + delta))
                {
                    return false;
                }
            }

            return true;
        }
    };
}