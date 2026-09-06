#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/EquipAction.h"

#include "../values/ItemCountValue.h"

using namespace ai;

bool EquipAction::Execute(Event event)
{
    string text = event.getParam();
    if (text == "?")
    {
        TellEquipmentSets();
        return true;
    }

    if (UseEquipmentSet(text))
        return true;

    ItemIds ids = chat->parseItems(text);

    for (ItemIds::iterator i =ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        EquipItem(&visitor);
    }

    return true;
}

bool EquipAction::UseEquipmentSet(string& name)
{
    for (auto const& itr : bot->GetEquipmentSets())
    {
        EquipmentSetInfo const& eqSet = itr.second;
        if (eqSet.State == EQUIPMENT_SET_DELETED || eqSet.Data.SetName != name)
            continue;

        UseEquipmentSet(eqSet.Data);

        ostringstream out; out << name << " set equipped";
        ai->TellMaster(out);
        return true;
    }
    return false;
}

bool EquipAction::UseEquipmentSet(EquipmentSetInfo::EquipmentSetData const& set)
{
    WorldPackets::EquipmentSet::UseEquipmentSet useSet{WorldPacket(CMSG_USE_EQUIPMENT_SET)};
    useSet.GUID = set.Guid;
    for (uint8 slot = 0; slot < EQUIPMENT_SET_SLOTS; ++slot)
    {
        useSet.Items[slot].Item = set.Pieces[slot];
        useSet.Items[slot].ContainerSlot = NULL_BAG;
        useSet.Items[slot].Slot = slot;
    }

    bot->GetSession()->HandleUseEquipmentSet(useSet);
    return true;
}

void EquipAction::TellEquipmentSets()
{
    ai->TellMaster("=== Equipment sets ===");
    for (auto const& itr : bot->GetEquipmentSets())
    {
        if (itr.second.State != EQUIPMENT_SET_DELETED)
            ai->TellMaster(itr.second.Data.SetName);
    }
}

void EquipAction::EquipItem(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    list<Item*> items = visitor->GetResult();
	if (!items.empty()) EquipItem(**items.begin());
}


void EquipAction::EquipItem(Item& item)
{
    uint8 bagIndex = item.GetBagSlot();
    uint8 slot = item.GetSlot();
    uint32 itemId = item.GetTemplate()->GetId();

    if (item.GetTemplate()->GetInventoryType() == INVTYPE_AMMO)
    {
        bot->SetAmmo(itemId);
    }
    else
    {
        WorldPacket* const packet = new WorldPacket(CMSG_AUTO_EQUIP_ITEM, 6);
        // 3.4.3 AutoEquipItem::Read reads an InvUpdate first (2-bit entry count,
        // then container/slot pairs) and the handler rejects anything but exactly
        // one entry, then PackSlot/Slot. The classic 2-byte (bag, slot) payload
        // made the server read the source bag as the bit field and run off the
        // end, so the item was never equipped.
        packet->WriteBits(1, 2);
        packet->FlushBits();
        *packet << bagIndex;   // InvUpdate entry: source container
        *packet << slot;       // InvUpdate entry: source slot
        *packet << bagIndex;   // PackSlot
        *packet << slot;       // Slot
        bot->GetSession()->QueuePacket(packet);
    }

    ostringstream out; out << "equipping " << chat->formatItem(item.GetTemplate());
    ai->TellMaster(out);
}
