#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/TeleportAction.h"
#include "../values/LastMovementValue.h"

using namespace ai;

bool TeleportAction::Execute(Event event)
{
    list<ObjectGuid> gos = *context->GetValue<list<ObjectGuid> >("nearest game objects");
    for (list<ObjectGuid>::iterator i = gos.begin(); i != gos.end(); i++)
    {
        GameObject* go = ai->GetGameObject(*i);
        if (!go)
            continue;

        GameObjectTemplate const *goInfo = go->GetGOInfo();
        if (goInfo->type != GAMEOBJECT_TYPE_SPELLCASTER)
            continue;

        uint32 spellId = goInfo->spellCaster.spell;
        const SpellInfo* const pSpellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (pSpellInfo->GetEffect(SpellEffIndex(0)).Effect != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->GetEffect(SpellEffIndex(1)).Effect != SPELL_EFFECT_TELEPORT_UNITS && pSpellInfo->GetEffect(SpellEffIndex(2)).Effect != SPELL_EFFECT_TELEPORT_UNITS)
            continue;

        ostringstream out; out << "Teleporting using " << goInfo->name;
        ai->TellMasterNoFacing(out.str());

        ai->ChangeStrategy("-follow,+stay", BOT_STATE_NON_COMBAT);

        Spell *spell = new Spell(bot, pSpellInfo, TRIGGERED_NONE);
        SpellCastTargets targets;
        targets.SetUnitTarget(bot);
        spell->prepare(targets);
        spell->cast(true);
        return true;
    }


    LastMovement& movement = context->GetValue<LastMovement&>("last movement")->Get();
    if (movement.lastAreaTrigger)
    {
        WorldPackets::AreaTrigger::AreaTrigger areaTrigger{WorldPacket(CMSG_AREA_TRIGGER)};
        areaTrigger.AreaTriggerID = int32(movement.lastAreaTrigger);
        areaTrigger.Entered = true;
        areaTrigger.FromClient = true;

        bot->GetSession()->HandleAreaTriggerOpcode(areaTrigger);
        movement.lastAreaTrigger = 0;
        return true;
    }

    ai->TellMaster("Cannot find any portal to teleport");
    return false;
}
