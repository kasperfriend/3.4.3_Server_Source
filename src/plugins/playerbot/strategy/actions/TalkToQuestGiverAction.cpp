#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/TalkToQuestGiverAction.h"


using namespace ai;

void TalkToQuestGiverAction::ProcessQuest(Quest const* quest, WorldObject* questGiver)
{
    std::ostringstream out; out << "Quest ";

    QuestStatus status = bot->GetQuestStatus(quest->GetQuestId());
    switch (status)
    {
    case QUEST_STATUS_COMPLETE:
        TurnInQuest(quest, questGiver, out);
        break;
    case QUEST_STATUS_INCOMPLETE:
        out << "|cffff0000Incompleted|r";
        break;
    case QUEST_STATUS_NONE:
        out << "|cff00ff00Available|r";
        break;
    case QUEST_STATUS_FAILED:
        out << "|cffff0000Failed|r";
        break;
    }

    out << ": " << chat->formatQuest(quest);
    ai->TellMaster(out);
}

void TalkToQuestGiverAction::TurnInQuest(Quest const* quest, WorldObject* questGiver, ostringstream& out)
{
    uint32 questID = quest->GetQuestId();

    if (bot->GetQuestRewardStatus(questID))
        return;

    if (quest->GetRewChoiceItemsCount() == 0)
        RewardNoItem(quest, questGiver, out);
    else if (quest->GetRewChoiceItemsCount() == 1)
        RewardSingleItem(quest, questGiver, out);
    else {
        AskToSelectReward(quest, out);
    }
}

void TalkToQuestGiverAction::RewardNoItem(Quest const* quest, WorldObject* questGiver, ostringstream& out)
{
    if (bot->CanRewardQuest(quest, false))
    {
        bot->RewardQuest(quest, LootItemType::Item, 0, questGiver, false);
        out << "Completed";
    }
    else
    {
        out << "|cffff0000Unable to turn in|r";
    }
}

void TalkToQuestGiverAction::RewardSingleItem(Quest const* quest, WorldObject* questGiver, ostringstream& out)
{
    int index = 0;
    ItemTemplate const *item = sObjectMgr->GetItemTemplate(quest->RewardChoiceItemId[index]);
    if (bot->CanRewardQuest(quest, LootItemType::Item, index, false))
    {
        bot->RewardQuest(quest, LootItemType::Item, index, questGiver, true);

        out << "Rewarded " << chat->formatItem(item);
    }
    else
    {
        out << "|cffff0000Unable to turn in:|r, reward: " << chat->formatItem(item);
    }
}

void TalkToQuestGiverAction::AskToSelectReward(Quest const* quest, ostringstream& out)
{
    ostringstream msg;
    msg << "Choose reward: ";
    for (uint8 i=0; i < quest->GetRewChoiceItemsCount(); ++i)
    {
        ItemTemplate const* item = sObjectMgr->GetItemTemplate(quest->RewardChoiceItemId[i]);
        // quest data may reference item ids that do not exist in this build's
        // DB2 stores - a null template must not be dereferenced
        if (!item)
            continue;
        msg << chat->formatItem(item);
    }
    ai->TellMaster(msg);

    out << "Reward pending";
}
