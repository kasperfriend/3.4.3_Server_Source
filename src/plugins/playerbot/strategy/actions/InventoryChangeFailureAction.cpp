#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/InventoryChangeFailureAction.h"


using namespace ai;

bool InventoryChangeFailureAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);
    // SMSG_INVENTORY_CHANGE_FAILURE leads with int32 BagResult
    // (InventoryChangeFailure::Write); the InventoryResult enum is uint8, so a
    // uint8 read only worked by accident via the little-endian low byte.
    int32 err;
    p >> err;
    if (err == EQUIP_ERR_OK)
        return false;

    switch (err)
    {
    case EQUIP_ERR_ITEM_MAX_COUNT:
        ai->TellMaster("I can't carry anymore of those.");
        break;
    case EQUIP_ERR_SPELL_FAILED_REAGENTS_GENERIC:
        ai->TellMaster("I'm missing some reagents for that.");
        break;
    case EQUIP_ERR_ITEM_LOCKED:
        ai->TellMaster("That item is locked.");
        break;
    case EQUIP_ERR_LOOT_GONE:
        break;
    case EQUIP_ERR_INV_FULL:
        ai->TellMaster("My inventory is full.");
        break;
    case EQUIP_ERR_NOT_IN_COMBAT:
        ai->TellMaster("I can't use that in combat.");
        break;
    case EQUIP_ERR_LOOT_CANT_LOOT_THAT_NOW:
        ai->TellMaster("I can't get that now.");
        break;
    case EQUIP_ERR_ITEM_UNIQUE_EQUIPPABLE:
        ai->TellMaster("I can only have one of those equipped.");
        break;
    case EQUIP_ERR_BANK_FULL:
        ai->TellMaster("My bank is full.");
        break;
    case EQUIP_ERR_ITEM_NOT_FOUND:
        ai->TellMaster("I can't find the item.");
        break;
    case EQUIP_ERR_NO_BANK_HERE:
        ai->TellMaster("I'm too far from the bank.");
        break;
    default:
        ai->TellMaster("I can't use that.");
    }
    return true;
}


bool BuyFailedAction::Execute(Event event)
{
    if (event.getPacket().GetOpcode() != SMSG_BUY_FAILED)
        return false;
    WorldPacket data(event.getPacket());
    data.rpos(0);
    data.ResetBitPos();
    ObjectGuid vendor;
    uint32 itemId;
    uint8 reason;
    data >> vendor >> itemId >> reason;
    if (data.rpos() != data.size())
        return false;
    if (reason == BUY_ERR_NOT_ENOUGHT_MONEY)
        ai->TellMaster("Not enough money");
    else if (reason == BUY_ERR_REPUTATION_REQUIRE)
        ai->TellMaster("Not enough reputation");
    else
        ai->TellMaster("I couldn't buy that item");
    return true;
}
