#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/ReadyCheckAction.h"
#include "../../PlayerbotAIConfig.h"
#include "../../PlayerbotPackets.h"
#include "PartyPackets.h"
#include "Entities/Pet/Pet.h"

using namespace ai;

bool ReadyCheckAction::Execute(Event event)
{
    Group* group = nullptr;
    if (!event.getPacket().empty())
    {
        packets::ReadyCheck request;
        if (event.getPacket().GetOpcode() != SMSG_READY_CHECK_STARTED || !packets::ReadReadyCheck(event.getPacket(), request) || request.PartyIndex < 0)
            return false;
        group = bot->GetGroup(uint8(request.PartyIndex));
        if (!group || group->GetGUID() != request.Party || request.Initiator == bot->GetGUID())
            return false;
    }
    else
        group = bot->GetGroup();

    if (!group || !group->IsReadyCheckStarted())
        return false;

    // Answer the current check, including an explicit NOT READY response.
    // Filled typed packets avoid inventing a client wire layout or starting a
    // second ready check. The core handler performs the group-state update.
    WorldPackets::Party::ReadyCheckResponseClient response{WorldPacket(CMSG_READY_CHECK_RESPONSE)};
    response.PartyIndex = group->GetGroupCategory();
    response.IsReady = bot->IsAlive() && ReadyCheck();
    bot->GetSession()->HandleReadyCheckResponseOpcode(response);
    ai->ChangeStrategy("-ready check", BOT_STATE_NON_COMBAT);
    return true;
}

bool ReadyCheckAction::ReadyCheck()
{
    bool health = AI_VALUE2(uint8, "health", "self target") > sPlayerbotAIConfig.almostFullHealth;
    if (!health)
    {
        ai->TellMaster("Low health!");
        return false;
    }

    bool mana = !AI_VALUE2(bool, "has mana", "self target") || AI_VALUE2(uint8, "mana", "self target") > sPlayerbotAIConfig.mediumMana;
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

    return true;
}

bool FinishReadyCheckAction::Execute(Event event)
{
    packets::ReadyCheck completed;
    if (event.getPacket().GetOpcode() != SMSG_READY_CHECK_COMPLETED || !packets::ReadReadyCheck(event.getPacket(), completed))
        return false;
    Group* group = bot->GetGroup();
    if (!group || group->GetGUID() != completed.Party)
        return false;
    ai->ChangeStrategy("-ready check", BOT_STATE_NON_COMBAT);
    return true;
}
