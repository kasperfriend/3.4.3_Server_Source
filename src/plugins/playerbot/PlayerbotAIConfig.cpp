#include "../pchdef.h"
#include "PlayerbotAIConfig.h"
#include "playerbot.h"
#include "RandomPlayerbotFactory.h"
#include "Accounts/AccountMgr.h"
#include "../../server/database/Database/DatabaseEnv.h"

using namespace std;

namespace
{
    // Column spec: name + full definition as written in
    // sql/custom/playerbot/characters_playerbot.sql
    struct BotTableColumn
    {
        char const* name;
        char const* definition;
    };

    struct BotTableSpec
    {
        char const* name;
        char const* createSql;
        BotTableColumn const* columns;
        size_t columnCount;
        char const* primaryKeyDef;   // e.g. "(`owner`, `bot`, `event`)" or null
    };

    // Ensures every ai_playerbot_* table exists and has all columns the code
    // expects. CREATE TABLE IF NOT EXISTS alone can never extend a table that
    // already exists, so installs that came from older ai_playerbot_* dumps
    // keep missing columns (e.g. `gender` in ai_playerbot_names, `owner` in
    // the event tables) forever - every query against them then fails at BEST
    // (and used to abort the server). Create missing tables, add missing
    // columns and repair a missing primary key; anything that cannot be added
    // (e.g. a PK that would collapse duplicate legacy rows) is logged as an
    // error instead of crashing.
    void EnsureBotTable(BotTableSpec const& spec)
    {
        CharacterDatabase.PExecute("{}", spec.createSql);

        for (size_t i = 0; i < spec.columnCount; ++i)
        {
            BotTableColumn const& column = spec.columns[i];
            QueryResult exists = CharacterDatabase.PQuery(
                    "SELECT 1 FROM information_schema.COLUMNS "
                    "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}' AND COLUMN_NAME = '{}'",
                    spec.name, column.name);
            // a successful but empty result means the column is missing
            if (exists && exists->GetRowCount())
                continue;

            TC_LOG_ERROR("playerbot",
                    "Playerbot table `{}` is missing the `{}` column - adding it",
                    spec.name, column.name);
            CharacterDatabase.PExecute("ALTER TABLE `{}` ADD COLUMN {} {}", spec.name, column.name, column.definition);
        }

        if (!spec.primaryKeyDef)
            return;

        QueryResult hasPk = CharacterDatabase.PQuery(
                "SELECT 1 FROM information_schema.TABLE_CONSTRAINTS "
                "WHERE TABLE_SCHEMA = DATABASE() AND TABLE_NAME = '{}' AND CONSTRAINT_TYPE = 'PRIMARY KEY'",
                spec.name);
        if (hasPk && hasPk->GetRowCount())
            return;

        TC_LOG_ERROR("playerbot", "Playerbot table `{}` has no primary key - adding one", spec.name);
        CharacterDatabase.PExecute("ALTER TABLE `{}` ADD PRIMARY KEY {}", spec.name, spec.primaryKeyDef);
    }

    void EnsureBotTables()
    {
        static BotTableColumn const randomBotsColumns[] =
        {
            { "owner",   "INT UNSIGNED NOT NULL DEFAULT 0" },
            { "bot",     "INT UNSIGNED NOT NULL DEFAULT 0" },
            { "time",    "INT UNSIGNED NOT NULL DEFAULT 0" },
            { "validIn", "INT UNSIGNED NOT NULL DEFAULT 0" },
            { "event",   "VARCHAR(64) NOT NULL DEFAULT ''" },
            { "value",   "INT UNSIGNED NOT NULL DEFAULT 0" },
        };

        static BotTableColumn const namesColumns[] =
        {
            { "name_id", "INT UNSIGNED NOT NULL AUTO_INCREMENT" },
            { "name",    "VARCHAR(12) NOT NULL" },
            { "gender",  "TINYINT UNSIGNED NOT NULL DEFAULT 0" },
        };

        static BotTableColumn const guildNamesColumns[] =
        {
            { "name_id", "INT UNSIGNED NOT NULL AUTO_INCREMENT" },
            { "name",    "VARCHAR(24) NOT NULL" },
        };

        static BotTableColumn const guildTasksColumns[] =
        {
            { "owner",   "INT UNSIGNED NOT NULL DEFAULT 0" },
            { "guildid", "INT UNSIGNED NOT NULL DEFAULT 0" },
            { "time",    "INT UNSIGNED NOT NULL DEFAULT 0" },
            { "validIn", "INT UNSIGNED NOT NULL DEFAULT 0" },
            { "type",    "VARCHAR(32) NOT NULL DEFAULT ''" },
            { "value",   "INT UNSIGNED NOT NULL DEFAULT 0" },
        };

        static BotTableColumn const speechColumns[] =
        {
            { "id",   "INT UNSIGNED NOT NULL AUTO_INCREMENT" },
            { "name", "VARCHAR(64) NOT NULL" },
            { "text", "VARCHAR(255) NOT NULL" },
            { "type", "VARCHAR(16) NOT NULL DEFAULT 'say'" },
        };

        static BotTableColumn const speechProbabilityColumns[] =
        {
            { "name",        "VARCHAR(64) NOT NULL" },
            { "probability", "INT UNSIGNED NOT NULL DEFAULT 0" },
        };

        static BotTableColumn const customStrategyColumns[] =
        {
            { "name",        "VARCHAR(64) NOT NULL" },
            { "action_line", "VARCHAR(255) NOT NULL" },
        };

        static BotTableSpec const tables[] =
        {
            { "ai_playerbot_random_bots",
                "CREATE TABLE IF NOT EXISTS `ai_playerbot_random_bots` ("
                "`owner` INT UNSIGNED NOT NULL DEFAULT 0, `bot` INT UNSIGNED NOT NULL DEFAULT 0,"
                "`time` INT UNSIGNED NOT NULL DEFAULT 0, `validIn` INT UNSIGNED NOT NULL DEFAULT 0,"
                "`event` VARCHAR(64) NOT NULL DEFAULT '', `value` INT UNSIGNED NOT NULL DEFAULT 0,"
                "PRIMARY KEY (`owner`, `bot`, `event`), KEY `idx_event` (`event`), KEY `idx_bot` (`bot`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
                randomBotsColumns, sizeof(randomBotsColumns) / sizeof(randomBotsColumns[0]), "(`owner`, `bot`, `event`)" },
            { "ai_playerbot_names",
                "CREATE TABLE IF NOT EXISTS `ai_playerbot_names` ("
                "`name_id` INT UNSIGNED NOT NULL AUTO_INCREMENT, `name` VARCHAR(12) NOT NULL,"
                "`gender` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
                "PRIMARY KEY (`name_id`), UNIQUE KEY `idx_name` (`name`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
                namesColumns, sizeof(namesColumns) / sizeof(namesColumns[0]), nullptr },
            { "ai_playerbot_guild_names",
                "CREATE TABLE IF NOT EXISTS `ai_playerbot_guild_names` ("
                "`name_id` INT UNSIGNED NOT NULL AUTO_INCREMENT, `name` VARCHAR(24) NOT NULL,"
                "PRIMARY KEY (`name_id`), UNIQUE KEY `idx_name` (`name`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
                guildNamesColumns, sizeof(guildNamesColumns) / sizeof(guildNamesColumns[0]), nullptr },
            { "ai_playerbot_guild_tasks",
                "CREATE TABLE IF NOT EXISTS `ai_playerbot_guild_tasks` ("
                "`owner` INT UNSIGNED NOT NULL DEFAULT 0, `guildid` INT UNSIGNED NOT NULL DEFAULT 0,"
                "`time` INT UNSIGNED NOT NULL DEFAULT 0, `validIn` INT UNSIGNED NOT NULL DEFAULT 0,"
                "`type` VARCHAR(32) NOT NULL DEFAULT '', `value` INT UNSIGNED NOT NULL DEFAULT 0,"
                "PRIMARY KEY (`owner`, `guildid`, `type`), KEY `idx_guild` (`guildid`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
                guildTasksColumns, sizeof(guildTasksColumns) / sizeof(guildTasksColumns[0]), "(`owner`, `guildid`, `type`)" },
            { "ai_playerbot_speech",
                "CREATE TABLE IF NOT EXISTS `ai_playerbot_speech` ("
                "`id` INT UNSIGNED NOT NULL AUTO_INCREMENT, `name` VARCHAR(64) NOT NULL,"
                "`text` VARCHAR(255) NOT NULL, `type` VARCHAR(16) NOT NULL DEFAULT 'say',"
                "PRIMARY KEY (`id`), KEY `idx_name` (`name`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
                speechColumns, sizeof(speechColumns) / sizeof(speechColumns[0]), nullptr },
            { "ai_playerbot_speech_probability",
                "CREATE TABLE IF NOT EXISTS `ai_playerbot_speech_probability` ("
                "`name` VARCHAR(64) NOT NULL, `probability` INT UNSIGNED NOT NULL DEFAULT 0,"
                "PRIMARY KEY (`name`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
                speechProbabilityColumns, sizeof(speechProbabilityColumns) / sizeof(speechProbabilityColumns[0]), "(`name`)" },
            { "ai_playerbot_custom_strategy",
                "CREATE TABLE IF NOT EXISTS `ai_playerbot_custom_strategy` ("
                "`name` VARCHAR(64) NOT NULL, `action_line` VARCHAR(255) NOT NULL,"
                "PRIMARY KEY (`name`, `action_line`)"
                ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4",
                customStrategyColumns, sizeof(customStrategyColumns) / sizeof(customStrategyColumns[0]), "(`name`, `action_line`)" },
        };

        for (BotTableSpec const& table : tables)
            EnsureBotTable(table);

        // seed tables with the starter rows from characters_playerbot.sql so
        // bots can be created even on a completely fresh install (synchronous:
        // the rows must be present before CreateRandomBots runs below)
        CharacterDatabase.PExecute(
            "INSERT IGNORE INTO `ai_playerbot_names` (`name`) VALUES"
            "('Aeltar'), ('Baldrin'), ('Cathmor'), ('Dornan'), ('Eldrik'), ('Faelan'),"
            "('Gorvin'), ('Halbrik'), ('Ithran'), ('Jorlan'), ('Kelvar'), ('Lorwyn'),"
            "('Mordak'), ('Nyrelle'), ('Orwin'), ('Perrin'), ('Quenna'), ('Rhogar'),"
            "('Sylvara'), ('Torvald'), ('Ulther'), ('Varlen'), ('Wyndel'), ('Xanthe'),"
            "('Yorik'), ('Zaltar'), ('Ashwyn'), ('Brannoc'), ('Cirien'), ('Draveth'),"
            "('Elowen'), ('Fenwick'), ('Gwynor'), ('Harlow'), ('Isolde'), ('Jareth'),"
            "('Kaelith'), ('Lyanna'), ('Merrick'), ('Norwyn'), ('Ondrel'), ('Pellan'),"
            "('Rowena'), ('Selwyn'), ('Thalric'), ('Ulmara'), ('Verrik'), ('Wilrun'),"
            "('Yalira'), ('Zeryth')");
        CharacterDatabase.PExecute(
            "INSERT IGNORE INTO `ai_playerbot_guild_names` (`name`) VALUES"
            "('The Wandering Blades'), ('Sons of Lordaeron'), ('Emerald Vanguard'),"
            "('Ashen Company'), ('Stormwatch'), ('The Silver Hand Irregulars'),"
            "('Dawnbreakers'), ('Ironforge Regulars'), ('Nightfall Covenant'),"
            "('The Last Caravan')");
    }
}

PlayerbotAIConfig::PlayerbotAIConfig() : config(ConfigMgr::instance())
{
}

template <class T>
void LoadList(string value, T &list)
{
    vector<string> ids = split(value, ',');
    for (vector<string>::iterator i = ids.begin(); i != ids.end(); i++)
    {
        if (i->empty())
            continue;

        // only skip tokens that are not numbers at all: 0 is a legal id here
        // (map 0 = Eastern Kingdoms in AiPlayerbot.RandomBotMaps = "0,1,530,571")
        char* end = nullptr;
        uint32 id = strtoul(i->c_str(), &end, 10);
        if (end == i->c_str())
            continue;

        list.push_back(id);
    }
}

bool PlayerbotAIConfig::Initialize()
{
    TC_LOG_INFO("playerbot",  "Initializing AI Playerbot by ike3, based on the original Playerbot by blueboy");

    // The playerbot settings live in the regular worldserver configuration
    // (worldserver.conf or, preferably, worldserver.conf.d/aiplayerbot.conf).
    // Loading an extra file is attempted for backwards compatibility only.
    std::string error;
    if (!config->LoadAdditionalFile("aiplayerbot.conf", true, error))
        TC_LOG_DEBUG("playerbot", "No separate aiplayerbot.conf found ({}), using worldserver configuration", error);

    enabled = config->GetBoolDefault("AiPlayerbot.Enabled", true);
    if (!enabled)
    {
        TC_LOG_INFO("playerbot",  "AI Playerbot is Disabled in aiplayerbot.conf");
        return false;
    }

    // self-heal the ai_playerbot_* tables before anything queries them:
    // create missing tables, add columns that older ai_playerbot_* dumps are
    // missing (IF NOT EXISTS scripts can never upgrade existing tables), and
    // seed the starter name pools on a fresh install
    EnsureBotTables();

    globalCoolDown = (uint32) config->GetIntDefault("AiPlayerbot.GlobalCooldown", 500);
    maxWaitForMove = config->GetIntDefault("AiPlayerbot.MaxWaitForMove", 3000);
    reactDelay = (uint32) config->GetIntDefault("AiPlayerbot.ReactDelay", 100);

    sightDistance = config->GetFloatDefault("AiPlayerbot.SightDistance", 50.0f);
    spellDistance = config->GetFloatDefault("AiPlayerbot.SpellDistance", 25.0f);
    reactDistance = config->GetFloatDefault("AiPlayerbot.ReactDistance", 150.0f);
    grindDistance = config->GetFloatDefault("AiPlayerbot.GrindDistance", 100.0f);
    lootDistance = config->GetFloatDefault("AiPlayerbot.LootDistance", 20.0f);
    fleeDistance = config->GetFloatDefault("AiPlayerbot.FleeDistance", 20.0f);
    tooCloseDistance = config->GetFloatDefault("AiPlayerbot.TooCloseDistance", 5.0f);
    meleeDistance = config->GetFloatDefault("AiPlayerbot.MeleeDistance", 0.5f);
    followDistance = config->GetFloatDefault("AiPlayerbot.FollowDistance", 1.5f);
    whisperDistance = config->GetFloatDefault("AiPlayerbot.WhisperDistance", 6000.0f);
    contactDistance = config->GetFloatDefault("AiPlayerbot.ContactDistance", 0.5f);

    criticalHealth = config->GetIntDefault("AiPlayerbot.CriticalHealth", 20);
    lowHealth = config->GetIntDefault("AiPlayerbot.LowHealth", 50);
    mediumHealth = config->GetIntDefault("AiPlayerbot.MediumHealth", 70);
    almostFullHealth = config->GetIntDefault("AiPlayerbot.AlmostFullHealth", 85);
    lowMana = config->GetIntDefault("AiPlayerbot.LowMana", 15);
    mediumMana = config->GetIntDefault("AiPlayerbot.MediumMana", 40);

    randomGearLoweringChance = config->GetFloatDefault("AiPlayerbot.RandomGearLoweringChance", 0.15);
    randomBotMaxLevelChance = config->GetFloatDefault("AiPlayerbot.RandomBotMaxLevelChance", 0.4);

    iterationsPerTick = config->GetIntDefault("AiPlayerbot.IterationsPerTick", 10);

    allowGuildBots = config->GetBoolDefault("AiPlayerbot.AllowGuildBots", true);

    randomBotMapsAsString = config->GetStringDefault("AiPlayerbot.RandomBotMaps", "0,1,530,571");
    LoadList<vector<uint32> >(randomBotMapsAsString, randomBotMaps);
    LoadList<list<uint32> >(config->GetStringDefault("AiPlayerbot.RandomBotQuestItems", "6948,5175,5176,5177,5178"), randomBotQuestItems);
    LoadList<list<uint32> >(config->GetStringDefault("AiPlayerbot.RandomBotSpellIds", "54197"), randomBotSpellIds);

    randomBotAutologin = config->GetBoolDefault("AiPlayerbot.RandomBotAutologin", true);
    minRandomBots = config->GetIntDefault("AiPlayerbot.MinRandomBots", 50);
    maxRandomBots = config->GetIntDefault("AiPlayerbot.MaxRandomBots", 200);
    randomBotUpdateInterval = config->GetIntDefault("AiPlayerbot.RandomBotUpdateInterval", 60);
    randomBotCountChangeMinInterval = config->GetIntDefault("AiPlayerbot.RandomBotCountChangeMinInterval", 24 * 3600);
    randomBotCountChangeMaxInterval = config->GetIntDefault("AiPlayerbot.RandomBotCountChangeMaxInterval", 3 * 24 * 3600);
    minRandomBotInWorldTime = config->GetIntDefault("AiPlayerbot.MinRandomBotInWorldTime", 24 * 3600);
    maxRandomBotInWorldTime = config->GetIntDefault("AiPlayerbot.MaxRandomBotInWorldTime", 14 * 24 * 3600);
    minRandomBotRandomizeTime = config->GetIntDefault("AiPlayerbot.MinRandomBotRandomizeTime", 2 * 3600);
    maxRandomBotRandomizeTime = config->GetIntDefault("AiPlayerbot.MaxRandomRandomizeTime", 14 * 24 * 3600);
    minRandomBotReviveTime = config->GetIntDefault("AiPlayerbot.MinRandomBotReviveTime", 60);
    maxRandomBotReviveTime = config->GetIntDefault("AiPlayerbot.MaxRandomReviveTime", 300);
    randomBotTeleportDistance = config->GetIntDefault("AiPlayerbot.RandomBotTeleportDistance", 1000);
    minRandomBotsPerInterval = config->GetIntDefault("AiPlayerbot.MinRandomBotsPerInterval", 50);
    maxRandomBotsPerInterval = config->GetIntDefault("AiPlayerbot.MaxRandomBotsPerInterval", 100);
    minRandomBotsPriceChangeInterval = config->GetIntDefault("AiPlayerbot.MinRandomBotsPriceChangeInterval", 2 * 3600);
    maxRandomBotsPriceChangeInterval = config->GetIntDefault("AiPlayerbot.MaxRandomBotsPriceChangeInterval", 48 * 3600);
    randomBotJoinLfg = config->GetBoolDefault("AiPlayerbot.RandomBotJoinLfg", true);
    logInGroupOnly = config->GetBoolDefault("AiPlayerbot.LogInGroupOnly", true);
    logValuesPerTick = config->GetBoolDefault("AiPlayerbot.LogValuesPerTick", false);
    fleeingEnabled = config->GetBoolDefault("AiPlayerbot.FleeingEnabled", true);
    randomBotMinLevel = config->GetIntDefault("AiPlayerbot.RandomBotMinLevel", 1);
    randomBotMaxLevel = config->GetIntDefault("AiPlayerbot.RandomBotMaxLevel", 255);
    randomBotLoginAtStartup = config->GetBoolDefault("AiPlayerbot.RandomBotLoginAtStartup", true);
    randomBotTeleLevel = config->GetIntDefault("AiPlayerbot.RandomBotTeleLevel", 3);

    randomChangeMultiplier = config->GetFloatDefault("AiPlayerbot.RandomChangeMultiplier", 1.0);

    randomBotCombatStrategies = config->GetStringDefault("AiPlayerbot.RandomBotCombatStrategies", "+dps,+dps assist,-threat");
    randomBotNonCombatStrategies = config->GetStringDefault("AiPlayerbot.RandomBotNonCombatStrategies", "+grind,+move random,+loot");
    combatStrategies = config->GetStringDefault("AiPlayerbot.CombatStrategies", "+custom::say");
    nonCombatStrategies = config->GetStringDefault("AiPlayerbot.NonCombatStrategies", "+custom::say");

    commandPrefix = config->GetStringDefault("AiPlayerbot.CommandPrefix", "");

    commandServerPort = config->GetIntDefault("AiPlayerbot.CommandServerPort", 0);

    for (uint32 cls = 0; cls < MAX_CLASSES; ++cls)
    {
        for (uint32 spec = 0; spec < 3; ++spec)
        {
            ostringstream os; os << "AiPlayerbot.RandomClassSpecProbability." << cls << "." << spec;
            specProbability[cls][spec] = config->GetIntDefault(os.str().c_str(), 33);
        }
    }

    randomBotAccountPrefix = config->GetStringDefault("AiPlayerbot.RandomBotAccountPrefix", "rndbot");
    randomBotAccountCount = config->GetIntDefault("AiPlayerbot.RandomBotAccountCount", 50);
    deleteRandomBotAccounts = config->GetBoolDefault("AiPlayerbot.DeleteRandomBotAccounts", false);
    randomBotGuildCount = config->GetIntDefault("AiPlayerbot.RandomBotGuildCount", 50);
    deleteRandomBotGuilds = config->GetBoolDefault("AiPlayerbot.DeleteRandomBotGuilds", false);

    guildTaskEnabled = config->GetBoolDefault("AiPlayerbot.EnableGuildTasks", true);
    minGuildTaskChangeTime = config->GetIntDefault("AiPlayerbot.MinGuildTaskChangeTime", 2 * 24 * 3600);
    maxGuildTaskChangeTime = config->GetIntDefault("AiPlayerbot.MaxGuildTaskChangeTime", 5 * 24 * 3600);
    minGuildTaskAdvertisementTime = config->GetIntDefault("AiPlayerbot.MinGuildTaskAdvertisementTime", 8 * 3600);
    maxGuildTaskAdvertisementTime = config->GetIntDefault("AiPlayerbot.MaxGuildTaskAdvertisementTime", 4 * 24 * 3600);
    minGuildTaskRewardTime = config->GetIntDefault("AiPlayerbot.MinGuildTaskRewardTime", 60);
    maxGuildTaskRewardTime = config->GetIntDefault("AiPlayerbot.MaxGuildTaskRewardTime", 600);

    // An inverted min/max pair from the config would eventually reach
    // urand(min, max) with max < min, whose ASSERT(max >= min) crashes the
    // worldserver - validate every pair and swap inverted values instead of
    // trusting the configuration file blindly
    auto normalizeRange = [](const char* minName, const char* maxName, uint32& minValue, uint32& maxValue)
    {
        if (minValue > maxValue)
        {
            TC_LOG_ERROR("playerbot", "PlayerbotAIConfig: configuration value {} ({}) is greater than {} ({}); swapping the two values",
                    minName, minValue, maxName, maxValue);
            uint32 temp = minValue;
            minValue = maxValue;
            maxValue = temp;
        }
    };

    normalizeRange("AiPlayerbot.MinRandomBots", "AiPlayerbot.MaxRandomBots", minRandomBots, maxRandomBots);
    normalizeRange("AiPlayerbot.RandomBotCountChangeMinInterval", "AiPlayerbot.RandomBotCountChangeMaxInterval",
            randomBotCountChangeMinInterval, randomBotCountChangeMaxInterval);
    normalizeRange("AiPlayerbot.MinRandomBotsPerInterval", "AiPlayerbot.MaxRandomBotsPerInterval",
            minRandomBotsPerInterval, maxRandomBotsPerInterval);
    normalizeRange("AiPlayerbot.MinRandomBotInWorldTime", "AiPlayerbot.MaxRandomBotInWorldTime",
            minRandomBotInWorldTime, maxRandomBotInWorldTime);
    normalizeRange("AiPlayerbot.MinRandomBotRandomizeTime", "AiPlayerbot.MaxRandomBotRandomizeTime",
            minRandomBotRandomizeTime, maxRandomBotRandomizeTime);
    normalizeRange("AiPlayerbot.MinRandomBotReviveTime", "AiPlayerbot.MaxRandomBotReviveTime",
            minRandomBotReviveTime, maxRandomBotReviveTime);
    normalizeRange("AiPlayerbot.MinRandomBotPvpTime", "AiPlayerbot.MaxRandomBotPvpTime",
            minRandomBotPvpTime, maxRandomBotPvpTime);
    normalizeRange("AiPlayerbot.MinRandomBotsPriceChangeInterval", "AiPlayerbot.MaxRandomBotsPriceChangeInterval",
            minRandomBotsPriceChangeInterval, maxRandomBotsPriceChangeInterval);
    normalizeRange("AiPlayerbot.RandomBotMinLevel", "AiPlayerbot.RandomBotMaxLevel", randomBotMinLevel, randomBotMaxLevel);
    normalizeRange("AiPlayerbot.MinGuildTaskChangeTime", "AiPlayerbot.MaxGuildTaskChangeTime",
            minGuildTaskChangeTime, maxGuildTaskChangeTime);
    normalizeRange("AiPlayerbot.MinGuildTaskAdvertisementTime", "AiPlayerbot.MaxGuildTaskAdvertisementTime",
            minGuildTaskAdvertisementTime, maxGuildTaskAdvertisementTime);
    normalizeRange("AiPlayerbot.MinGuildTaskRewardTime", "AiPlayerbot.MaxGuildTaskRewardTime",
            minGuildTaskRewardTime, maxGuildTaskRewardTime);

    // used as a divisor in trigger/LFG probability rolls - zero (or a negative
    // value) makes the division produce inf/garbage and undefined int casts
    if (randomChangeMultiplier <= 0.0f)
    {
        TC_LOG_ERROR("playerbot", "PlayerbotAIConfig: AiPlayerbot.RandomChangeMultiplier ({}) must be positive; using 1.0",
                randomChangeMultiplier);
        randomChangeMultiplier = 1.0f;
    }

    RandomPlayerbotFactory::CreateRandomBots();
    TC_LOG_INFO("playerbot",  "AI Playerbot configuration loaded");

    return true;
}


bool PlayerbotAIConfig::IsInRandomAccountList(uint32 id)
{
    return find(randomBotAccounts.begin(), randomBotAccounts.end(), id) != randomBotAccounts.end();
}

bool PlayerbotAIConfig::IsInRandomQuestItemList(uint32 id)
{
    return find(randomBotQuestItems.begin(), randomBotQuestItems.end(), id) != randomBotQuestItems.end();
}

string PlayerbotAIConfig::GetValue(string name)
{
    ostringstream out;

    if (name == "GlobalCooldown")
        out << globalCoolDown;
    else if (name == "ReactDelay")
        out << reactDelay;

    else if (name == "SightDistance")
        out << sightDistance;
    else if (name == "SpellDistance")
        out << spellDistance;
    else if (name == "ReactDistance")
        out << reactDistance;
    else if (name == "GrindDistance")
        out << grindDistance;
    else if (name == "LootDistance")
        out << lootDistance;
    else if (name == "FleeDistance")
        out << fleeDistance;

    else if (name == "CriticalHealth")
        out << criticalHealth;
    else if (name == "LowHealth")
        out << lowHealth;
    else if (name == "MediumHealth")
        out << mediumHealth;
    else if (name == "AlmostFullHealth")
        out << almostFullHealth;
    else if (name == "LowMana")
        out << lowMana;

    else if (name == "IterationsPerTick")
        out << iterationsPerTick;

    return out.str();
}

void PlayerbotAIConfig::SetValue(string name, string value)
{
    istringstream out(value, istringstream::in);

    if (name == "GlobalCooldown")
        out >> globalCoolDown;
    else if (name == "ReactDelay")
        out >> reactDelay;

    else if (name == "SightDistance")
        out >> sightDistance;
    else if (name == "SpellDistance")
        out >> spellDistance;
    else if (name == "ReactDistance")
        out >> reactDistance;
    else if (name == "GrindDistance")
        out >> grindDistance;
    else if (name == "LootDistance")
        out >> lootDistance;
    else if (name == "FleeDistance")
        out >> fleeDistance;

    else if (name == "CriticalHealth")
        out >> criticalHealth;
    else if (name == "LowHealth")
        out >> lowHealth;
    else if (name == "MediumHealth")
        out >> mediumHealth;
    else if (name == "AlmostFullHealth")
        out >> almostFullHealth;
    else if (name == "LowMana")
        out >> lowMana;

    else if (name == "IterationsPerTick")
        out >> iterationsPerTick;
}
