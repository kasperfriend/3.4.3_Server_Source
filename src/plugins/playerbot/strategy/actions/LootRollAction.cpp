#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/LootRollAction.h"
#include "Groups/Group.h"


using namespace ai;

bool LootRollAction::Execute(Event event)
{
    Player *bot = QueryItemUsageAction::ai->GetBot();

    WorldPackets::Loot::LootRoll packet(WorldPacket(event.getPacket()));
    packet.Read();

    ObjectGuid lootObject = packet.LootObj;
    uint8 lootListId = packet.LootListID;

    Group* group = bot->GetGroup();
    if (!group)
        return false;

    RollVote vote = PASS;
    for (Roll const* roll : group->GetRolls())
    {
        if (!roll || !roll->isValid())
            continue;

        Loot* loot = const_cast<Roll*>(roll)->getLoot();
        if (!loot || loot->GetGUID() != lootObject || roll->itemSlot != lootListId)
            continue;

        uint32 itemId = roll->itemid;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        switch (proto->GetClass())
        {
        case ITEM_CLASS_WEAPON:
        case ITEM_CLASS_ARMOR:
            if (QueryItemUsage(proto))
                vote = NEED;
            else if (bot->HasSkill(SKILL_ENCHANTING))
                vote = DISENCHANT;
            break;
        default:
            if (IsLootAllowed(itemId))
                vote = NEED;
            break;
        }
        break;
    }

    switch (group->GetLootMethod())
    {
    case MASTER_LOOT:
    case FREE_FOR_ALL:
        group->CountRollVote(bot->GetGUID(), lootObject, lootListId, PASS);
        break;
    default:
        group->CountRollVote(bot->GetGUID(), lootObject, lootListId, vote);
        break;
    }

    return true;
}
