#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/TrainerAction.h"

using namespace ai;

void TrainerAction::Learn(uint32 cost, Trainer::Spell const& tSpell, ostringstream& msg)
{
    if (bot->GetMoney() < cost)
        return;

    bot->ModifyMoney(-int64(cost));

    if (tSpell.IsCastable())
        bot->CastSpell(bot, tSpell.SpellId, true);
    else
        bot->LearnSpell(tSpell.SpellId, false);

    msg << " - learned";
}

void TrainerAction::List(Creature* creature, TrainerSpellAction action, SpellIds& spells)
{
    TellHeader(creature);

    Trainer::Trainer const* trainer = sObjectMgr->GetTrainer(creature->GetEntry());
    if (!trainer)
        return;

    float fDiscountMod = bot->GetReputationPriceDiscount(creature);
    uint32 totalCost = 0;

    for (Trainer::Spell const& tSpell : trainer->GetSpells())
    {
        if (!tSpell.SpellId)
            continue;

        if (!bot->IsSpellFitByClassAndRace(tSpell.SpellId))
            continue;

        if (trainer->GetSpellStateForPlayer(bot, tSpell) != Trainer::SpellState::Available)
            continue;

        const SpellInfo* const pSpellInfo = sSpellMgr->GetSpellInfo(tSpell.SpellId, DIFFICULTY_NONE);
        if (!pSpellInfo)
            continue;

        uint32 cost = uint32(floor(tSpell.MoneyCost * fDiscountMod));
        totalCost += cost;

        ostringstream out;
        out << chat->formatSpell(pSpellInfo) << chat->formatMoney(cost);

        if (action && (spells.empty() || spells.find(tSpell.SpellId) != spells.end()))
            (this->*action)(cost, tSpell, out);

        ai->TellMaster(out);
    }

    TellFooter(totalCost);
}


bool TrainerAction::Execute(Event event)
{
    string text = event.getParam();

    Player* master = GetMaster();
    if (!master)
        return false;

    Unit* target = master->GetSelectedUnit();
    if (!target)
        return false;

    Creature *creature = ai->GetCreature(target->GetGUID());
    if (!creature)
        return false;

    // check present spell in trainer spell list
    Trainer::Trainer const* cSpells = sObjectMgr->GetTrainer(creature->GetEntry());
    if (!cSpells)
    {
        ai->TellMaster("No spells can be learned from this trainer");
        return false;
    }

    uint32 spell = chat->parseSpell(text);
    SpellIds spells;
    if (spell)
        spells.insert(spell);

    if (text == "learn")
        List(creature, &TrainerAction::Learn, spells);
    else
        List(creature, NULL, spells);

    return true;
}

void TrainerAction::TellHeader(Creature* creature)
{
    ostringstream out; out << "--- can learn from " << creature->GetName() << " ---";
    ai->TellMaster(out);
}

void TrainerAction::TellFooter(uint32 totalCost)
{
    if (totalCost)
    {
        ostringstream out; out << "Total cost: " << chat->formatMoney(totalCost);
        ai->TellMaster(out);
    }
}
