#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/DestroyItemAction.h"

#include "../values/ItemCountValue.h"

using namespace ai;

bool DestroyItemAction::Execute(Event event)
{
    string text = event.getParam();
    ItemIds ids = chat->parseItems(text);

    for (ItemIds::iterator i =ids.begin(); i != ids.end(); i++)
    {
        FindItemByIdVisitor visitor(*i);
        DestroyItem(&visitor);
    }

    return true;
}

void DestroyItemAction::DestroyItem(FindItemVisitor* visitor)
{
    IterateItems(visitor);
    list<Item*> items = visitor->GetResult();
	for (list<Item*>::iterator i = items.begin(); i != items.end(); ++i)
    {
		Item* item = *i;

        // build the confirmation text while the item still exists - after
        // DestroyItem the item is removed (and new items are deleted in place)
        ostringstream out; out << chat->formatItem(item->GetTemplate()) << " destroyed";

        bot->DestroyItem(item->GetBagSlot(), item->GetSlot(), true);
        ai->TellMaster(out);
    }
}
