#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/QueryQuestAction.h"


using namespace ai;

void QueryQuestAction::TellObjective(string name, int available, int required)
{
    ai->TellMaster(chat->formatQuestObjective(name, available, required));
}


bool QueryQuestAction::Execute(Event event)
{

    Player *bot = ai->GetBot();
    string text = event.getParam();

    PlayerbotChatHandler ch(bot);
    uint32 questId = ch.extractQuestId(text);
    if (!questId)
        return false;

    for (uint16 slot = 0; slot < MAX_QUEST_LOG_SIZE; ++slot)
    {
        if(questId != bot->GetQuestSlotQuestId(slot))
            continue;

        Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);
        if (!questTemplate)
            continue;

        ostringstream out;
        out << "--- " << chat->formatQuest(questTemplate) << " ";
        if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
        {
            out << "|c0000FF00completed|r ---";
            ai->TellMaster(out);
        }
        else
        {
            out << "|c00FF0000not completed|r ---";
            ai->TellMaster(out);
            TellObjectives(questId);
        }

        return true;
    }

    return false;
}

void QueryQuestAction::TellObjectives(uint32 questId)
{
    Quest const* questTemplate = sObjectMgr->GetQuestTemplate(questId);
    if (!questTemplate)
        return;

    for (QuestObjective const& objective : questTemplate->GetObjectives())
    {
        if (!objective.IsStoringValue())
        {
            if (!objective.Description.empty())
                ai->TellMaster(objective.Description);
            continue;
        }

        int32 required = objective.Amount;
        int32 available = bot->GetQuestObjectiveData(objective);

        switch (objective.Type)
        {
            case QUEST_OBJECTIVE_ITEM:
            {
                if (ItemTemplate const* proto = sObjectMgr->GetItemTemplate(uint32(objective.ObjectID)))
                    TellObjective(chat->formatItem(proto), available, required);
                break;
            }
            case QUEST_OBJECTIVE_GAMEOBJECT:
            {
                if (GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(uint32(objective.ObjectID)))
                    TellObjective(info->name, available, required);
                break;
            }
            case QUEST_OBJECTIVE_MONSTER:
            case QUEST_OBJECTIVE_TALKTO:
            {
                if (CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(uint32(objective.ObjectID)))
                    TellObjective(info->Name, available, required);
                break;
            }
            default:
                if (!objective.Description.empty())
                    TellObjective(objective.Description, available, required);
                break;
        }
    }
}
