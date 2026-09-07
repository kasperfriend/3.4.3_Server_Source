#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/AcceptQuestAction.h"

using namespace ai;

void AcceptAllQuestsAction::ProcessQuest(Quest const* quest, WorldObject* questGiver)
{
    AcceptQuest(quest, questGiver->GetGUID());
}

bool AcceptQuestAction::Execute(Event event)
{
    Player* master = GetMaster();

    if (!master)
        return false;

    Player *bot = ai->GetBot();
    ObjectGuid guid;
    uint32 quest = 0;

    string text = event.getParam();
    PlayerbotChatHandler ch(master);
    quest = ch.extractQuestId(text);
    if (quest)
    {
        Unit* npc = master->GetSelectedUnit();
        if (!npc)
        {
            ai->TellMaster("Please select quest giver NPC");
            return false;
        }

        guid = npc->GetGUID();
    }
    else if (!event.getPacket().empty())
    {
        if (event.getPacket().GetOpcode() != CMSG_QUEST_GIVER_ACCEPT_QUEST)
            return false;
        event.getPacket().rpos(0);
        event.getPacket().ResetBitPos();
        WorldPackets::Quest::QuestGiverAcceptQuest packet(WorldPacket(event.getPacket()));
        packet.Read();
        guid = packet.QuestGiverGUID;
        quest = uint32(packet.QuestID);
    }
    else if (text == "*")
    {
        return QuestAction::Execute(event);
    }
    else
        return false;

    Quest const* qInfo = sObjectMgr->GetQuestTemplate(quest);
    if (!qInfo)
        return false;

    return AcceptQuest(qInfo, guid);
}

bool AcceptQuestShareAction::Execute(Event event)
{
    Player* master = GetMaster();
    Player *bot = ai->GetBot();

    if (!master || event.getPacket().GetOpcode() != CMSG_PUSH_QUEST_TO_PARTY)
        return false;
    event.getPacket().rpos(0);
    event.getPacket().ResetBitPos();
    WorldPackets::Quest::PushQuestToParty packet(WorldPacket(event.getPacket()));
    packet.Read();
    if (bot->GetSharedQuestID() != packet.QuestID || bot->GetPlayerSharingQuest() != master->GetGUID())
        return false; // stale share, or a quest the core did not offer to this bot
    uint32 quest = packet.QuestID;
    Quest const* qInfo = sObjectMgr->GetQuestTemplate(quest);

    if (!qInfo || bot->GetPlayerSharingQuest().IsEmpty())
        return false;

    quest = qInfo->GetQuestId();
    if( !bot->CanTakeQuest( qInfo, false ) )
    {
        // can't take quest
        bot->ClearQuestSharingInfo();
        ai->TellMaster("I can't take this quest");

        return false;
    }

    // send msg to quest giving player
    master->SendPushToPartyResponse( bot, QuestPushReason::Accepted );
    bot->ClearQuestSharingInfo();

    if( bot->CanAddQuest( qInfo, false ) )
    {
        bot->AddQuest( qInfo, master );

        if( bot->CanCompleteQuest( quest ) )
            bot->CompleteQuest( quest );

        // Runsttren: did not add typeid switch from WorldSession::HandleQuestgiverAcceptQuestOpcode!
        // I think it's not needed, cause typeid should be TYPEID_PLAYER - and this one is not handled
        // there and there is no default case also.

        if( qInfo->GetSrcSpell() > 0 )
            bot->CastSpell( bot, qInfo->GetSrcSpell(), true );

        ai->TellMaster("Quest accepted");
        return true;
    }

    return false;
}
