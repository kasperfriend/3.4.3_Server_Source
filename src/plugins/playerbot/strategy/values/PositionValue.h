#pragma once
#include "../Value.h"

namespace ai
{
    // NOTE: deliberately *not* called "Position".  The core has a global
    // ::Position class, and every bot translation unit pulls in both
    // `using namespace ai;` and the core headers, which made the conversion
    // operator in Entities/Object/Position.h ambiguous:
    //   Position.h(232,24): error C2872: 'Position': ambiguous symbol
    //     could be 'Position' / or 'ai::Position'
    class BotPosition
    {
    public:
        BotPosition() : valueSet(false) {}
        void Set(double x, double y, double z) { this->x = x; this->y = y; this->z = z; this->valueSet = true; }
        void Reset() { valueSet = false; }
        bool isSet() { return valueSet; }

        double x, y, z;
        bool valueSet;
    };

    class PositionValue : public ManualSetValue<BotPosition&>, public Qualified
	{
	public:
        PositionValue(PlayerbotAI* ai);

	private:
        BotPosition position;
    };
}
