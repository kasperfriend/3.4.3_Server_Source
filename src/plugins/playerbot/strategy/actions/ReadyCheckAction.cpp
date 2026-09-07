#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/ReadyCheckAction.h"
#include "../../PlayerbotAIConfig.h"
#include "Entities/Pet/Pet.h"

using namespace ai;

bool ReadyCheckAction::Execute(Event event)
{
    WorldPacket &p = event.getPacket();
	ObjectGuid player;
	p.rpos(0);
    if (!p.empty())
        p >> player;

	if (player == bot->GetGUID())
        return false;

	return ReadyCheck();
}

bool ReadyCheckAction::ReadyCheck()
{
    bool health = AI_VALUE2(uint8, "health", "self target") > sPlayerbotAIConfig.almostFullHealth;
    if (!health)
    {
        ai->TellMaster("Low health!");
        return false;
    }

    bool mana = !AI_VALUE2(bool, "has mana", "self target") || AI_VALUE2(uint8, "mana", "self target") > sPlayerbotAIConfig.mediumHealth;
    if (!mana)
    {
        ai->TellMaster("Low mana!");
        return false;
    }

    Player* master = GetMaster();
    if (master)
    {
        bool distance = bot->GetDistance(master) <= sPlayerbotAIConfig.sightDistance;
        if (!distance)
        {
            ai->TellMaster("Too far away!");
            return false;
        }
    }

    if (bot->GetClass() == CLASS_HUNTER)
    {


        if (!bot->GetPet())
        {
            ai->TellMaster("No pet!");
            return false;
        }

        if (bot->GetPet()->GetHappinessState() == UNHAPPY)
        {
            ai->TellMaster("Pet is unhappy!");
            return false;
        }
    }

    // CMSG_DO_READY_CHECK in 3.4.3 (WorldPackets::Party::DoReadyCheck::Read)
    // carries only an optional party-index bit; the handler starts the check on
    // the current party when the index is absent. The old payload (packed guid
    // followed by a u8) made the reader take the first guid byte as the
    // "has index" bit and, when it was set, read the next guid byte as a
    // garbage subgroup index - so the check either never started or ran against
    // the wrong subgroup. Send the absent-index (whole current party) form.
    WorldPacket* const packet = new WorldPacket(CMSG_DO_READY_CHECK, 1);
    packet->WriteBit(false);                    // no subgroup index
    packet->FlushBits();
    bot->GetSession()->QueuePacket(packet);

    ai->ChangeStrategy("-ready check", BOT_STATE_NON_COMBAT);

    return true;
}

bool FinishReadyCheckAction::Execute(Event event)
{
    return ReadyCheck();
}
