#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/TellCastFailedAction.h"


using namespace ai;

bool TellCastFailedAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);

    // SMSG_CAST_FAILED in 3.4.3 (WorldPackets::Spell::CastFailed::Write) starts
    // with the cast id as an ObjectGuid, then spell id / visual / reason as
    // int32s. The classic 3.3.5 header this action used to read (cast count u8,
    // spell id u32, reason u8) mis-parsed the modern packet into a garbage
    // spell id and reason; the garbage id could resolve to no SpellInfo and the
    // report then dereferenced null in formatSpell/Spell. Read the modern
    // layout instead and bail out when the spell cannot be resolved.
    p.read_skip<ObjectGuid>();      // CastID
    int32 spellId = 0;
    p >> spellId;
    p.read_skip<int32>();           // SpellCastVisual
    int32 result = 0;
    p >> result;                    // SpellCastResult
    p.read_skip<int32>();           // FailedArg1
    p.read_skip<int32>();           // FailedArg2

    ai->SpellInterrupted(spellId > 0 ? uint32(spellId) : 0);

    if (result == SPELL_CAST_OK || spellId <= 0)
        return false;

    const SpellInfo *const pSpellInfo =  sSpellMgr->GetSpellInfo(uint32(spellId), DIFFICULTY_NONE);
    if (!pSpellInfo)
        return true;

    ostringstream out; out << chat->formatSpell(pSpellInfo) << ": ";
    switch (result)
    {
    case SPELL_FAILED_NOT_READY:
        out << "not ready";
        break;
    case SPELL_FAILED_REQUIRES_SPELL_FOCUS:
        out << "requires spell focus";
        break;
    case SPELL_FAILED_REQUIRES_AREA:
        out << "cannot cast here";
        break;
    case SPELL_FAILED_TOTEMS:
    case SPELL_FAILED_TOTEM_CATEGORY:
        out << "requires totem";
        break;
    case SPELL_FAILED_EQUIPPED_ITEM_CLASS:
        out << "requires item";
        break;
    case SPELL_FAILED_EQUIPPED_ITEM_CLASS_MAINHAND:
    case SPELL_FAILED_EQUIPPED_ITEM_CLASS_OFFHAND:
        out << "requires weapon";
        break;
    case SPELL_FAILED_PREVENTED_BY_MECHANIC:
        out << "interrupted";
        break;
    default:
        out << "cannot cast";
    }
    Spell *spell = new Spell(bot, pSpellInfo, TRIGGERED_NONE);
    int32 castTime = spell->GetCastTime();
    delete spell;

    if (castTime >= 2000)
        ai->TellMasterNoFacing(out.str());

    return true;
}


bool TellSpellAction::Execute(Event event)
{
    string spell = event.getParam();
    uint32 spellId = AI_VALUE2(uint32, "spell id", spell);
    if (!spellId)
        return false;

    SpellInfo const *spellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
    if (!spellInfo)
        return false;

    ostringstream out; out << chat->formatSpell(spellInfo);
    ai->TellMaster(out);
    return true;
}
