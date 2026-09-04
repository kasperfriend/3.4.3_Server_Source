#pragma once
#include "../Value.h"
#include "strategy/values/TargetValue.h"

namespace ai
{
   
    class SpellCastUsefulValue : public BoolCalculatedValue, public Qualified
	{
	public:
        SpellCastUsefulValue(PlayerbotAI* ai) : BoolCalculatedValue(ai) {}
      
    public:
        virtual bool Calculate();

    };
}
