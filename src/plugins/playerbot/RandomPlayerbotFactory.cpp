#include "../pchdef.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "../../server/database/Database/DatabaseEnv.h"
#include "PlayerbotAI.h"
#include "../../server/game/Entities/Player/Player.h"
#include "../../server/game/Guilds/Guild.h"
#include "../../server/game/Guilds/GuildMgr.h"
#include "RandomPlayerbotFactory.h"

map<uint8, vector<uint8> > RandomPlayerbotFactory::availableRaces;

RandomPlayerbotFactory::RandomPlayerbotFactory(uint32 accountId) : accountId(accountId)
{
    availableRaces[CLASS_WARRIOR].push_back(RACE_HUMAN);
    availableRaces[CLASS_WARRIOR].push_back(RACE_NIGHTELF);
    availableRaces[CLASS_WARRIOR].push_back(RACE_GNOME);
    availableRaces[CLASS_WARRIOR].push_back(RACE_DWARF);
    availableRaces[CLASS_WARRIOR].push_back(RACE_ORC);
    availableRaces[CLASS_WARRIOR].push_back(RACE_UNDEAD_PLAYER);
    availableRaces[CLASS_WARRIOR].push_back(RACE_TAUREN);
    availableRaces[CLASS_WARRIOR].push_back(RACE_TROLL);
    availableRaces[CLASS_WARRIOR].push_back(RACE_DRAENEI);

    availableRaces[CLASS_PALADIN].push_back(RACE_HUMAN);
    availableRaces[CLASS_PALADIN].push_back(RACE_DWARF);
    availableRaces[CLASS_PALADIN].push_back(RACE_DRAENEI);
    availableRaces[CLASS_PALADIN].push_back(RACE_BLOODELF);

    availableRaces[CLASS_ROGUE].push_back(RACE_HUMAN);
    availableRaces[CLASS_ROGUE].push_back(RACE_DWARF);
    availableRaces[CLASS_ROGUE].push_back(RACE_NIGHTELF);
    availableRaces[CLASS_ROGUE].push_back(RACE_GNOME);
    availableRaces[CLASS_ROGUE].push_back(RACE_ORC);
    availableRaces[CLASS_ROGUE].push_back(RACE_TROLL);
    availableRaces[CLASS_ROGUE].push_back(RACE_BLOODELF);

    availableRaces[CLASS_PRIEST].push_back(RACE_HUMAN);
    availableRaces[CLASS_PRIEST].push_back(RACE_DWARF);
    availableRaces[CLASS_PRIEST].push_back(RACE_NIGHTELF);
    availableRaces[CLASS_PRIEST].push_back(RACE_DRAENEI);
    availableRaces[CLASS_PRIEST].push_back(RACE_TROLL);
    availableRaces[CLASS_PRIEST].push_back(RACE_UNDEAD_PLAYER);
    availableRaces[CLASS_PRIEST].push_back(RACE_BLOODELF);

    availableRaces[CLASS_MAGE].push_back(RACE_HUMAN);
    availableRaces[CLASS_MAGE].push_back(RACE_GNOME);
    availableRaces[CLASS_MAGE].push_back(RACE_DRAENEI);
    availableRaces[CLASS_MAGE].push_back(RACE_UNDEAD_PLAYER);
    availableRaces[CLASS_MAGE].push_back(RACE_TROLL);
    availableRaces[CLASS_MAGE].push_back(RACE_BLOODELF);

    availableRaces[CLASS_WARLOCK].push_back(RACE_HUMAN);
    availableRaces[CLASS_WARLOCK].push_back(RACE_GNOME);
    availableRaces[CLASS_WARLOCK].push_back(RACE_UNDEAD_PLAYER);
    availableRaces[CLASS_WARLOCK].push_back(RACE_ORC);
    availableRaces[CLASS_WARLOCK].push_back(RACE_BLOODELF);

    availableRaces[CLASS_SHAMAN].push_back(RACE_DRAENEI);
    availableRaces[CLASS_SHAMAN].push_back(RACE_ORC);
    availableRaces[CLASS_SHAMAN].push_back(RACE_TAUREN);
    availableRaces[CLASS_SHAMAN].push_back(RACE_TROLL);

    availableRaces[CLASS_HUNTER].push_back(RACE_DWARF);
    availableRaces[CLASS_HUNTER].push_back(RACE_NIGHTELF);
    availableRaces[CLASS_HUNTER].push_back(RACE_DRAENEI);
    availableRaces[CLASS_HUNTER].push_back(RACE_ORC);
    availableRaces[CLASS_HUNTER].push_back(RACE_TAUREN);
    availableRaces[CLASS_HUNTER].push_back(RACE_TROLL);
    availableRaces[CLASS_HUNTER].push_back(RACE_BLOODELF);

    availableRaces[CLASS_DRUID].push_back(RACE_NIGHTELF);
    availableRaces[CLASS_DRUID].push_back(RACE_TAUREN);
}

static void FillRandomCustomizations(uint8 race, uint8 gender, WorldPackets::Array<UF::ChrCustomizationChoice, 250>& customizations)
{
    std::vector<ChrCustomizationOptionEntry const*> const* options = sDB2Manager.GetCustomiztionOptions(race, gender);
    if (!options)
        return;

    for (ChrCustomizationOptionEntry const* option : *options)
    {
        std::vector<ChrCustomizationChoiceEntry const*> const* choices = sDB2Manager.GetCustomiztionChoices(option->ID);
        if (!choices || choices->empty())
            continue;

        // only pick choices without additional requirements so that the bot always looks valid
        std::vector<ChrCustomizationChoiceEntry const*> usable;
        for (ChrCustomizationChoiceEntry const* choice : *choices)
            if (!choice->ChrCustomizationReqID)
                usable.push_back(choice);

        if (usable.empty())
            usable = *choices;

        ChrCustomizationChoiceEntry const* picked = usable[urand(0, usable.size() - 1)];

        UF::ChrCustomizationChoice choice;
        choice.ChrCustomizationOptionID = option->ID;
        choice.ChrCustomizationChoiceID = picked->ID;
        customizations.push_back(choice);
    }
}

bool RandomPlayerbotFactory::CreateRandomBot(uint8 cls)
{
    TC_LOG_DEBUG("playerbot", "Creating new random bot for class {}", cls);

    uint8 gender = rand() % 2 ? GENDER_MALE : GENDER_FEMALE;

    uint8 race = availableRaces[cls][urand(0, availableRaces[cls].size() - 1)];
    string name = CreateRandomBotName();
    if (name.empty())
        return false;

    std::string accountName;
    if (!AccountMgr::GetName(accountId, accountName))
        accountName = "rndbot";

    WorldSession* session = new WorldSession(accountId, std::move(accountName), 0, nullptr, SEC_PLAYER,
        uint8(sWorld->getIntConfig(CONFIG_EXPANSION)), 0, "", Minutes(0), LOCALE_enUS, 0, false);

    session->SetBotSession(true);

    Player* player = new Player(session);
    // Player::Create relies on the motion master being initialized exactly
    // like the core does it in HandleCharCreateOpcode.
    player->GetMotionMaster()->Initialize();

    WorldPackets::Character::CharacterCreateInfo cci;
    cci.Name = name;
    cci.Race = race;
    cci.Class = cls;
    cci.Sex = gender;
    FillRandomCustomizations(race, gender, cci.Customizations);

    if (!player->Create(sObjectMgr->GetGenerator<HighGuid::Player>().Generate(), &cci))
    {
        TC_LOG_ERROR("playerbot", "Unable to create random bot for account {} - name: \"{}\"; race: {}; class: {}",
                accountId, name.c_str(), race, cls);
        // the player is never added to a map; CleanupsBeforeDelete keeps the
        // Player destructor's ResetMap/grid accounting consistent
        player->CleanupsBeforeDelete();
        delete player;
        delete session;
        return false;
    }

    player->setCinematic(2);
    player->SetAtLoginFlag(AT_LOGIN_NONE);
    player->SaveToDB(true);

    TC_LOG_DEBUG("playerbot", "Random bot created for account {} - name: \"{}\"; race: {}; class: {}",
            accountId, name.c_str(), race, cls);

    // this player is never added to the world/map; run the same pre-delete
    // teardown the core uses before freeing it
    player->CleanupsBeforeDelete();
    delete player;
    delete session;
    return true;
}

string RandomPlayerbotFactory::CreateRandomBotName()
{
    QueryResult result = CharacterDatabase.Query("SELECT MAX(name_id) FROM ai_playerbot_names");
    if (!result)
    {
        TC_LOG_ERROR("playerbot",  "No more names left for random guilds");
        return "";
    }

    Field *fields = result->Fetch();
    uint32 maxId = fields[0].GetUInt32();

    uint32 id = urand(0, maxId);
    result = CharacterDatabase.PQuery("SELECT n.name FROM ai_playerbot_names n "
            "LEFT OUTER JOIN characters e ON e.name = n.name "
            "WHERE e.guid IS NULL AND n.name_id >= '{}' LIMIT 1", id);
    if (!result)
    {
        TC_LOG_ERROR("playerbot",  "No more names left for random bots");
        return "";
    }

	fields = result->Fetch();
    return fields[0].GetString();
}


void RandomPlayerbotFactory::CreateRandomBots()
{
    if (sPlayerbotAIConfig.deleteRandomBotAccounts)
    {
        TC_LOG_INFO("playerbot",  "Deleting random bot accounts...");
        QueryResult results = LoginDatabase.PQuery("SELECT id FROM account where username like '{}%'", sPlayerbotAIConfig.randomBotAccountPrefix.c_str());
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                sAccountMgr->DeleteAccount(fields[0].GetUInt32());
            } while (results->NextRow());
        }

        CharacterDatabase.Execute("DELETE FROM ai_playerbot_random_bots");
        TC_LOG_INFO("playerbot",  "Random bot accounts deleted");
    }

    for (int accountNumber = 0; accountNumber < sPlayerbotAIConfig.randomBotAccountCount; ++accountNumber)
    {
        ostringstream out; out << sPlayerbotAIConfig.randomBotAccountPrefix << accountNumber;
        string accountName = out.str();
        QueryResult results = LoginDatabase.PQuery("SELECT id FROM account where username = '{}'", accountName.c_str());
        if (results)
        {
            continue;
        }

        string password = "";
        for (int i = 0; i < 10; i++)
        {
            password += (char)urand('!', 'z');
        }
        sAccountMgr->CreateAccount(accountName, password, "playerbot");

        TC_LOG_DEBUG("playerbot",  "Account {} created for random bots", accountName.c_str());
    }

    LoginDatabase.PExecute("UPDATE account SET expansion = '{}' where username like '{}%'", 2, sPlayerbotAIConfig.randomBotAccountPrefix.c_str());

    int totalRandomBotChars = 0;
    for (int accountNumber = 0; accountNumber < sPlayerbotAIConfig.randomBotAccountCount; ++accountNumber)
    {
        ostringstream out; out << sPlayerbotAIConfig.randomBotAccountPrefix << accountNumber;
        string accountName = out.str();

        QueryResult results = LoginDatabase.PQuery("SELECT id FROM account where username = '{}'", accountName.c_str());
        if (!results)
            continue;

        Field* fields = results->Fetch();
        uint32 accountId = fields[0].GetUInt32();

        sPlayerbotAIConfig.randomBotAccounts.push_back(accountId);

        int count = sAccountMgr->GetCharactersCount(accountId);
        if (count >= 10)
        {
            totalRandomBotChars += count;
            continue;
        }

        RandomPlayerbotFactory factory(accountId);
        for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
        {
            if (cls != 10 && cls != CLASS_DEATH_KNIGHT)
                factory.CreateRandomBot(cls);
        }

        totalRandomBotChars += sAccountMgr->GetCharactersCount(accountId);
    }

    TC_LOG_INFO("playerbot",  "{} random bot accounts with {} characters available", sPlayerbotAIConfig.randomBotAccounts.size(), totalRandomBotChars);
}


void RandomPlayerbotFactory::CreateRandomGuilds()
{
    vector<uint32> randomBots;
    QueryResult results = LoginDatabase.PQuery("SELECT id FROM account where username like '{}%'", sPlayerbotAIConfig.randomBotAccountPrefix.c_str());
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 accountId = fields[0].GetUInt32();

            QueryResult results2 = CharacterDatabase.PQuery("SELECT guid FROM characters where account  = '{}'", accountId);
            if (results2)
            {
                do
                {
                    Field* fields = results2->Fetch();
                    uint32 guid = fields[0].GetUInt32();
                    randomBots.push_back(guid);
                } while (results2->NextRow());
            }

        } while (results->NextRow());
    }

    if (sPlayerbotAIConfig.deleteRandomBotGuilds)
    {
        TC_LOG_INFO("playerbot",  "Deleting random bot guilds...");
        for (vector<uint32>::iterator i = randomBots.begin(); i != randomBots.end(); ++i)
        {
            ObjectGuid leader = ObjectGuid::Create<HighGuid::Player>(*i);
            Guild* guild = sGuildMgr->GetGuildByLeader(leader);
            if (guild) guild->Disband();
        }
        TC_LOG_INFO("playerbot",  "Random bot guilds deleted");
    }

    int guildNumber = 0;
    vector<ObjectGuid> availableLeaders;
    for (vector<uint32>::iterator i = randomBots.begin(); i != randomBots.end(); ++i)
    {
        ObjectGuid leader = ObjectGuid::Create<HighGuid::Player>(*i);
        Guild* guild = sGuildMgr->GetGuildByLeader(leader);
        if (guild)
        {
            ++guildNumber;
            sPlayerbotAIConfig.randomBotGuilds.push_back(guild->GetId());
        }
        else
        {
            Player* player = ObjectAccessor::FindPlayer(leader);
            if (player)
                availableLeaders.push_back(leader);
        }
    }

    for (; guildNumber < sPlayerbotAIConfig.randomBotGuildCount; ++guildNumber)
    {
        string guildName = CreateRandomGuildName();
        if (guildName.empty())
            break;

        if (availableLeaders.empty())
        {
            TC_LOG_ERROR("playerbot",  "No leaders for random guilds available");
            break;
        }

        int index = urand(0, availableLeaders.size() - 1);
        ObjectGuid leader = availableLeaders[index];
        Player* player = ObjectAccessor::FindPlayer(leader);
        if (!player)
        {
            TC_LOG_ERROR("playerbot",  "Cannot find player for leader {}", leader.ToString());
            break;
        }

        Guild* guild = new Guild();
        if (!guild->Create(player, guildName))
        {
            TC_LOG_ERROR("playerbot",  "Error creating guild {}", guildName.c_str());
            break;
        }

        sGuildMgr->AddGuild(guild);
        sPlayerbotAIConfig.randomBotGuilds.push_back(guild->GetId());
    }

    TC_LOG_INFO("playerbot",  "{} random bot guilds available", guildNumber);
}

string RandomPlayerbotFactory::CreateRandomGuildName()
{
    QueryResult result = CharacterDatabase.Query("SELECT MAX(name_id) FROM ai_playerbot_guild_names");
    if (!result)
    {
        TC_LOG_ERROR("playerbot",  "No more names left for random guilds");
        return "";
    }

    Field *fields = result->Fetch();
    uint32 maxId = fields[0].GetUInt32();

    uint32 id = urand(0, maxId);
    result = CharacterDatabase.PQuery("SELECT n.name FROM ai_playerbot_guild_names n "
            "LEFT OUTER JOIN guild e ON e.name = n.name "
            "WHERE e.guildid IS NULL AND n.name_id >= '{}' LIMIT 1", id);
    if (!result)
    {
        TC_LOG_ERROR("playerbot",  "No more names left for random guilds");
        return "";
    }

    fields = result->Fetch();
    return fields[0].GetString();
}

