#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/GossipHelloAction.h"


using namespace ai;

bool GossipHelloAction::Execute(Event event)
{
    ObjectGuid guid;

    WorldPacket &p = event.getPacket();
    if (p.empty())
    {
        Player* master = GetMaster();
        if (master && master->GetSelectedUnit())
            guid = master->GetSelectedUnit()->GetGUID();
    }
    else
    {
        p.rpos(0);
        p >> guid;
    }

    if (guid.IsEmpty())
        return false;

    Creature *pCreature = bot->GetNPCIfCanInteractWith(guid, UNIT_NPC_FLAG_NONE, UNIT_NPC_FLAG_2_NONE);
    if (!pCreature)
    {
        TC_LOG_DEBUG("playerbot", "[PlayerbotMgr]: HandleMasterIncomingPacket - Received CMSG_TALK_TO_GOSSIP {} not found or you can't interact with him.", guid.ToString());
        return false;
    }

    if (pCreature->GetCreatureTemplate()->GossipMenuIds.empty())
        return false;

    WorldPackets::NPC::Hello hello{WorldPacket(CMSG_TALK_TO_GOSSIP)};
    hello.Unit = guid;
    bot->GetSession()->HandleGossipHelloOpcode(hello);
    bot->SetFacingToObject(pCreature);

    ostringstream out; out << "--- " << pCreature->GetName() << " ---";
    ai->TellMasterNoFacing(out.str());

    GossipMenu& menu = bot->PlayerTalkClass->GetGossipMenu();
    uint32 i = 0, loops = 0;
    while (i < menu.GetMenuItemCount() && loops++ < 100)
    {
        GossipMenuItem const* item = menu.GetItemByIndex(i);
        if (!item)
            break;

        ai->TellMasterNoFacing(item->OptionText);

        if (item->OptionNpc != GossipOptionNpc::None)
        {
            i++;
            continue;
        }

        WorldPackets::NPC::GossipSelectOption selectOption{WorldPacket(CMSG_GOSSIP_SELECT_OPTION)};
        selectOption.GossipUnit = guid;
        selectOption.GossipID = int32(menu.GetMenuId());
        selectOption.GossipOptionID = item->GossipOptionID;
        bot->GetSession()->HandleGossipSelectOptionOpcode(selectOption);

        i = 0;
    }

    bot->TalkedToCreature(pCreature->GetEntry(), pCreature->GetGUID());
    return true;
}
