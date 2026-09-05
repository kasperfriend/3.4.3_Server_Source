#pragma once

#include "../Action.h"

namespace ai
{
	class TrainerAction : public Action {
	public:
		TrainerAction(PlayerbotAI* ai) : Action(ai, "trainer") {}

    public:
        virtual bool Execute(Event event);

    private:
        typedef void (TrainerAction::*TrainerSpellAction)(uint32, Trainer::Spell const&, ostringstream& msg);
        void List(Creature* creature, TrainerSpellAction action, SpellIds& spells);
        void Learn(uint32 cost, Trainer::Spell const& tSpell, ostringstream& msg);
        void TellHeader(Creature* creature);
        void TellFooter(uint32 totalCost);
    };

}