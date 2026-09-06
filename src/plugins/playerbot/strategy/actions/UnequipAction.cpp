#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/UnequipAction.h"

#include "../values/ItemCountValue.h"

using namespace ai;

bool UnequipAction::Execute(Event event)
{
    string text = event.getParam();

    ItemIds ids = chat->parseItems(text);
    for (ItemIds::iterator i =ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        UnequipItem(&visitor);
    }

    return true;
}


void UnequipAction::UnequipItem(FindItemVisitor* visitor)
{
    IterateItems(visitor, ITERATE_ALL_ITEMS);
    list<Item*> items = visitor->GetResult();
	if (!items.empty()) UnequipItem(**items.begin());
}

void UnequipAction::UnequipItem(Item& item)
{
    uint8 bagIndex = item.GetBagSlot();
    uint8 slot = item.GetSlot();
    uint8 dstBag = NULL_BAG;


    WorldPacket* const packet = new WorldPacket(CMSG_AUTO_STORE_BAG_ITEM, 6);
    // 3.4.3 AutoStoreBagItem::Read expects an InvUpdate bit field first, then
    // ContainerSlotA (source bag), ContainerSlotB (destination bag) and SlotA
    // (source slot). The classic (bag, slot, dstBag) order made the server read
    // the source bag as the bit field and throw on the truncated tail, so the
    // item was never unequipped. The handler requires an empty InvUpdate.
    packet->WriteBits(0, 2);
    packet->FlushBits();
    *packet << bagIndex;   // ContainerSlotA: source bag
    *packet << dstBag;     // ContainerSlotB: destination bag (NULL_BAG = auto)
    *packet << slot;       // SlotA: source slot
    bot->GetSession()->QueuePacket(packet);

    ostringstream out; out << chat->formatItem(item.GetTemplate()) << " unequipped";
    ai->TellMaster(out);
}

