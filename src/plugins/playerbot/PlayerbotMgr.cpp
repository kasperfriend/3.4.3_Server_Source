#include "../pchdef.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "RandomPlayerbotMgr.h"


class LoginQueryHolder;
class CharacterHandler;

namespace
{
    // All holder/session operations run on the world thread. Reserve across
    // holders, not just within one player's pending list.
    std::set<ObjectGuid> pendingBotLogins;
}

PlayerbotHolder::PlayerbotHolder() : PlayerbotAIBase()
{

}

PlayerbotHolder::~PlayerbotHolder()
{
    LogoutAllBots();
}


void PlayerbotHolder::UpdateAIInternal(uint32 elapsed)
{
}

void PlayerbotHolder::UpdateSessions(uint32 /*elapsed*/)
{
    std::vector<ObjectGuid> pending;
    for (auto const& entry : pendingBots)
        pending.push_back(entry.first);
    for (ObjectGuid guid : pending)
    {
        auto it = pendingBots.find(guid);
        if (it == pendingBots.end())
            continue;
        WorldSession* session = it->second;
        try
        {
            session->HandleBotPackets();
            Player* bot = session->GetPlayer();
            // A login can be redirected to homebind before AI attachment.
            if (bot && bot->IsBeingTeleportedFar())
            {
                session->HandleMoveWorldportAck();
                bot = session->GetPlayer();
            }
            if (bot && bot->IsInWorld())
            {
                // Retain pending ownership until AI initialization succeeds.
                OnBotLogin(bot);
                auto [owned, inserted] = botSessions.try_emplace(guid);
                if (!inserted)
                    throw std::runtime_error("Duplicate owned bot session");
                owned->second.reset(session); // ownership transfer after map allocation succeeds
                pendingBots.erase(guid);
                pendingBotLogins.erase(guid);
                continue;
            }
            if (session->PlayerLoading())
                continue;
            TC_LOG_ERROR("playerbot", "Bot {} failed to enter the world", guid.ToString());
        }
        catch (std::exception const& error)
        {
            TC_LOG_ERROR("playerbot", "Bot {} login failed: {}", guid.ToString(), error.what());
        }
        catch (...)
        {
            TC_LOG_ERROR("playerbot", "Bot {} login failed with an unknown exception", guid.ToString());
        }
        // Erase before teardown callbacks can revisit the holder.
        pendingBots.erase(guid);
        pendingBotLogins.erase(guid);
        playerBots.erase(guid);
        delete session;
    }

    // A packet/teleport callback may log a bot out and erase its map entry.
    // Never retain an iterator over playerBots across those callbacks.
    std::vector<ObjectGuid> active;
    for (auto const& entry : botSessions)
        active.push_back(entry.first);
    for (ObjectGuid guid : active)
    {
        auto it = botSessions.find(guid);
        if (it == botSessions.end())
            continue;
        WorldSession* session = it->second.get();
        Player* bot = session->GetPlayer();
        if (bot && bot->GetPlayerbotAI())
        {
            if (bot->IsBeingTeleported())
                bot->GetPlayerbotAI()->HandleTeleportAck();
            else if (bot->IsInWorld())
                session->HandleBotPackets();
        }
        // Do not destroy the session while one of its callbacks is still on
        // the stack; reclaim it only after packet/query handling returns.
        if (!session->GetPlayer())
        {
            playerBots.erase(guid);
            botSessions.erase(guid);
        }
    }
}

void PlayerbotHolder::AddPlayerBot(ObjectGuid guid, uint32 masterAccountId)
{
    if (!sPlayerbotAIConfig.enabled || !guid.IsPlayer() || guid.IsEmpty())
        return;

    if (playerBots.find(guid) != playerBots.end() || pendingBots.find(guid) != pendingBots.end() || botSessions.count(guid))
        return;

    // the character must not be online with a real client
    if (Player* existing = ObjectAccessor::FindConnectedPlayer(guid))
    {
        if (!existing->GetPlayerbotAI())
        {
            TC_LOG_ERROR("playerbot", "Bot {} is already online", guid.ToString());
            return;
        }
        return;
    }

    uint32 accountId = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    if (!accountId)
    {
        TC_LOG_ERROR("playerbot", "Cannot resolve the account of bot {}", guid.ToString());
        return;
    }

    std::string accountName;
    if (!AccountMgr::GetName(accountId, accountName))
        accountName = "playerbot";

    if (!pendingBotLogins.insert(guid).second)
        return;

    // socket-less session, updated by this holder only (never added to the world session map)
    try
    {
        auto session = std::make_unique<WorldSession>(accountId, std::move(accountName), 0, nullptr, SEC_PLAYER,
            uint8(sWorld->getIntConfig(CONFIG_EXPANSION)), 0, "", Minutes(0), LOCALE_enUS, 0, false);
        session->SetBotSession(true);
        session->LoginBotPlayer(guid);
        pendingBots.emplace(guid, session.get());
        session.release();
    }
    catch (...)
    {
        pendingBotLogins.erase(guid);
        throw;
    }

    TC_LOG_DEBUG("playerbot", "Bot {} logging in (master account {})", guid.ToString(), masterAccountId);
}

void PlayerbotHolder::LogoutAllBots()
{
    while (!pendingBots.empty())
        LogoutPlayerBot(pendingBots.begin()->first);
    while (!botSessions.empty())
        LogoutPlayerBot(botSessions.begin()->first);
    while (!playerBots.empty())
        LogoutPlayerBot(playerBots.begin()->first);
}

void PlayerbotHolder::LogoutPlayerBot(ObjectGuid guid)
{
    if (auto pending = pendingBots.find(guid); pending != pendingBots.end())
    {
        WorldSession* session = pending->second;
        pendingBots.erase(pending);
        pendingBotLogins.erase(guid);
        playerBots.erase(guid);
        // Destroying the session drops its query callbacks; no delayed login
        // can attach an AI to an owner who has already logged out.
        delete session;
        return;
    }
    if (auto owned = botSessions.find(guid); owned != botSessions.end())
    {
        std::unique_ptr<WorldSession> session = std::move(owned->second);
        botSessions.erase(owned);
        playerBots.erase(guid);
        if (Player* bot = session->GetPlayer())
        {
            if (PlayerbotAI* ai = bot->GetPlayerbotAI())
                ai->TellMaster("Goodbye!");
            TC_LOG_INFO("playerbot", "Bot {} logged out", bot->GetName());
            session->LogoutPlayer(true);
        }
        return;
    }
    // Entries without an owned session are only possible during initialization
    // or defensive teardown. Never assume a raw Player entry owns a session.
    playerBots.erase(guid);
}

Player* PlayerbotHolder::GetPlayerBot(ObjectGuid playerGuid) const
{
    PlayerBotMap::const_iterator it = playerBots.find(playerGuid);
    return (it == playerBots.end()) ? nullptr : it->second;
}

void PlayerbotHolder::OnBotLogin(Player * const bot)
{
	PlayerbotAI* ai = new PlayerbotAI(bot);
	bot->SetPlayerbotAI(ai);
	OnBotLoginInternal(bot);

    playerBots[bot->GetGUID()] = bot;

    Player* master = ai->GetMaster();
    if (master)
    {
        ObjectGuid masterGuid = master->GetGUID();
        if (master->GetGroup() &&
            ! master->GetGroup()->IsLeader(masterGuid))
            master->GetGroup()->ChangeLeader(masterGuid);
    }

    Group *group = bot->GetGroup();
    if (group)
    {
        bool groupValid = false;
        Group::MemberSlotList const& slots = group->GetMemberSlots();
        for (Group::MemberSlotList::const_iterator i = slots.begin(); i != slots.end(); ++i)
        {
            ObjectGuid member = i->guid;
            uint32 account = sCharacterCache->GetCharacterAccountIdByGuid(member);
            if (!sPlayerbotAIConfig.IsInRandomAccountList(account))
            {
                groupValid = true;
                break;
            }
        }

        if (!groupValid)
        {
            WorldPackets::Party::LeaveGroup leaveGroup{WorldPacket(CMSG_LEAVE_GROUP)};
            bot->GetSession()->HandleLeaveGroupOpcode(leaveGroup);
        }
    }

    ai->ResetStrategies();
    ai->TellMaster("Hello!");
    TC_LOG_INFO("playerbot",  "Bot {} logged in", bot->GetName());
}

string PlayerbotHolder::ProcessBotCommand(string cmd, ObjectGuid guid, bool admin, uint32 masterAccountId, uint32 masterGuildId)
{
    if (!sPlayerbotAIConfig.enabled || guid.IsEmpty())
        return "bot system is disabled";

    uint32 botAccount = sCharacterCache->GetCharacterAccountIdByGuid(guid);
    bool isRandomBot = sRandomPlayerbotMgr.IsRandomBot(guid.GetCounter());
    bool isRandomAccount = sPlayerbotAIConfig.IsInRandomAccountList(botAccount);
    bool isMasterAccount = (masterAccountId == botAccount);

    if (isRandomAccount && !isRandomBot && !admin)
    {
        // the character may be offline (FindPlayer returns null); fall back
        // to the cached guild id instead of dereferencing a null player
        uint32 guildId = 0;
        if (Player* bot = ObjectAccessor::FindPlayer(guid))
            guildId = bot->GetGuildId();
        else
            guildId = uint32(sCharacterCache->GetCharacterGuildIdByGuid(guid));

        if (guildId != masterGuildId)
            return "not in your guild";
    }

    if (!isRandomAccount && !isMasterAccount && !admin)
        return "not in your account";

    if (cmd == "add" || cmd == "login")
    {
        if (ObjectAccessor::FindPlayer(guid))
            return "player already logged in";

        AddPlayerBot(guid, masterAccountId);
        return "ok";
    }
    else if (cmd == "remove" || cmd == "logout" || cmd == "rm")
    {
        if (pendingBots.count(guid))
        {
            LogoutPlayerBot(guid);
            return "ok";
        }
        if (!ObjectAccessor::FindPlayer(guid))
            return "player is offline";

        if (!GetPlayerBot(guid))
            return "not your bot";

        LogoutPlayerBot(guid);
        return "ok";
    }

    if (admin)
    {
        Player* bot = GetPlayerBot(guid);
        if (!bot || !bot->GetPlayerbotAI())
            return "bot not found";

        Player* master = bot->GetPlayerbotAI()->GetMaster();
        if (master)
        {
            if (cmd == "init=white" || cmd == "init=common")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_NORMAL);
                factory.CleanRandomize();
                return "ok";
            }
            else if (cmd == "init=green" || cmd == "init=uncommon")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_UNCOMMON);
                factory.CleanRandomize();
                return "ok";
            }
            else if (cmd == "init=blue" || cmd == "init=rare")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_RARE);
                factory.CleanRandomize();
                return "ok";
            }
            else if (cmd == "init=epic" || cmd == "init=purple")
            {
                PlayerbotFactory factory(bot, master->GetLevel(), ITEM_QUALITY_EPIC);
                factory.CleanRandomize();
                return "ok";
            }
        }

        if (cmd == "update")
        {
            PlayerbotFactory factory(bot, bot->GetLevel());
            factory.Refresh();
            return "ok";
        }
        else if (cmd == "random")
        {
            sRandomPlayerbotMgr.Randomize(bot);
            return "ok";
        }
    }

    return "unknown command";
}

bool PlayerbotMgr::HandlePlayerbotMgrCommand(ChatHandler* handler, char const* args)
{
	if (!sPlayerbotAIConfig.enabled)
	{
		handler->PSendSysMessage("|cffff0000Playerbot system is currently disabled!");
		handler->SetSentErrorMessage(true);
        return false;
	}

    WorldSession *m_session = handler->GetSession();

    if (!m_session)
    {
        handler->PSendSysMessage("You may only add bots from an active session");
        handler->SetSentErrorMessage(true);
        return false;
    }

    Player* player = m_session->GetPlayer();
    if (!player)
    {
        handler->PSendSysMessage("You may only add bots from an in-game session");
        handler->SetSentErrorMessage(true);
        return false;
    }

    PlayerbotMgr* mgr = player->GetPlayerbotMgr();
    if (!mgr)
    {
        handler->PSendSysMessage("you cannot control bots yet");
        handler->SetSentErrorMessage(true);
        return false;
    }

    list<string> messages = mgr->HandlePlayerbotCommand(args, player);
    if (messages.empty())
        return true;

    for (list<string>::iterator i = messages.begin(); i != messages.end(); ++i)
    {
        handler->PSendSysMessage(i->c_str());
    }

    handler->SetSentErrorMessage(true);
    return false;
}

list<string> PlayerbotHolder::HandlePlayerbotCommand(char const* args, Player* master)
{
    list<string> messages;

    std::istringstream input(args ? args : "");
    std::string cmdStr, charnameStr;
    if (!(input >> cmdStr))
    {
        messages.push_back("usage: list or add/init/remove PLAYERNAME");
        return messages;
    }
    if (cmdStr == "list")
    {
        messages.push_back(ListBots(master));
        return messages;
    }
    if (!(input >> charnameStr))
    {
        messages.push_back("usage: list or add/init/remove PLAYERNAME");
        return messages;
    }

    set<string> bots;
    if (charnameStr == "*" && master)
    {
        Group* group = master->GetGroup();
        if (!group)
        {
            messages.push_back("you must be in group");
            return messages;
        }

        Group::MemberSlotList slots = group->GetMemberSlots();
        for (Group::member_citerator i = slots.begin(); i != slots.end(); i++)
        {
			ObjectGuid member = i->guid;

			if (member == master->GetGUID())
				continue;

			string bot;
			if (sCharacterCache->GetCharacterNameByGuid(member, bot))
			    bots.insert(bot);
        }
    }

    if (charnameStr == "!" && master && master->GetSession()->GetSecurity() > SEC_GAMEMASTER)
    {
        for (PlayerBotMap::const_iterator i = GetPlayerBotsBegin(); i != GetPlayerBotsEnd(); ++i)
        {
            Player* bot = i->second;
            if (bot && bot->IsInWorld())
                bots.insert(bot->GetName());
        }
    }

    vector<string> chars = split(charnameStr, ',');
    for (vector<string>::iterator i = chars.begin(); i != chars.end(); i++)
    {
        string s = *i;

        uint32 accountId = GetAccountId(s);
        if (!accountId)
        {
            bots.insert(s);
            continue;
        }

        QueryResult results = CharacterDatabase.PQuery(
            "SELECT name FROM characters WHERE account = '{}'",
            accountId);
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                string charName = fields[0].GetString();
                bots.insert(charName);
            } while (results->NextRow());
        }
	}

    for (set<string>::iterator i = bots.begin(); i != bots.end(); ++i)
    {
        string bot = *i;
        ostringstream out;
        out << cmdStr << ": " << bot << " - ";

        ObjectGuid member = sCharacterCache->GetCharacterGuidByName(bot);
        if (!member)
        {
            out << "character not found";
        }
        else if (master && member != master->GetGUID())
        {
            out << ProcessBotCommand(cmdStr, member,
                    master->GetSession()->GetSecurity() >= SEC_GAMEMASTER,
                    master->GetSession()->GetAccountId(),
                    master->GetGuildId());
        }
        else if (!master)
        {
            out << ProcessBotCommand(cmdStr, member, true, -1, -1);
        }

        messages.push_back(out.str());
    }

    return messages;
}

uint32 PlayerbotHolder::GetAccountId(string name)
{
    uint32 accountId = 0;

    // name comes straight from a player chat command - a raw quote in it would
    // produce a malformed query, and a SQL error aborts this server
    LoginDatabase.EscapeString(name);

    QueryResult results = LoginDatabase.PQuery("SELECT id FROM account WHERE username = '{}'", name);
    if(results)
    {
        Field* fields = results->Fetch();
        accountId = fields[0].GetUInt32();
    }

    return accountId;
}

string PlayerbotHolder::ListBots(Player* master)
{
    set<string> bots;
    map<uint8,string> classNames;
    classNames[CLASS_DRUID] = "Druid";
    classNames[CLASS_HUNTER] = "Hunter";
    classNames[CLASS_MAGE] = "Mage";
    classNames[CLASS_PALADIN] = "Paladin";
    classNames[CLASS_PRIEST] = "Priest";
    classNames[CLASS_ROGUE] = "Rogue";
    classNames[CLASS_SHAMAN] = "Shaman";
    classNames[CLASS_WARLOCK] = "Warlock";
    classNames[CLASS_WARRIOR] = "Warrior";
    classNames[CLASS_DEATH_KNIGHT] = "Death Knight";
    ostringstream out;
    bool first = true;
    out << "Bot roster: ";
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        string name = bot->GetName();
        bots.insert(name);

        if (first) first = false; else out << ", ";
        out << "+" << name << " " << classNames[bot->GetClass()];
    }

    if (master)
    {
        QueryResult results = CharacterDatabase.PQuery("SELECT class,name FROM characters where account = '{}'",
                master->GetSession()->GetAccountId());
        if (results != NULL)
        {
            do
            {
                Field* fields = results->Fetch();
                uint8 cls = fields[0].GetUInt8();
                string name = fields[1].GetString();
                if (bots.find(name) == bots.end() && name != master->GetSession()->GetPlayerName())
                {
                    if (first) first = false; else out << ", ";
                    out << "-" << name << " " << classNames[cls];
                }
            } while (results->NextRow());
        }
    }

    return out.str();
}


PlayerbotMgr::PlayerbotMgr(Player* const master) : PlayerbotHolder(),  master(master)
{
}

PlayerbotMgr::~PlayerbotMgr()
{
}

void PlayerbotMgr::UpdateAIInternal(uint32 elapsed)
{
    SetNextCheckDelay(sPlayerbotAIConfig.reactDelay);
}

void PlayerbotMgr::HandleCommand(uint32 type, const string& text)
{
    Player *master = GetMaster();
    if (!master)
        return;

    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (ai)
            ai->HandleCommand(type, text, *master);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (ai && ai->GetMaster() == master)
            ai->HandleCommand(type, text, *master);
    }
}

void PlayerbotMgr::HandleMasterIncomingPacket(const WorldPacket& packet)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (ai)
            ai->HandleMasterIncomingPacket(packet);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (ai && ai->GetMaster() == GetMaster())
            ai->HandleMasterIncomingPacket(packet);
    }

    switch (packet.GetOpcode())
    {
        // if master is logging out, log out all bots
        case CMSG_LOGOUT_REQUEST:
        {
            LogoutAllBots();
            return;
        }
    }
}
void PlayerbotMgr::HandleMasterOutgoingPacket(const WorldPacket& packet)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (ai)
            ai->HandleMasterOutgoingPacket(packet);
    }

    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (ai && ai->GetMaster() == GetMaster())
            ai->HandleMasterOutgoingPacket(packet);
    }
}

void PlayerbotMgr::SaveToDB()
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        bot->SaveToDB();
    }
    for (PlayerBotMap::const_iterator it = sRandomPlayerbotMgr.GetPlayerBotsBegin(); it != sRandomPlayerbotMgr.GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (ai && ai->GetMaster() == GetMaster())
            bot->SaveToDB();
    }
}

void PlayerbotMgr::OnBotLoginInternal(Player * const bot)
{
    bot->GetPlayerbotAI()->SetMaster(master);
    bot->GetPlayerbotAI()->ResetStrategies();
}
