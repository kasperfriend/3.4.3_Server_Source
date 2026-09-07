#include "../pchdef.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotFactory.h"
#include "../../server/database/Database/DatabaseEnv.h"
#include "PlayerbotAI.h"
#include "AiFactory.h"
#include "Maps/MapManager.h"
#include "PlayerbotCommandServer.h"
#include "GuildTaskMgr.h"

RandomPlayerbotMgr::RandomPlayerbotMgr() : PlayerbotHolder(), processTicks(0)
{
    sPlayerbotCommandServer.Start();
}

RandomPlayerbotMgr::~RandomPlayerbotMgr()
{
}

void RandomPlayerbotMgr::UpdateAIInternal(uint32 elapsed)
{
    SetNextCheckDelay(sPlayerbotAIConfig.randomBotUpdateInterval * 1000);

    if (!sPlayerbotAIConfig.randomBotAutologin || !sPlayerbotAIConfig.enabled)
        return;

    TC_LOG_INFO("playerbot",  "Processing random bots...");

    int maxAllowedBotCount = GetEventValue(0, "bot_count");
    if (!maxAllowedBotCount)
    {
        maxAllowedBotCount = urand(sPlayerbotAIConfig.minRandomBots, sPlayerbotAIConfig.maxRandomBots);
        SetEventValue(0, "bot_count", maxAllowedBotCount,
                urand(sPlayerbotAIConfig.randomBotCountChangeMinInterval, sPlayerbotAIConfig.randomBotCountChangeMaxInterval));
    }

    list<uint32> bots = GetBots();
    int botCount = bots.size();
    int randomBotsPerInterval = (int)urand(sPlayerbotAIConfig.minRandomBotsPerInterval, sPlayerbotAIConfig.maxRandomBotsPerInterval);
    if (!processTicks)
    {
        // log the population in gradually over ~10 ticks instead of all in a
        // single one: every ProcessBot loads a character on the world thread,
        // and all-at-once stalls long enough for the FreezeDetector to kill
        // the server whenever RandomBotLoginAtStartup is on
        if (sPlayerbotAIConfig.randomBotLoginAtStartup && botCount / 10 > randomBotsPerInterval)
            randomBotsPerInterval = botCount / 10;
    }
    // processTicks was initialised but never incremented, making the startup
    // branch above fire on EVERY tick - every tick processed the whole bot list
    if (processTicks < 1000)
        ++processTicks;

    // resolve the free bots for this tick ONCE: AddRandomBot used to rescan the
    // characters of every random account per bot added, i.e. accountCount x
    // addedBots character queries in a single world tick (a FreezeDetector trip
    // at startup with a large MaxRandomBots)
    vector<uint32> freeAllianceBots = GetFreeBots(true);
    vector<uint32> freeHordeBots = GetFreeBots(false);

    int addsThisTick = 0;
    while (botCount++ < maxAllowedBotCount && addsThisTick < 200)
    {
        bool alliance = botCount % 2;
        uint32 bot = AddRandomBot(alliance ? freeAllianceBots : freeHordeBots);
        if (bot)
        {
            bots.push_back(bot);
            ++addsThisTick;
        }
        else break;
    }

    int botProcessed = 0;
    for (list<uint32>::iterator i = bots.begin(); i != bots.end(); ++i)
    {
        uint32 bot = *i;
        if (ProcessBot(bot))
            botProcessed++;

        if (botProcessed >= randomBotsPerInterval)
            break;
    }

    TC_LOG_INFO("playerbot",  "{} bots processed. Next check in {} seconds",
            botProcessed, sPlayerbotAIConfig.randomBotUpdateInterval);

    PrintStats();
}

uint32 RandomPlayerbotMgr::AddRandomBot(vector<uint32>& bots)
{
    if (bots.empty())
        return 0;

    int index = urand(0, bots.size() - 1);
    uint32 bot = bots[index];
    // the chosen bot is no longer free - do not offer it again this tick
    bots.erase(bots.begin() + index);
    SetEventValue(bot, "add", 1, urand(sPlayerbotAIConfig.minRandomBotInWorldTime, sPlayerbotAIConfig.maxRandomBotInWorldTime));
    uint32 randomTime = 30 + urand(sPlayerbotAIConfig.randomBotUpdateInterval, sPlayerbotAIConfig.randomBotUpdateInterval * 3);
    ScheduleRandomize(bot, randomTime);
    TC_LOG_DEBUG("playerbot",  "Random bot {} added", bot);
    return bot;
}

void RandomPlayerbotMgr::ScheduleRandomize(uint32 bot, uint32 time)
{
    SetEventValue(bot, "randomize", 1, time);
    SetEventValue(bot, "logout", 1, time + 30 + urand(sPlayerbotAIConfig.randomBotUpdateInterval, sPlayerbotAIConfig.randomBotUpdateInterval * 3));
}

void RandomPlayerbotMgr::ScheduleTeleport(uint32 bot)
{
    SetEventValue(bot, "teleport", 1, 60 + urand(sPlayerbotAIConfig.randomBotUpdateInterval, sPlayerbotAIConfig.randomBotUpdateInterval * 3));
}

bool RandomPlayerbotMgr::ProcessBot(uint32 bot)
{
    uint32 isValid = GetEventValue(bot, "add");
    if (!isValid)
    {
		Player* player = GetPlayerBot(ObjectGuid::Create<HighGuid::Player>(bot));
		if (!player || !player->GetGroup())
		{
			TC_LOG_INFO("playerbot",  "Bot {} expired", bot);
			SetEventValue(bot, "add", 0, 0);
		}
        return true;
    }

    if (!GetPlayerBot(ObjectGuid::Create<HighGuid::Player>(bot)))
    {
        AddPlayerBot(ObjectGuid::Create<HighGuid::Player>(bot), 0);
        if (!GetEventValue(bot, "online"))
        {
            SetEventValue(bot, "online", 1, sPlayerbotAIConfig.minRandomBotInWorldTime);
        }
        return true;
    }

    Player* player = GetPlayerBot(ObjectGuid::Create<HighGuid::Player>(bot));
    if (!player)
        return false;

    PlayerbotAI* ai = player->GetPlayerbotAI();
    if (!ai)
        return false;

    if (player->GetGroup())
    {
        TC_LOG_INFO("playerbot",  "Skipping bot {} as it is in group", bot);
        return false;
    }

    if (player->isDead())
    {
        if (!GetEventValue(bot, "dead"))
        {
            TC_LOG_INFO("playerbot",  "Setting dead flag for bot {}", bot);
            uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotReviveTime, sPlayerbotAIConfig.maxRandomBotReviveTime);
            SetEventValue(bot, "dead", 1, randomTime);
            // guard the -60 offset: a revive time below 60 would wrap the uint32
            // validIn around 4 billion seconds and the bot would never revive
            SetEventValue(bot, "revive", 1, randomTime > 60 ? randomTime - 60 : 1);
            return false;
        }

        if (!GetEventValue(bot, "revive"))
        {
            TC_LOG_INFO("playerbot",  "Reviving dead bot {}", bot);
            SetEventValue(bot, "dead", 0, 0);
            SetEventValue(bot, "revive", 0, 0);
            RandomTeleport(player, player->GetMapId(), player->GetPositionX(), player->GetPositionY(), player->GetPositionZ());
            return true;
        }

        return false;
    }

    if (player->GetGuild() && player->GetGuild()->GetLeaderGUID() == player->GetGUID())
    {
        // "players" holds raw pointers to live real players; skip anything that
        // is gone or still loading so guild task updates never touch stale players
        for (vector<Player*>::iterator i = players.begin(); i != players.end(); ++i)
        {
            if (!*i || !(*i)->IsInWorld() || (*i)->GetSession()->IsBotSession())
                continue;

            sGuildTaskMgr.Update(*i, player);
        }
    }

    uint32 randomize = GetEventValue(bot, "randomize");
    if (!randomize)
    {
        TC_LOG_INFO("playerbot",  "Randomizing bot {}", bot);
        Randomize(player);
        uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
        ScheduleRandomize(bot, randomTime);
        return true;
    }

    uint32 logout = GetEventValue(bot, "logout");
    if (!logout)
    {
        TC_LOG_INFO("playerbot",  "Logging out bot {}", bot);
        LogoutPlayerBot(ObjectGuid::Create<HighGuid::Player>(bot));
        SetEventValue(bot, "logout", 1, sPlayerbotAIConfig.maxRandomBotInWorldTime);
        return true;
    }

    uint32 teleport = GetEventValue(bot, "teleport");
    if (!teleport)
    {
        TC_LOG_INFO("playerbot",  "Random teleporting bot {}", bot);
        RandomTeleportForLevel(ai->GetBot());
        SetEventValue(bot, "teleport", 1, sPlayerbotAIConfig.maxRandomBotInWorldTime);
        return true;
    }

    return false;
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot, vector<WorldLocation> &locs)
{
    if (bot->IsBeingTeleported())
        return;

    if (locs.empty())
    {
        TC_LOG_ERROR("playerbot",  "Cannot teleport bot {} - no locations available", bot->GetName().c_str());
        return;
    }

    for (int attemtps = 0; attemtps < 10; ++attemtps)
    {
        int index = urand(0, locs.size() - 1);
        WorldLocation loc = locs[index];
        float x = loc.m_positionX + urand(0, sPlayerbotAIConfig.grindDistance) - sPlayerbotAIConfig.grindDistance / 2;
        float y = loc.m_positionY + urand(0, sPlayerbotAIConfig.grindDistance) - sPlayerbotAIConfig.grindDistance / 2;
        float z = loc.m_positionZ;

        Map* map = sMapMgr->CreateMap(loc.GetMapId(), bot);
        if (!map)
            continue;

        if (map->IsInWater(bot->GetPhaseShift(), x, y, z))
            continue;

        uint32 areaId = map->GetAreaId(bot->GetPhaseShift(), x, y, z);
        if (!areaId)
            continue;

		AreaTableEntry const* area = sAreaTableStore.LookupEntry(areaId);
        if (!area)
            continue;

        float ground = map->GetHeight(bot->GetPhaseShift(), x, y, z + 0.5f);
        if (ground <= INVALID_HEIGHT)
            continue;

        z = 0.05f + ground;
        TC_LOG_INFO("playerbot",  "Random teleporting bot {} to {} {},{},{} (1/{} locations)",
                bot->GetName().c_str(), area->AreaName[LOCALE_enUS], x, y, z, locs.size());

        bot->GetMotionMaster()->Clear();
        bot->TeleportTo(loc.GetMapId(), x, y, z, 0);
        return;
    }

    TC_LOG_ERROR("playerbot",  "Cannot teleport bot {} - no locations available", bot->GetName().c_str());
}

void RandomPlayerbotMgr::RandomTeleportForLevel(Player* bot)
{
    TC_LOG_INFO("playerbot",  "Preparing location to random teleporting bot {} for level {}", bot->GetName().c_str(), bot->GetLevel());

    if (locsPerLevelCache[bot->GetLevel()].empty()) {
        // creature levels live in creature_template_difficulty (DifficultyID 0 = DIFFICULTY_NONE);
        // creature_template has no minlevel/maxlevel columns in this schema - querying them
        // raises ER_BAD_FIELD_ERROR which ABORTs the server
        QueryResult results = WorldDatabase.PQuery("select map, position_x, position_y, position_z "
            "from (select map, position_x, position_y, position_z, avg(d.MaxLevel), avg(d.MinLevel), "
            "{} - (avg(d.MaxLevel) + avg(d.MinLevel)) / 2 delta "
            "from creature c inner join creature_template_difficulty d on d.Entry = c.id and d.DifficultyID = 0 group by d.Entry) q "
            "where delta >= 0 and delta <= {} and map in ({}) and not exists ( "
            "select map, position_x, position_y, position_z from "
            "("
            "select map, c.position_x, c.position_y, c.position_z, avg(d.MaxLevel), avg(d.MinLevel), "
            "{} - (avg(d.MaxLevel) + avg(d.MinLevel)) / 2 delta "
            "from creature c "
            "inner join creature_template_difficulty d on d.Entry = c.id and d.DifficultyID = 0 group by d.Entry "
            ") q1 "
            "where delta > {} and q1.map = q.map "
            "and sqrt("
            "(q1.position_x - q.position_x)*(q1.position_x - q.position_x) +"
            "(q1.position_y - q.position_y)*(q1.position_y - q.position_y) +"
            "(q1.position_z - q.position_z)*(q1.position_z - q.position_z)"
            ") < {})",
            bot->GetLevel(),
            sPlayerbotAIConfig.randomBotTeleLevel,
            sPlayerbotAIConfig.randomBotMapsAsString.c_str(),
            bot->GetLevel(),
            sPlayerbotAIConfig.randomBotTeleLevel,
            (uint32)sPlayerbotAIConfig.sightDistance
            );
        if (results)
        {
            do
            {
                Field* fields = results->Fetch();
                uint16 mapId = fields[0].GetUInt16();
                float x = fields[1].GetFloat();
                float y = fields[2].GetFloat();
                float z = fields[3].GetFloat();
                WorldLocation loc(mapId, x, y, z, 0);
                locsPerLevelCache[bot->GetLevel()].push_back(loc);
            } while (results->NextRow());
        }
    }

    RandomTeleport(bot, locsPerLevelCache[bot->GetLevel()]);
}

void RandomPlayerbotMgr::RandomTeleport(Player* bot, uint16 mapId, float teleX, float teleY, float teleZ)
{
    TC_LOG_INFO("playerbot",  "Preparing location to random teleporting bot {}", bot->GetName().c_str());

    vector<WorldLocation> locs;
    QueryResult results = WorldDatabase.PQuery("select position_x, position_y, position_z from creature where map = '{}' and abs(position_x - '{}') < '{}' and abs(position_y - '{}') < '{}'",
            mapId, teleX, sPlayerbotAIConfig.randomBotTeleportDistance / 2, teleY, sPlayerbotAIConfig.randomBotTeleportDistance / 2);
    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            float x = fields[0].GetFloat();
            float y = fields[1].GetFloat();
            float z = fields[2].GetFloat();
            WorldLocation loc(mapId, x, y, z, 0);
            locs.push_back(loc);
        } while (results->NextRow());
    }

    RandomTeleport(bot, locs);
    Refresh(bot);
}

void RandomPlayerbotMgr::Randomize(Player* bot)
{
    if (bot->GetLevel() == 1)
        RandomizeFirst(bot);
    else
        IncreaseLevel(bot);
}

void RandomPlayerbotMgr::IncreaseLevel(Player* bot)
{
    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    uint32 level = min((uint32)(bot->GetLevel() + 1), maxLevel);
    PlayerbotFactory factory(bot, level);
    if (bot->GetGuildId())
        factory.Refresh();
    else
        factory.Randomize();
    RandomTeleportForLevel(bot);
}

void RandomPlayerbotMgr::RandomizeFirst(Player* bot)
{
    uint32 maxLevel = sPlayerbotAIConfig.randomBotMaxLevel;
    if (maxLevel > sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL))
        maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    if (sPlayerbotAIConfig.randomBotMaps.empty())
    {
        TC_LOG_ERROR("playerbot", "Cannot randomize bot {} - AiPlayerbot.RandomBotMaps is empty", bot->GetName());
        return;
    }

    for (int attempt = 0; attempt < 10; ++attempt)
    {
        int index = urand(0, sPlayerbotAIConfig.randomBotMaps.size() - 1);
        uint16 mapId = sPlayerbotAIConfig.randomBotMaps[index];

        vector<GameTele const*> locs;
        GameTeleContainer const & teleMap = sObjectMgr->GetGameTeleMap();
        for(GameTeleContainer::const_iterator itr = teleMap.begin(); itr != teleMap.end(); ++itr)
        {
            GameTele const* tele = &itr->second;
            if (tele->mapId == mapId)
                locs.push_back(tele);
        }

        // no teleport locations defined for this map (empty/stripped game_tele):
        // indexing locs would underflow urand() and dereference
        if (locs.empty())
        {
            TC_LOG_ERROR("playerbot", "No game_tele locations for map {}, skipping for random teleport of bot {}",
                    mapId, bot->GetName());
            continue;
        }

        index = urand(0, locs.size() - 1);
        GameTele const* tele = locs[index];
        uint32 level = GetZoneLevel(tele->mapId, tele->position_x, tele->position_y, tele->position_z);
        if (level > maxLevel + 5)
            continue;

        level = min(level, maxLevel);
        if (!level) level = 1;

        if (urand(0, 100) < 100 * sPlayerbotAIConfig.randomBotMaxLevelChance)
            level = maxLevel;

        if (level < sPlayerbotAIConfig.randomBotMinLevel)
            continue;

        PlayerbotFactory factory(bot, level);
        factory.CleanRandomize();
        RandomTeleport(bot, tele->mapId, tele->position_x, tele->position_y, tele->position_z);
        break;
    }
}

uint32 RandomPlayerbotMgr::GetZoneLevel(uint16 mapId, float teleX, float teleY, float teleZ)
{
    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);

    // A creature-table scan per bot teleport is heavy and creature levels do
    // not change at runtime: cache the computed (min,max) per map area grid for
    // the whole server run (sentinel max=0 means "no creatures in that area")
    static std::map<uint64, uint64> zoneLevelCache;
    uint64 levelKey = (uint64(mapId) << 44)
            | (uint64((int32(teleY) / 64) + (1 << 21)) << 22)
            | uint64((int32(teleX) / 64) + (1 << 21));
    std::map<uint64, uint64>::iterator cached = zoneLevelCache.find(levelKey);
    if (cached != zoneLevelCache.end())
    {
        uint32 cachedMin = uint32(cached->second >> 32);
        uint32 cachedMax = uint32(cached->second);
        if (!cachedMax)
            return urand(1, maxLevel);

        uint32 cachedLevel = urand(cachedMin, cachedMax);
        return cachedLevel > maxLevel ? maxLevel : cachedLevel;
    }

	uint32 level;
    // creature levels live in creature_template_difficulty (DifficultyID 0 = DIFFICULTY_NONE);
    // creature_template has no minlevel/maxlevel columns in this schema - querying them
    // raises ER_BAD_FIELD_ERROR which ABORTs the server
    QueryResult results = WorldDatabase.PQuery("select avg(d.MinLevel) minlevel, avg(d.MaxLevel) maxlevel from creature c "
            "inner join creature_template_difficulty d on d.Entry = c.id and d.DifficultyID = 0 "
            "where c.map = '{}' and d.MinLevel > 1 and abs(c.position_x - '{}') < '{}' and abs(c.position_y - '{}') < '{}'",
            mapId, teleX, sPlayerbotAIConfig.randomBotTeleportDistance / 2, teleY, sPlayerbotAIConfig.randomBotTeleportDistance / 2);

    if (results)
    {
        Field* fields = results->Fetch();
        // AVG() over zero matching rows returns NULL rather than no row;
        // reading such a field is undefined - fall through to the random level
        if (fields && !fields[0].IsNull() && !fields[1].IsNull())
        {
            // AVG() comes back as DECIMAL - read it as double; GetUInt8() on a fractional
            // value trips the Field truncation assert and crashes the server
            uint32 minLevel = static_cast<uint32>(fields[0].GetDouble());
            uint32 maxZoneLevel = static_cast<uint32>(fields[1].GetDouble());
            if (!minLevel)
                minLevel = 1;
            if (maxZoneLevel < minLevel)
                maxZoneLevel = minLevel;

            zoneLevelCache[levelKey] = (uint64(minLevel) << 32) | maxZoneLevel;
            level = urand(minLevel, maxZoneLevel);
            if (level > maxLevel)
                level = maxLevel;
        }
        else
        {
            zoneLevelCache[levelKey] = 0;
            level = urand(1, maxLevel);
        }
    }
    else
    {
        zoneLevelCache[levelKey] = 0;
        level = urand(1, maxLevel);
    }

    return level;
}

void RandomPlayerbotMgr::Refresh(Player* bot)
{
    TC_LOG_INFO("playerbot",  "Refreshing bot {}", bot->GetName().c_str());
    if (bot->isDead())
    {
        bot->ResurrectPlayer(1.0f);
        bot->SpawnCorpseBones();
        bot->SaveToDB();
        bot->GetPlayerbotAI()->ResetStrategies();
    }

    bot->GetPlayerbotAI()->Reset();

    for (auto const& pair : bot->GetThreatManager().GetThreatenedByMeList())
    {
        if (Unit* unit = pair.second->GetOwner())
        {
            unit->RemoveAllAttackers();
            unit->CombatStop(true);
        }
    }

    bot->GetThreatManager().ClearAllThreat();
    bot->RemoveAllAttackers();
    bot->CombatStop(true);

    bot->DurabilityRepairAll(false, 1.0f, false);
    bot->SetFullHealth();
    bot->SetPvP(true);

    if (bot->GetMaxPower(POWER_MANA) > 0)
        bot->SetPower(POWER_MANA, bot->GetMaxPower(POWER_MANA));

    if (bot->GetMaxPower(POWER_ENERGY) > 0)
        bot->SetPower(POWER_ENERGY, bot->GetMaxPower(POWER_ENERGY));
}


bool RandomPlayerbotMgr::IsRandomBot(Player* bot)
{
    return IsRandomBot(bot->GetGUID().GetCounter());
}

bool RandomPlayerbotMgr::IsRandomBot(uint32 bot)
{
    return GetEventValue(bot, "add");
}

list<uint32> RandomPlayerbotMgr::GetBots()
{
    list<uint32> bots;

    QueryResult results = CharacterDatabase.Query(
            "select bot from ai_playerbot_random_bots where owner = 0 and event = 'add'");

    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 bot = fields[0].GetUInt32();
            bots.push_back(bot);
        } while (results->NextRow());
    }

    return bots;
}

vector<uint32> RandomPlayerbotMgr::GetFreeBots(bool alliance)
{
    set<uint32> bots;

    QueryResult results = CharacterDatabase.PQuery(
            "select `bot` from ai_playerbot_random_bots where event = 'add'");

    if (results)
    {
        do
        {
            Field* fields = results->Fetch();
            uint32 bot = fields[0].GetUInt32();
            bots.insert(bot);
        } while (results->NextRow());
    }

    vector<uint32> guids;
    for (list<uint32>::iterator i = sPlayerbotAIConfig.randomBotAccounts.begin(); i != sPlayerbotAIConfig.randomBotAccounts.end(); i++)
    {
        uint32 accountId = *i;
        if (!sAccountMgr->GetCharactersCount(accountId))
            continue;

        QueryResult result = CharacterDatabase.PQuery("SELECT guid, race FROM characters WHERE account = '{}'", accountId);
        if (!result)
            continue;

        do
        {
            Field* fields = result->Fetch();
            uint32 guid = fields[0].GetUInt32();
            uint8 race = fields[1].GetUInt8();
            if (bots.find(guid) == bots.end() &&
                    ((alliance && IsAlliance(race)) || ((!alliance && !IsAlliance(race))
            )))
                guids.push_back(guid);
        } while (result->NextRow());
    }


    return guids;
}

uint32 RandomPlayerbotMgr::GetEventValue(uint32 bot, string event)
{
    uint32 value = 0;

    QueryResult results = CharacterDatabase.PQuery(
            "select `value`, `time`, validIn from ai_playerbot_random_bots where owner = 0 and bot = '{}' and event = '{}'",
            bot, event.c_str());

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

uint32 RandomPlayerbotMgr::SetEventValue(uint32 bot, string event, uint32 value, uint32 validIn)
{
    CharacterDatabase.PExecute("delete from ai_playerbot_random_bots where owner = 0 and bot = '{}' and event = '{}'",
            bot, event.c_str());
    if (value)
    {
        CharacterDatabase.PExecute(
                "insert into ai_playerbot_random_bots (owner, bot, `time`, validIn, event, `value`) values ('{}', '{}', '{}', '{}', '{}', '{}')",
                0, bot, (uint32)time(0), validIn, event.c_str(), value);
    }

    return value;
}

bool RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(ChatHandler* handler, char const* args)
{
    if (!sPlayerbotAIConfig.enabled)
    {
        TC_LOG_ERROR("playerbot",  "Playerbot system is currently disabled!");
        return false;
    }

    if (!args || !*args)
    {
        TC_LOG_ERROR("playerbot",  "Usage: rndbot stats/update/reset/init/refresh/add/remove");
        return false;
    }

    string cmd = args;

    if (cmd == "reset")
    {
        CharacterDatabase.PExecute("delete from ai_playerbot_random_bots");
        TC_LOG_INFO("playerbot",  "Random bots were reset for all players. Please restart the Server.");
        return true;
    }
    else if (cmd == "stats")
    {
        sRandomPlayerbotMgr.PrintStats();
        return true;
    }
    else if (cmd == "update")
    {
        sRandomPlayerbotMgr.UpdateAIInternal(0);
        return true;
    }
    else if (cmd == "init" || cmd == "refresh" || cmd == "teleport")
    {
		TC_LOG_INFO("playerbot",  "Randomizing bots for {} accounts", sPlayerbotAIConfig.randomBotAccounts.size());
        list<uint32> botIds;
        for (list<uint32>::iterator i = sPlayerbotAIConfig.randomBotAccounts.begin(); i != sPlayerbotAIConfig.randomBotAccounts.end(); ++i)
        {
            uint32 account = *i;
            if (QueryResult results = CharacterDatabase.PQuery("SELECT guid FROM characters where account = '{}'", account))
            {
                do
                {
                    Field* fields = results->Fetch();

                    uint32 botId = fields[0].GetUInt32();
                    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(botId);
                    Player* bot = ObjectAccessor::FindPlayer(guid);
                    if (!bot)
                        continue;

                    botIds.push_back(botId);
                } while (results->NextRow());
            }
        }

        int processed = 0;
        for (list<uint32>::iterator i = botIds.begin(); i != botIds.end(); ++i)
        {
            ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(*i);
            Player* bot = ObjectAccessor::FindPlayer(guid);
            if (!bot)
                continue;

            TC_LOG_INFO("playerbot",  "[{}/{}] Processing command '{}' for bot '{}'",
                    processed++, botIds.size(), cmd.c_str(), bot->GetName().c_str());

            if (cmd == "init")
            {
                sRandomPlayerbotMgr.RandomizeFirst(bot);
            }
            else if (cmd == "teleport")
            {
                sRandomPlayerbotMgr.RandomTeleportForLevel(bot);
            }
            else
            {
                bot->SetLevel(bot->GetLevel() - 1);
                sRandomPlayerbotMgr.IncreaseLevel(bot);
            }
            uint32 randomTime = urand(sPlayerbotAIConfig.minRandomBotRandomizeTime, sPlayerbotAIConfig.maxRandomBotRandomizeTime);
            CharacterDatabase.PExecute("update ai_playerbot_random_bots set validIn = '{}' where event = 'randomize' and bot = '{}'",
                    randomTime, bot->GetGUID().GetCounter());
            CharacterDatabase.PExecute("update ai_playerbot_random_bots set validIn = '{}' where event = 'logout' and bot = '{}'",
                    sPlayerbotAIConfig.maxRandomBotInWorldTime, bot->GetGUID().GetCounter());
        }
        return true;
    }
    else
    {
        list<string> messages = sRandomPlayerbotMgr.HandlePlayerbotCommand(args, NULL);
        for (list<string>::iterator i = messages.begin(); i != messages.end(); ++i)
        {
            TC_LOG_INFO("playerbot",  "{}", i->c_str());
        }
        return true;
    }

    return false;
}

void RandomPlayerbotMgr::HandleCommand(uint32 type, const string& text, Player& fromPlayer)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (!ai)
            continue;

        ai->HandleCommand(type, text, fromPlayer);
    }
}

void RandomPlayerbotMgr::OnPlayerLogout(Player* player)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (!ai)
            continue;

        // session logouts are processed before the next map/player update, but a
        // bot tick after that would still drain chat commands that captured this
        // player as their owner (whisper-then-quit); drop them while he is alive
        ai->DropCommandsFrom(player);

        if (player == ai->GetMaster())
        {
            ai->SetMaster(NULL);
            ai->ResetStrategies();
        }
    }

    // The real-player list must never outlive a player object: bot sessions log
    // out with their PlayerbotAI still attached, so the old "no AI" filter left
    // every logged out bot dangling in the list. Remove unconditionally.
    vector<Player*>::iterator i = find(players.begin(), players.end(), player);
    if (i != players.end())
        players.erase(i);
}

void RandomPlayerbotMgr::OnPlayerLogin(Player* player)
{
    for (PlayerBotMap::const_iterator it = GetPlayerBotsBegin(); it != GetPlayerBotsEnd(); ++it)
    {
        Player* const bot = it->second;
        if (player == bot || player->GetPlayerbotAI())
            continue;

        // the AI is attached before a bot enters the map, but guard anyway: a
        // bot that is mid logout/teardown must not be dereferenced here
        PlayerbotAI* ai = bot->GetPlayerbotAI();
        if (!ai)
            continue;

        Group* group = bot->GetGroup();
        if (!group)
            continue;

        for (GroupReference *gref = group->GetFirstMember(); gref; gref = gref->next())
        {
            // offline group members (players that logged out earlier) have a
            // null source; they can never be the player that just logged in
            Player* member = gref->GetSource();
            if (!member)
                continue;

            if (member == player && (!ai->GetMaster() || ai->GetMaster()->GetPlayerbotAI()))
            {
                ai->SetMaster(player);
                ai->ResetStrategies();
                ai->TellMaster("Hello");
                break;
            }
        }
    }

    // Track real players only. Bot sessions still have no PlayerbotAI at this
    // point (their AI is attached once login completes), so the AI check alone
    // used to add every bot to the list - and bots are never removed again at
    // logout, leaving dangling Player pointers for GuildTaskMgr::Update and
    // GetRandomPlayer to dereference while real players are online.
    if (player->GetPlayerbotAI() ||
        (player->GetSession() && player->GetSession()->IsBotSession()))
        return;

    players.push_back(player);
}

Player* RandomPlayerbotMgr::GetRandomPlayer()
{
    if (players.empty())
        return NULL;

    uint32 index = urand(0, players.size() - 1);
    return players[index];
}

void RandomPlayerbotMgr::PrintStats()
{
    TC_LOG_INFO("playerbot",  "{} Random Bots online", playerBots.size());

    map<uint32, int> alliance, horde;
    for (uint32 i = 0; i < 10; ++i)
    {
        alliance[i] = 0;
        horde[i] = 0;
    }

    map<uint8, int> perRace, perClass;
    for (uint8 race = RACE_HUMAN; race < MAX_RACES; ++race)
    {
        perRace[race] = 0;
    }
    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        perClass[cls] = 0;
    }

    int dps = 0, heal = 0, tank = 0;
    for (PlayerBotMap::iterator i = playerBots.begin(); i != playerBots.end(); ++i)
    {
        Player* bot = i->second;
        if (IsAlliance(bot->GetRace()))
            alliance[bot->GetLevel() / 10]++;
        else
            horde[bot->GetLevel() / 10]++;

        perRace[bot->GetRace()]++;
        perClass[bot->GetClass()]++;

        int spec = AiFactory::GetPlayerSpecTab(bot);
        switch (bot->GetClass())
        {
        case CLASS_DRUID:
            if (spec == 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_PALADIN:
            if (spec == 1)
                tank++;
            else if (spec == 0)
                heal++;
            else
                dps++;
            break;
        case CLASS_PRIEST:
            if (spec != 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_SHAMAN:
            if (spec == 2)
                heal++;
            else
                dps++;
            break;
        case CLASS_WARRIOR:
            if (spec == 2)
                tank++;
            else
                dps++;
            break;
        default:
            dps++;
            break;
        }
    }

    TC_LOG_INFO("playerbot",  "Per level:");
    uint32 maxLevel = sWorld->getIntConfig(CONFIG_MAX_PLAYER_LEVEL);
    for (uint32 i = 0; i < 10; ++i)
    {
        if (!alliance[i] && !horde[i])
            continue;

        uint32 from = i*10;
        uint32 to = min(from + 9, maxLevel);
        if (!from) from = 1;
        TC_LOG_INFO("playerbot",  "    {}..{}: {} alliance, {} horde", from, to, alliance[i], horde[i]);
    }
    TC_LOG_INFO("playerbot",  "Per race:");
    for (uint8 race = RACE_HUMAN; race < MAX_RACES; ++race)
    {
        if (perRace[race])
            TC_LOG_INFO("playerbot",  "    {}: {}", ChatHelper::formatRace(race).c_str(), perRace[race]);
    }
    TC_LOG_INFO("playerbot",  "Per class:");
    for (uint8 cls = CLASS_WARRIOR; cls < MAX_CLASSES; ++cls)
    {
        if (perClass[cls])
            TC_LOG_INFO("playerbot",  "    {}: {}", ChatHelper::formatClass(cls).c_str(), perClass[cls]);
    }
    TC_LOG_INFO("playerbot",  "Per role:");
    TC_LOG_INFO("playerbot",  "    tank: {}", tank);
    TC_LOG_INFO("playerbot",  "    heal: {}", heal);
    TC_LOG_INFO("playerbot",  "    dps: {}", dps);
}

double RandomPlayerbotMgr::GetBuyMultiplier(Player* bot)
{
    uint32 id = bot->GetGUID().GetCounter();
    uint32 value = GetEventValue(id, "buymultiplier");
    if (!value)
    {
        value = urand(1, 120);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval, sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "buymultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

double RandomPlayerbotMgr::GetSellMultiplier(Player* bot)
{
    uint32 id = bot->GetGUID().GetCounter();
    uint32 value = GetEventValue(id, "sellmultiplier");
    if (!value)
    {
        value = urand(80, 250);
        uint32 validIn = urand(sPlayerbotAIConfig.minRandomBotsPriceChangeInterval, sPlayerbotAIConfig.maxRandomBotsPriceChangeInterval);
        SetEventValue(id, "sellmultiplier", value, validIn);
    }

    return (double)value / 100.0;
}

uint32 RandomPlayerbotMgr::GetLootAmount(Player* bot)
{
    uint32 id = bot->GetGUID().GetCounter();
    return GetEventValue(id, "lootamount");
}

void RandomPlayerbotMgr::SetLootAmount(Player* bot, uint32 value)
{
    uint32 id = bot->GetGUID().GetCounter();
    SetEventValue(id, "lootamount", value, 24 * 3600);
}

uint32 RandomPlayerbotMgr::GetTradeDiscount(Player* bot)
{
    Group* group = bot->GetGroup();
    return GetLootAmount(bot) / (group ? group->GetMembersCount() : 10);
}

string RandomPlayerbotMgr::HandleRemoteCommand(string request)
{
    string::iterator pos = find(request.begin(), request.end(), ',');
    if (pos == request.end())
    {
        ostringstream out; out << "invalid request: " << request;
        return out.str();
    }

    string command = string(request.begin(), pos);
    ObjectGuid guid = ObjectGuid::Create<HighGuid::Player>(uint64(atoi(string(pos + 1, request.end()).c_str())));
    Player* bot = GetPlayerBot(guid);
    if (!bot)
        return "invalid guid";

    PlayerbotAI *ai = bot->GetPlayerbotAI();
    if (!ai)
        return "invalid guid";

    return ai->HandleRemoteCommand(command);
}
