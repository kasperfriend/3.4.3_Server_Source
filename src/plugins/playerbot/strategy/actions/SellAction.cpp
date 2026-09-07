#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/SellAction.h"
#include "../ItemVisitors.h"

using namespace ai;

class SellItemsVisitor : public IterateItemsVisitor
{
public:
    SellItemsVisitor(SellAction* action) : IterateItemsVisitor()
    {
        this->action = action;
    }

    virtual bool Visit(Item* item)
    {
        action->Sell(item);
        return true;
    }

private:
    SellAction* action;
};

class SellGrayItemsVisitor : public SellItemsVisitor
{
public:
    SellGrayItemsVisitor(SellAction* action) : SellItemsVisitor(action) {}

    virtual bool Visit(Item* item)
    {
        ItemTemplate const* proto = item->GetTemplate();
        if (!proto || proto->GetQuality() != ITEM_QUALITY_POOR)
            return true;

        return SellItemsVisitor::Visit(item);
    }
};


bool SellAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    string text = event.getParam();

    if (text == "gray" || text == "*")
    {
        SellGrayItemsVisitor visitor(this);
        IterateItems(&visitor);
        return true;
    }

    ItemIds ids = chat->parseItems(text);

    for (ItemIds::iterator i =ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        Sell(&visitor);
    }

    return true;
}


void SellAction::Sell(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    list<Item*> items = visitor->GetResult();
    for (list<Item*>::iterator i = items.begin(); i != items.end(); ++i)
        Sell(*i);
}

void SellAction::Sell(Item* item)
{
    Player* master = GetMaster();
    Unit* vendor = master->GetSelectedUnit();
    if (!vendor)
    {
        ai->TellMaster("Select a vendor first");
        return;
    }

    ObjectGuid itemguid = item->GetGUID();
    uint32 count = item->GetCount();

    // the template can be gone while the bot still carries the item; the core
    // sell handler would reject it anyway, so do not dereference null here
    ItemTemplate const* proto = item->GetTemplate();
    if (!proto)
        return;

    ostringstream out; out << chat->formatItem(proto) << " sold";

    WorldPackets::Item::SellItem sell{WorldPacket(CMSG_SELL_ITEM)};
    sell.VendorGUID = vendor->GetGUID();
    sell.ItemGUID = itemguid;
    sell.Amount = count;
    bot->GetSession()->HandleSellItemOpcode(sell);

    // build the chat line before the sell: a successful full sell moves the
    // item into the buyback slot, so referencing it afterwards is fragile
    ai->TellMaster(out.str());
}
