#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/StatsAction.h"


using namespace ai;

bool StatsAction::Execute(Event event)
{
    ostringstream out;

    ListGold(out);

    out << ", ";
    ListBagSlots(out);

    out << ", ";
    ListRepairCost(out);

    if (bot->GetXPForNextLevel())
    {
        out << ", ";
        ListXP(out);
    }

    ai->TellMaster(out);
    return true;
}

void StatsAction::ListGold(ostringstream &out)
{
    out << chat->formatMoney(bot->GetMoney());
}

void StatsAction::ListBagSlots(ostringstream &out)
{
    uint32 totalused = 0, total = 16;
    // list out items in main backpack
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
    {
        const Item* const pItem = bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot);
        if (pItem)
            totalused++;
    }
    uint32 totalfree = 16 - totalused;
    // list out items in other removable backpacks
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        const Bag* const pBag = (Bag*) bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
        {
            ItemTemplate const* pBagProto = pBag->GetTemplate();
            if (pBagProto->GetClass() == ITEM_CLASS_CONTAINER && pBagProto->GetSubClass() == ITEM_SUBCLASS_CONTAINER)
            {
                total += pBag->GetBagSize();
                totalfree += pBag->GetFreeSlots();
            }
        }

    }

	string color = "ff00ff00";
	if (totalfree < total / 2)
		color = "ffffff00";
	if (totalfree < total / 4)
		color = "ffff0000";
    out << "|h|c" << color << (total - totalfree) << "/" << total << "|h|cffffffff Bag";
}

void StatsAction::ListXP( ostringstream &out )
{
    uint32 curXP = bot->GetXP();
    uint32 nextLevelXP = bot->GetXPForNextLevel();
    uint32 xpPercent = 0;
    if (nextLevelXP)
        xpPercent = 100 * curXP / nextLevelXP;

    out << "|r|cff00ff00" << xpPercent << "|r|cffffd333%" << "|h|cffffffff XP";
}

void StatsAction::ListRepairCost(ostringstream &out)
{
    out << chat->formatMoney(EstRepairAll()) << " Repair";
}

uint32 StatsAction::EstRepairAll()
{
    uint32 TotalCost = 0;
    // equipped, backpack, bags itself
    for(int i = EQUIPMENT_SLOT_START; i < INVENTORY_SLOT_ITEM_END; ++i)
        TotalCost += EstRepair(( (INVENTORY_SLOT_BAG_0 << 8) | i ));

    // bank, buyback and keys not repaired

    // items in inventory bags
    for(int j = INVENTORY_SLOT_BAG_START; j < INVENTORY_SLOT_BAG_END; ++j)
        for(int i = 0; i < MAX_BAG_SIZE; ++i)
            TotalCost += EstRepair(( (j << 8) | i ));
    return TotalCost;
}

uint32 StatsAction::EstRepair(uint16 pos)
{
    Item* item = bot->GetItemByPos(pos);
    if (!item)
        return 0;

    return uint32(item->CalculateDurabilityRepairCost(1.0f));
}
