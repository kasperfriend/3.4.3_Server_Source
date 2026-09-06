#include "../pchdef.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "GuildTaskMgr.h"

#include "../../plugins/ahbot/AhBot.h"
#include "../../server/game/Guilds/GuildMgr.h"
#include "../../server/database/Database/DatabaseEnv.h"
#include "../../server/game/Mails/Mail.h"
#include "PlayerbotAI.h"

#include "../../plugins/ahbot/AhBotConfig.h"
#include "RandomItemMgr.h"

char * strstri (const char* str1, const char* str2);

enum GuildTaskType
{
    GUILD_TASK_TYPE_NONE = 0,
    GUILD_TASK_TYPE_ITEM = 1,
    GUILD_TASK_TYPE_KILL = 2
};

GuildTaskMgr::GuildTaskMgr()
{
}

GuildTaskMgr::~GuildTaskMgr()
{
}

void GuildTaskMgr::Update(Player* player, Player* guildMaster)
{
    if (!sPlayerbotAIConfig.guildTaskEnabled)
        return;

    uint32 guildId = guildMaster->GetGuildId();
    if (!guildId || !guildMaster->GetPlayerbotAI() || !guildMaster->GetGuild())
        return;

    if (!player->IsFriendlyTo(guildMaster))
        return;

    DenyReason reason = PLAYERBOT_DENY_NONE;
    PlayerbotSecurityLevel secLevel = guildMaster->GetPlayerbotAI()->GetSecurity()->LevelFor(player, &reason);
    if (secLevel == PLAYERBOT_SECURITY_DENY_ALL || (secLevel == PLAYERBOT_SECURITY_TALK && reason != PLAYERBOT_DENY_FAR))
    {
        TC_LOG_DEBUG("gtask",  "{} / {}: skipping guild task update - not enough security level, reason = {}",
                guildMaster->GetGuild()->GetName().c_str(), player->GetName().c_str(), reason);
        return;
    }

    uint32 owner = uint32(player->GetGUID().GetCounter());

    uint32 activeTask = GetTaskValue(owner, guildId, "activeTask");
    if (!activeTask)
    {
        SetTaskValue(owner, guildId, "killTask", 0, 0);
        SetTaskValue(owner, guildId, "itemTask", 0, 0);
        SetTaskValue(owner, guildId, "itemCount", 0, 0);
        SetTaskValue(owner, guildId, "killTask", 0, 0);
        SetTaskValue(owner, guildId, "killCount", 0, 0);
        SetTaskValue(owner, guildId, "payment", 0, 0);
        SetTaskValue(owner, guildId, "thanks", 1, 2 * sPlayerbotAIConfig.maxGuildTaskChangeTime);
        SetTaskValue(owner, guildId, "reward", 1, 2 * sPlayerbotAIConfig.maxGuildTaskChangeTime);

        uint32 task = CreateTask(owner, guildId);

        if (task == GUILD_TASK_TYPE_NONE)
        {
            TC_LOG_ERROR("gtask",  "{} / {}: error creating guild task",
                    guildMaster->GetGuild()->GetName().c_str(), player->GetName().c_str());
        }

        uint32 time = urand(sPlayerbotAIConfig.minGuildTaskChangeTime, sPlayerbotAIConfig.maxGuildTaskChangeTime);
        SetTaskValue(owner, guildId, "activeTask", task, time);
        SetTaskValue(owner, guildId, "advertisement", 1,
                urand(sPlayerbotAIConfig.minGuildTaskAdvertisementTime, sPlayerbotAIConfig.maxGuildTaskAdvertisementTime));

        TC_LOG_DEBUG("gtask",  "{} / {}: guild task {} is set for {} secs",
                guildMaster->GetGuild()->GetName().c_str(), player->GetName().c_str(),
                task, time);
        return;
    }

    uint32 advertisement = GetTaskValue(owner, guildId, "advertisement");
    if (!advertisement)
    {
        TC_LOG_DEBUG("gtask",  "{} / {}: sending advertisement",
                guildMaster->GetGuild()->GetName().c_str(), player->GetName().c_str());
        if (SendAdvertisement(owner, guildId))
        {
            SetTaskValue(owner, guildId, "advertisement", 1,
                    urand(sPlayerbotAIConfig.minGuildTaskAdvertisementTime, sPlayerbotAIConfig.maxGuildTaskAdvertisementTime));
        }
        else
        {
            TC_LOG_ERROR("gtask",  "{} / {}: error sending advertisement",
                    guildMaster->GetGuild()->GetName().c_str(), player->GetName().c_str());
        }
    }

    uint32 thanks = GetTaskValue(owner, guildId, "thanks");
    if (!thanks)
    {
        TC_LOG_DEBUG("gtask",  "{} / {}: sending thanks",
                guildMaster->GetGuild()->GetName().c_str(), player->GetName().c_str());
        if (SendThanks(owner, guildId))
        {
            SetTaskValue(owner, guildId, "thanks", 1, 2 * sPlayerbotAIConfig.maxGuildTaskChangeTime);
            SetTaskValue(owner, guildId, "payment", 0, 0);
        }
        else
        {
            TC_LOG_ERROR("gtask",  "{} / {}: error sending thanks",
                    guildMaster->GetGuild()->GetName().c_str(), player->GetName().c_str());
        }
    }

    uint32 reward = GetTaskValue(owner, guildId, "reward");
    if (!reward)
    {
        TC_LOG_DEBUG("gtask",  "{} / {}: sending reward",
                guildMaster->GetGuild()->GetName().c_str(), player->GetName().c_str());
        if (Reward(owner, guildId))
        {
            SetTaskValue(owner, guildId, "reward", 1, 2 * sPlayerbotAIConfig.maxGuildTaskChangeTime);
            SetTaskValue(owner, guildId, "payment", 0, 0);
        }
        else
        {
            TC_LOG_ERROR("gtask",  "{} / {}: error sending reward",
                    guildMaster->GetGuild()->GetName().c_str(), player->GetName().c_str());
        }
    }
}

uint32 GuildTaskMgr::CreateTask(uint32 owner, uint32 guildId)
{
    switch (urand(0, 1))
    {
    case 0:
        CreateItemTask(owner, guildId);
        return GUILD_TASK_TYPE_ITEM;
    default:
        CreateKillTask(owner, guildId);
        return GUILD_TASK_TYPE_KILL;
    }
}

bool GuildTaskMgr::CreateItemTask(uint32 owner, uint32 guildId)
{
    Player* player = ObjectAccessor::FindPlayerByLowGUID(owner);
    if (!player)
        return false;

    uint32 itemId = sRandomItemMgr.GetRandomItem(RANDOM_ITEM_GUILD_TASK);
    if (!itemId)
    {
        TC_LOG_ERROR("gtask",  "{} / {}: no items avaible for item task",
                sGuildMgr->GetGuildById(guildId)->GetName().c_str(), player->GetName().c_str());
        return false;
    }

    uint32 count = GetMaxItemTaskCount(itemId);

    TC_LOG_DEBUG("gtask",  "{} / {}: item task {} (x{})",
            sGuildMgr->GetGuildById(guildId)->GetName().c_str(), player->GetName().c_str(),
            itemId, count);

    SetTaskValue(owner, guildId, "itemCount", count, sPlayerbotAIConfig.maxGuildTaskChangeTime);
    SetTaskValue(owner, guildId, "itemTask", itemId, sPlayerbotAIConfig.maxGuildTaskChangeTime);
    return true;
}

bool GuildTaskMgr::CreateKillTask(uint32 owner, uint32 guildId)
{
    Player* player = ObjectAccessor::FindPlayerByLowGUID(owner);
    if (!player)
        return false;

    CreatureClassifications rank = !urand(0, 2) ? CreatureClassifications::RareElite : CreatureClassifications::Rare;
    vector<uint32> ids;
    CreatureTemplateContainer const& creatureTemplateContainer = sObjectMgr->GetCreatureTemplates();
    for (CreatureTemplateContainer::const_iterator i = creatureTemplateContainer.begin(); i != creatureTemplateContainer.end(); ++i)
    {
        CreatureTemplate const& co = i->second;
        if (co.Classification != rank)
            continue;

        CreatureDifficulty const* creatureDifficulty = co.GetDifficulty(DIFFICULTY_NONE);
        if (!creatureDifficulty || creatureDifficulty->MaxLevel > player->GetLevel() + 4 || creatureDifficulty->MinLevel < player->GetLevel() - 3)
            continue;

        if (co.Name.find("UNUSED") != string::npos)
            continue;

        ids.push_back(i->first);
    }

    if (ids.empty())
    {
        TC_LOG_ERROR("gtask",  "{} / {}: no rare creatures available for kill task",
                sGuildMgr->GetGuildById(guildId)->GetName().c_str(), player->GetName().c_str());
        return false;
    }

    uint32 index = urand(0, ids.size() - 1);
    uint32 creatureId = ids[index];

    TC_LOG_DEBUG("gtask",  "{} / {}: kill task {}",
            sGuildMgr->GetGuildById(guildId)->GetName().c_str(), player->GetName().c_str(),
            creatureId);

    SetTaskValue(owner, guildId, "killTask", creatureId, sPlayerbotAIConfig.maxGuildTaskChangeTime);
    return true;
}

bool GuildTaskMgr::SendAdvertisement(uint32 owner, uint32 guildId)
{
    Guild *guild = sGuildMgr->GetGuildById(guildId);
    if (!guild)
        return false;

    Player* player = ObjectAccessor::FindPlayerByLowGUID(owner);
    if (!player)
        return false;

    Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());
    if (!leader)
        return false;

    uint32 validIn;
    uint32 itemTask = GetTaskValue(owner, guildId, "itemTask", &validIn);
    if (itemTask)
        return SendItemAdvertisement(itemTask, owner, guildId, validIn);

    uint32 killTask = GetTaskValue(owner, guildId, "killTask", &validIn);
    if (killTask)
        return SendKillAdvertisement(killTask, owner, guildId, validIn);

    return false;
}

string formatTime(uint32 secs)
{
    ostringstream out;
    if (secs < 3600)
    {
        out << secs / 60 << " min";
    }
    else if (secs < 7200)
    {
        out << "1 hr " << (secs - 3600) / 60 << " min";
    }
    else if (secs < 3600 * 24)
    {
        out << secs / 3600 << " hr";
    } else
    {
        out << secs / 3600 / 24 << " days";
    }

    return out.str();
}

bool GuildTaskMgr::SendItemAdvertisement(uint32 itemId, uint32 owner, uint32 guildId, uint32 validIn)
{
    Guild *guild = sGuildMgr->GetGuildById(guildId);
    if (!guild) return false;
    Player* player = ObjectAccessor::FindPlayerByLowGUID(owner);
    if (!player) return false;
    Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());
    if (!leader) return false;

    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return false;

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    ostringstream body;
    body << "Hello, " << player->GetName() << ",\n";
    body << "\n";
    body << "We are in a great need of " << proto->GetDefaultLocaleName() << ". If you could sell us ";
    uint32 count = GetTaskValue(owner, guildId, "itemCount");
    if (count > 1)
        body << "at least " << count << " of them ";
    else
        body << "some ";
    body << "we'd really appreciate that and pay a high price.\n";
    body << "The task will expire in " << formatTime(validIn) << "\n";
    body << "\n";
    body << "Best Regards,\n";
    body << guild->GetName() << "\n";
    body << leader->GetName() << "\n";

    ostringstream subject;
    subject << "Guild Task: " << proto->GetDefaultLocaleName();
    MailDraft(subject.str(), body.str()).SendMailTo(trans, MailReceiver(player), MailSender(leader));
    CharacterDatabase.CommitTransaction(trans);

    return true;
}


bool GuildTaskMgr::SendKillAdvertisement(uint32 creatureId, uint32 owner, uint32 guildId, uint32 validIn)
{
    Guild *guild = sGuildMgr->GetGuildById(guildId);
    if (!guild) return false;
    Player* player = ObjectAccessor::FindPlayerByLowGUID(owner);
    if (!player) return false;
    Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());
    if (!leader) return false;

    CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(creatureId);
    if (!proto)
        return false;

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    ostringstream body;
    body << "Hello, " << player->GetName() << ",\n";
    body << "\n";
    body << "As you probably know " << proto->Name << " is wanted dead for the crimes it did against our guild. If you should kill it ";
    body << "we'd really appreciate that.\n";
    body << "The task will expire in " << formatTime(validIn) << "\n";
    body << "\n";
    body << "Best Regards,\n";
    body << guild->GetName() << "\n";
    body << leader->GetName() << "\n";

    ostringstream subject;
    subject << "Guild Task: " << proto->Name;
    MailDraft(subject.str(), body.str()).SendMailTo(trans, MailReceiver(player), MailSender(leader));
    CharacterDatabase.CommitTransaction(trans);

    return true;
}

bool GuildTaskMgr::SendThanks(uint32 owner, uint32 guildId)
{
    Guild *guild = sGuildMgr->GetGuildById(guildId);
    if (!guild)
        return false;

    Player* player = ObjectAccessor::FindPlayerByLowGUID(owner);
    if (!player)
        return false;

    Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());
    if (!leader)
        return false;

    uint32 itemTask = GetTaskValue(owner, guildId, "itemTask");
    if (itemTask)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemTask);
        if (!proto)
            return false;

        CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
        ostringstream body;
        body << "Hello, " << player->GetName() << ",\n";
        body << "\n";
        body << "One of our guild members wishes to thank you for the " << proto->GetDefaultLocaleName() << "! If we have another ";
        uint32 count = GetTaskValue(owner, guildId, "itemCount");
        body << count << " of them that would help us tremendously.\n";
        body << "\n";
        body << "Thanks again,\n";
        body << guild->GetName() << "\n";
        body << leader->GetName() << "\n";

        MailDraft("Thank You", body.str()).
                AddMoney(GetTaskValue(owner, guildId, "payment")).
                SendMailTo(trans, MailReceiver(player), MailSender(leader));

        CharacterDatabase.CommitTransaction(trans);

        return true;
    }

    return false;
}

uint32 GuildTaskMgr::GetMaxItemTaskCount(uint32 itemId)
{
    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
    if (!proto)
        return 0;

    if (proto->GetQuality() < ITEM_QUALITY_RARE && proto->GetMaxStackSize() && proto->GetMaxStackSize() > 1)
        return proto->GetMaxStackSize();
    else if (proto->GetMaxStackSize() && proto->GetMaxStackSize() > 1)
        return urand(1 + proto->GetMaxStackSize() / 4, proto->GetMaxStackSize());

    return 1;
}

bool GuildTaskMgr::IsGuildTaskItem(uint32 itemId, uint32 guildId)
{
    uint32 value = 0;

    QueryResult results = CharacterDatabase.PQuery(
            "select `value`, `time`, validIn from ai_playerbot_guild_tasks where `value` = '{}' and guildid = '{}' and `type` = 'itemTask'",
            itemId, guildId);

    if (results)
    {
        Field* fields = results->Fetch();
        value = fields[0].GetUInt32();
        uint32 lastChangeTime = fields[1].GetUInt32();
        uint32 validIn = fields[2].GetUInt32();
        if ((time(0) - lastChangeTime) >= validIn)
            value = 0;
    }

    return value;
}

map<uint32,uint32> GuildTaskMgr::GetTaskValues(uint32 owner, string type, uint32 *validIn /* = NULL */)
{
    map<uint32,uint32> result;

    QueryResult results = CharacterDatabase.PQuery(
            "select `value`, `time`, validIn, guildid from ai_playerbot_guild_tasks where owner = '{}' and `type` = '{}'",
            owner, type.c_str());

    if (!results)
        return result;

    do
    {
        Field* fields = results->Fetch();
        uint32 value = fields[0].GetUInt32();
        uint32 lastChangeTime = fields[1].GetUInt32();
        uint32 secs = fields[2].GetUInt32();
        uint32 guildId = fields[3].GetUInt32();
        if ((time(0) - lastChangeTime) >= secs)
            value = 0;

        result[guildId] = value;

    } while (results->NextRow());

    return result;
}

uint32 GuildTaskMgr::GetTaskValue(uint32 owner, uint32 guildId, string type, uint32 *validIn /* = NULL */)
{
    uint32 value = 0;

    QueryResult results = CharacterDatabase.PQuery(
            "select `value`, `time`, validIn from ai_playerbot_guild_tasks where owner = '{}' and guildid = '{}' and `type` = '{}'",
            owner, guildId, type.c_str());

    if (results)
    {
        Field* fields = results->Fetch();
        value = fields[0].GetUInt32();
        uint32 lastChangeTime = fields[1].GetUInt32();
        uint32 secs = fields[2].GetUInt32();
        if ((time(0) - lastChangeTime) >= secs)
            value = 0;

        if (validIn) *validIn = secs;
    }

    return value;
}

uint32 GuildTaskMgr::SetTaskValue(uint32 owner, uint32 guildId, string type, uint32 value, uint32 validIn)
{
    CharacterDatabase.PExecute("delete from ai_playerbot_guild_tasks where owner = '{}' and guildid = '{}' and `type` = '{}'",
            owner, guildId, type.c_str());
    if (value)
    {
        CharacterDatabase.PExecute(
                "insert into ai_playerbot_guild_tasks (owner, guildid, `time`, validIn, `type`, `value`) values ('{}', '{}', '{}', '{}', '{}', '{}')",
                owner, guildId, (uint32)time(0), validIn, type.c_str(), value);
    }

    return value;
}

bool GuildTaskMgr::HandleConsoleCommand(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.guildTaskEnabled)
    {
        TC_LOG_ERROR("gtask",  "Guild task system is currently disabled!");
        return false;
    }

    if (!args || !*args)
    {
        TC_LOG_ERROR("gtask",  "Usage: gtask stats/reset");
        return false;
    }

    string cmd = args;

    if (cmd == "reset")
    {
        CharacterDatabase.PExecute("delete from ai_playerbot_guild_tasks");
        TC_LOG_INFO("gtask",  "Guild tasks were reset for all players");
        return true;
    }

    if (cmd == "stats")
    {
        TC_LOG_INFO("gtask",  "Usage: gtask stats <player name>");
        return true;
    }

    if (cmd.find("stats ") != string::npos)
    {
        string charName = cmd.substr(cmd.find("stats ") + 6);
        ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(charName);
        if (!guid)
        {
            TC_LOG_ERROR("gtask",  "Player {} not found", charName.c_str());
            return false;
        }

        uint32 owner = uint32(guid.GetCounter());

        QueryResult result = CharacterDatabase.PQuery(
                "select `value`, `time`, validIn, guildid, `type` from ai_playerbot_guild_tasks where owner = '{}' order by guildid, `type`",
                owner);

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 value = fields[0].GetUInt32();
                uint32 lastChangeTime = fields[1].GetUInt32();
                uint32 validIn = fields[2].GetUInt32();
                if ((time(0) - lastChangeTime) >= validIn)
                    value = 0;
                uint32 guildId = fields[3].GetUInt32();
                string type = fields[4].GetString();

                Guild *guild = sGuildMgr->GetGuildById(guildId);
                if (!guild)
                    continue;

                ostringstream name;
                name << value;
                if (type == "killTask")
                {
                    CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(value);
                    string rank = proto->Classification == CreatureClassifications::Rare ? "rare" : "elite";
                    if (proto) name << " (" << proto->Name << "," << rank << ")";
                }
                else if (type == "itemTask")
                {
                    ItemTemplate const* proto = sObjectMgr->GetItemTemplate(value);
                    string rank = proto->GetQuality() == ITEM_QUALITY_UNCOMMON ? "uncommon" : "rare";
                    if (proto) name << " (" << proto->GetDefaultLocaleName() << "," << rank << ")";
                }

                TC_LOG_INFO("gtask",  "Player '{}' Guild '{}' {}={} ({} secs)",
                        charName.c_str(), guild->GetName().c_str(),
                        type.c_str(), name.str().c_str(), validIn);

            } while (result->NextRow());

            Field* fields = result->Fetch();
        }

        return true;
    }

    if (cmd == "reward")
    {
        TC_LOG_INFO("gtask",  "Usage: gtask reward <player name>");
        return true;
    }

    if (cmd.find("reward ") != string::npos)
    {
        string charName = cmd.substr(cmd.find("reward ") + 7);
        ObjectGuid guid = sCharacterCache->GetCharacterGuidByName(charName);
        if (!guid)
        {
            TC_LOG_ERROR("gtask",  "Player {} not found", charName.c_str());
            return false;
        }

        uint32 owner = uint32(guid.GetCounter());
        QueryResult result = CharacterDatabase.PQuery(
                "select distinct guildid from ai_playerbot_guild_tasks where owner = '{}'",
                owner);

        if (result)
        {
            do
            {
                Field* fields = result->Fetch();
                uint32 guildId = fields[0].GetUInt32();
                Guild *guild = sGuildMgr->GetGuildById(guildId);
                if (!guild)
                    continue;

                sGuildTaskMgr.Reward(owner, guildId);
            } while (result->NextRow());

            Field* fields = result->Fetch();
            return true;
        }
    }

    return false;
}

void GuildTaskMgr::CheckItemTask(uint32 itemId, uint32 obtained, Player* ownerPlayer, Player* bot, bool byMail)
{
    uint32 guildId = bot->GetGuildId();
    if (!guildId)
        return;

    uint32 owner = uint32(ownerPlayer->GetGUID().GetCounter());

    TC_LOG_DEBUG("gtask",  "{} / {}: checking guild task",
            bot->GetGuild()->GetName().c_str(), ownerPlayer->GetName().c_str());

    uint32 itemTask = GetTaskValue(owner, guildId, "itemTask");
    if (itemTask != itemId)
    {
        TC_LOG_DEBUG("gtask",  "{} / {}: item {} is not guild task item ({})",
                bot->GetGuild()->GetName().c_str(), ownerPlayer->GetName().c_str(),
                itemId, itemTask);
        return;
    }

    if (byMail)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            return;

        uint32 money = GetTaskValue(owner, guildId, "payment");
        SetTaskValue(owner, guildId, "payment", money + auctionbot.GetBuyPrice(proto) * obtained,
                sPlayerbotAIConfig.maxGuildTaskRewardTime);
    }

    uint32 count = GetTaskValue(owner, guildId, "itemCount");
    if (obtained >= count)
    {
        TC_LOG_DEBUG("gtask",  "{} / {}: guild task complete",
                bot->GetGuild()->GetName().c_str(), ownerPlayer->GetName().c_str());
        SetTaskValue(owner, guildId, "reward", 1,
                urand(sPlayerbotAIConfig.minGuildTaskRewardTime, sPlayerbotAIConfig.maxGuildTaskRewardTime));
        ChatHandler(ownerPlayer->GetSession()).PSendSysMessage("You have completed a guild task");
    }
    else
    {
        TC_LOG_DEBUG("gtask",  "{} / {}: guild task progress",
                bot->GetGuild()->GetName().c_str(), ownerPlayer->GetName().c_str());
        SetTaskValue(owner, guildId, "itemCount", count - obtained, sPlayerbotAIConfig.maxGuildTaskChangeTime);
        SetTaskValue(owner, guildId, "thanks", 1,
                urand(sPlayerbotAIConfig.minGuildTaskRewardTime, sPlayerbotAIConfig.maxGuildTaskRewardTime));
    }
}

bool GuildTaskMgr::Reward(uint32 owner, uint32 guildId)
{
    Guild *guild = sGuildMgr->GetGuildById(guildId);
    if (!guild)
        return false;

    Player* player = ObjectAccessor::FindPlayerByLowGUID(owner);
    if (!player)
        return false;

    Player* leader = ObjectAccessor::FindPlayer(guild->GetLeaderGUID());
    if (!leader)
        return false;

    uint32 itemTask = GetTaskValue(owner, guildId, "itemTask");
    uint32 killTask = GetTaskValue(owner, guildId, "killTask");
    if (!itemTask && !killTask)
        return false;

    ostringstream body;
    body << "Hello, " << player->GetName() << ",\n";
    body << "\n";

    RandomItemType rewardType;
    if (itemTask)
    {
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemTask);
        if (!proto)
            return false;

        body << "We wish to thank you for the " << proto->GetDefaultLocaleName() << " you provided so kindly. We really appreciate this and may this small gift bring you our thanks!\n";
        body << "\n";
        body << "Many thanks,\n";
        body << guild->GetName() << "\n";
        body << leader->GetName() << "\n";
        rewardType = RANDOM_ITEM_GUILD_TASK_REWARD_EQUIP;
    }
    else if (killTask)
    {
        CreatureTemplate const* proto = sObjectMgr->GetCreatureTemplate(killTask);
        if (!proto)
            return false;

        body << "We wish to thank you for the " << proto->Name << " you've killed recently. We really appreciate this and may this small gift bring you our thanks!\n";
        body << "\n";
        body << "Many thanks,\n";
        body << guild->GetName() << "\n";
        body << leader->GetName() << "\n";
        rewardType = RANDOM_ITEM_GUILD_TASK_REWARD_TRADE;
    }

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();
    MailDraft draft("Thank You", body.str());

    uint32 itemId = sRandomItemMgr.GetRandomItem(rewardType);
    if (itemId)
    {
        Item* item = Item::CreateItem(itemId, 1, ItemContext::NONE, leader);
        item->SaveToDB(trans);
        draft.AddItem(item);
    }

    draft.AddMoney(GetTaskValue(owner, guildId, "payment")).SendMailTo(trans, MailReceiver(player), MailSender(leader));
    CharacterDatabase.CommitTransaction(trans);

    SetTaskValue(owner, guildId, "activeTask", 0, 0);
    return true;
}

void GuildTaskMgr::CheckKillTask(Player* player, Unit* victim)
{
    uint32 owner = player->GetGUID().GetCounter();
    Creature* creature = victim->ToCreature();
    if (!creature)
        return;

    map<uint32,uint32> tasks = GetTaskValues(owner, "killTask");
    for (map<uint32,uint32>::iterator i = tasks.begin(); i != tasks.end(); ++i)
    {
        uint32 guildId = i->first;
        uint32 value = i->second;
        Guild* guild = sGuildMgr->GetGuildById(guildId);

        if (value != creature->GetCreatureTemplate()->Entry)
            continue;

        TC_LOG_DEBUG("gtask",  "{} / {}: guild task complete",
                guild->GetName().c_str(), player->GetName().c_str());
        SetTaskValue(owner, guildId, "reward", 1,
                urand(sPlayerbotAIConfig.minGuildTaskRewardTime, sPlayerbotAIConfig.maxGuildTaskRewardTime));
        ChatHandler(player->GetSession()).PSendSysMessage("You have completed a guild task");
    }
}
