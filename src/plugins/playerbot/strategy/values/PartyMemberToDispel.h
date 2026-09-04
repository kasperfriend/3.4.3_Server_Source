#pragma once
#include "../Value.h"
#include "strategy/values/PartyMemberValue.h"

namespace ai
{
    class PartyMemberToDispel : public PartyMemberValue, Qualified
	{
	public:
        PartyMemberToDispel(PlayerbotAI* ai) : 
          PartyMemberValue(ai) {}
    
    protected:
        virtual Unit* Calculate();
	};
}
