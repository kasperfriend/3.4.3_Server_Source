#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/QueryItemUsageAction.h"
#include "../values/ItemUsageValue.h"
#include "../../../ahbot/AhBot.h"
#include "../../RandomPlayerbotMgr.h"


using namespace ai;


bool QueryItemUsageAction::Execute(Event event)
{
    WorldPacket& data = event.getPacket();
    if (!data.empty())
    {
        data.rpos(0);

        // SMSG_ITEM_PUSH_RESULT in 3.4.3 (WorldPackets::Item::ItemPushResult::Write,
        // Player::SendNewItem) - not the classic 3.3.5 field order this action
        // used to read (which mis-parsed every push now that the item flow works
        // again: the fields after the guid are different and the item id sits
        // inside a trailing ItemInstance). Read the modern layout instead.
        ObjectGuid guid;
        data >> guid;
        if (guid != bot->GetGUID())
            return false;

        data.read_skip<uint8>();                // Slot
        data.read_skip<int32>();                // SlotInBag
        int32 questLogItemId = 0;
        data >> questLogItemId;                 // only set when it differs from the real id
        int32 quantity = 0;
        data >> quantity;                       // count of the pushed stack
        data.read_skip<int32>();                // QuantityInInventory
        data.read_skip<int32>();                // DungeonEncounterID
        data.read_skip<int32>();                // BattlePetSpeciesID
        data.read_skip<int32>();                // BattlePetBreedID
        data.read_skip<uint32>();               // BattlePetBreedQuality
        data.read_skip<int32>();                // BattlePetLevel
        data.read_skip<ObjectGuid>();           // ItemGUID

        bool pushed = data.ReadBit();           // Pushed (e.g. quest reward, trade)
        bool created = data.ReadBit();          // Created (crafted)
        data.ReadBit();                         // Unused_1017
        data.ReadBits(3);                       // DisplayText
        data.ReadBit();                         // IsBonusRoll
        data.ReadBit();                         // IsEncounterLoot
        data.ResetBitPos();

        int32 itemId = 0;
        data >> itemId;                         // ItemInstance.ItemID (first field)
        if (itemId <= 0)
            itemId = questLogItemId;            // fall back to the quest-credit id
        if (itemId <= 0)
            return false;

        ItemTemplate const *item = sObjectMgr->GetItemTemplate(uint32(itemId));
        if (!item)
            return false;

        ostringstream out; out << chat->formatItem(item, quantity);
        if (created)
            out << " created";
        else if (pushed)
            out << " received";
        ai->TellMaster(out);

        QueryItemUsage(item);
        QueryQuestItem(uint32(itemId));
        return true;
    }

    string text = event.getParam();

    ItemIds items = chat->parseItems(text);
    QueryItemsUsage(items);
    return true;
}

bool QueryItemUsageAction::QueryItemUsage(ItemTemplate const *item)
{
    ostringstream out; out << item->GetId();
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
    switch (usage)
    {
    case ITEM_USAGE_EQUIP:
        ai->TellMaster("Equip");
        return true;
    case ITEM_USAGE_REPLACE:
        ai->TellMaster("Equip (replace)");
        return true;
    case ITEM_USAGE_SKILL:
        ai->TellMaster("Tradeskill");
        return true;
    case ITEM_USAGE_USE:
        ai->TellMaster("Use");
        return true;
    case ITEM_USAGE_GUILD_TASK:
        ai->TellMaster("Guild task");
        return true;
    }

    return false;
}

void QueryItemUsageAction::QueryItemPrice(ItemTemplate const *item)
{
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return;

    if (item->GetBonding() == BIND_ON_ACQUIRE)
        return;

    list<Item*> items = InventoryAction::parseItems(item->GetDefaultLocaleName());
    if (!items.empty())
    {
        for (list<Item*>::iterator i = items.begin(); i != items.end(); ++i)
        {
            Item* sell = *i;
            int32 sellPrice = sell->GetCount() * auctionbot.GetSellPrice(sell->GetTemplate()) * sRandomPlayerbotMgr.GetSellMultiplier(bot);
            ostringstream out;
            out << "Selling " << chat->formatItem(sell->GetTemplate(), sell->GetCount()) << " for " << chat->formatMoney(sellPrice);
            ai->TellMaster(out.str());
        }
    }

    ostringstream out; out << item->GetId();
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
    if (usage == ITEM_USAGE_NONE)
        return;

    int32 buyPrice = auctionbot.GetBuyPrice(item) * sRandomPlayerbotMgr.GetBuyMultiplier(bot);
    if (buyPrice)
    {
        ostringstream out;
        out << "Will buy for " << chat->formatMoney(buyPrice);
        ai->TellMaster(out.str());
    }
}

void QueryItemUsageAction::QueryItemsUsage(ItemIds items)
{
    for (ItemIds::iterator i = items.begin(); i != items.end(); i++)
    {
        ItemTemplate const *item = sObjectMgr->GetItemTemplate(*i);
        QueryItemUsage(item);
        QueryQuestItem(*i);
        QueryItemPrice(item);
    }
}

void QueryItemUsageAction::QueryQuestItem(uint32 itemId)
{
    Player *bot = ai->GetBot();
    QuestStatusMap const& questMap = bot->getQuestStatusMap();
    for (QuestStatusMap::const_iterator i = questMap.begin(); i != questMap.end(); i++)
    {
        const Quest *questTemplate = sObjectMgr->GetQuestTemplate( i->first );
        if( !questTemplate )
            continue;

        uint32 questId = questTemplate->GetQuestId();
        QuestStatus status = bot->GetQuestStatus(questId);
        if (status == QUEST_STATUS_INCOMPLETE || (status == QUEST_STATE_COMPLETE && !bot->GetQuestRewardStatus(questId)))
        {
            QuestStatusData const& questStatus = i->second;
            QueryQuestItem(itemId, questTemplate, &questStatus);
        }
    }
}


void QueryItemUsageAction::QueryQuestItem(uint32 itemId, const Quest *questTemplate, const QuestStatusData *questStatus)
{
    for (QuestObjective const& objective : questTemplate->GetObjectives())
    {
        if (objective.Type != QUEST_OBJECTIVE_ITEM || uint32(objective.ObjectID) != itemId)
            continue;

        int32 required = objective.Amount;
        if (!required)
            continue;

        int32 available = bot->GetQuestObjectiveData(objective);

        ai->TellMaster(chat->formatQuestObjective(chat->formatQuest(questTemplate), available, required));
    }
}

