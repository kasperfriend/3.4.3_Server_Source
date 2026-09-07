#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/QuestAction.h"
#include "../../PlayerbotAIConfig.h"

using namespace ai;

bool QuestAction::Execute(Event event)
{
    ObjectGuid guid = event.getObject();

    Player* master = GetMaster();
    if (!master)
        return false;

    if (!guid)
    {
        Unit* target = master->GetSelectedUnit();
        if (target)
            guid = target->GetGUID();
    }

    if (!guid)
        return false;

    return ProcessQuests(guid);
}

bool QuestAction::ProcessQuests(ObjectGuid questGiver)
{
    GameObject *gameObject = ai->GetGameObject(questGiver);
    if (gameObject && gameObject->GetGoType() == GAMEOBJECT_TYPE_QUESTGIVER)
        return ProcessQuests(gameObject);

    Creature* creature = ai->GetCreature(questGiver);
    if (creature)
        return ProcessQuests(creature);

    return false;
}

bool QuestAction::ProcessQuests(WorldObject* questGiver)
{
    ObjectGuid guid = questGiver->GetGUID();

    if (bot->GetDistance(questGiver) > INTERACTION_DISTANCE)
    {
        ai->TellMaster("Cannot talk to quest giver");
        return false;
    }

    if (!bot->isInFront(questGiver, M_PI / 2))
        bot->SetFacingTo(bot->GetAbsoluteAngle(questGiver));

    bot->SetSelection(guid);
    bot->PrepareQuestMenu(guid);
    QuestMenu& questMenu = bot->PlayerTalkClass->GetQuestMenu();
    for (uint32 i = 0; i < questMenu.GetMenuItemCount(); ++i)
    {
        QuestMenuItem const& menuItem = questMenu.GetItem(i);
        uint32 questID = menuItem.QuestId;
        Quest const* quest = sObjectMgr->GetQuestTemplate(questID);
        if (!quest)
            continue;

        ProcessQuest(quest, questGiver);
    }

    return true;
}

bool QuestAction::AcceptQuest(Quest const* quest, ObjectGuid questGiver)
{
    std::ostringstream out;

    uint32 questId = quest->GetQuestId();

    if (bot->GetQuestStatus(questId) == QUEST_STATUS_COMPLETE)
        out << "Already completed";
    else if (! bot->CanTakeQuest(quest, false))
    {
        if (! bot->SatisfyQuestStatus(quest, false))
            out << "Already on";
        else
            out << "Can't take";
    }
    else if (! bot->SatisfyQuestLog(false))
        out << "Quest log is full";
    else if (! bot->CanAddQuest(quest, false))
        out << "Bags are full";

    else
    {
        WorldPackets::Quest::QuestGiverAcceptQuest accept{WorldPacket(CMSG_QUEST_GIVER_ACCEPT_QUEST)};
        accept.QuestGiverGUID = questGiver;
        accept.QuestID = int32(questId);
        bot->GetSession()->HandleQuestgiverAcceptQuestOpcode(accept);

        if (bot->GetQuestStatus(questId) != QUEST_STATUS_NONE)
        {
            out << "Accepted " << chat->formatQuest(quest);
            ai->TellMaster(out);
            return true;
        }
    }

    out << " " << chat->formatQuest(quest);
    ai->TellMaster(out);
    return false;
}

bool QuestObjectiveCompletedAction::Execute(Event event)
{
    WorldPacket p(event.getPacket());
    p.rpos(0);

    // SMSG_QUEST_UPDATE_ADD_CREDIT in 3.4.3 (WorldPackets::Quest::QuestUpdateAddCredit)
    // is victim guid, quest id, object id, count, required, objective type; the
    // classic 3.3.5 order this action used to read needed more bytes and always
    // overran, so the objective report never reached the master. Read the
    // modern layout instead. The victim guid is serialized in the packed
    // ObjectGuid format, so it must be consumed with operator>> - skipping a
    // fixed sizeof(ObjectGuid) walks past the guid into the int32 fields and
    // the reads below then run past the end of the packet.
    ObjectGuid victimGuid;
    p >> victimGuid;                    // VictimGUID
    p.read_skip<int32>();               // QuestID
    int32 objectId = 0;
    p >> objectId;                      // credit entry; negative for game objects
    uint16 available = 0;
    p >> available;                     // Count
    uint16 required = 0;
    p >> required;                      // Required
    p.read_skip<uint8>();               // ObjectiveType

    if (objectId < 0)
    {
        GameObjectTemplate const* info = sObjectMgr->GetGameObjectTemplate(uint32(-(int64)objectId));
        if (info)
            ai->TellMaster(chat->formatQuestObjective(info->name, available, required));
    }
    else
    {
        CreatureTemplate const* info = sObjectMgr->GetCreatureTemplate(uint32(objectId));
        if (info)
            ai->TellMaster(chat->formatQuestObjective(info->Name, available, required));
    }

    return true;
}
