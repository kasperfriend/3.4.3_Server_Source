/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "DBUpdater.h"
#include "BuiltInConfig.h"
#include "Config.h"
#include "DatabaseEnv.h"
#include "DatabaseLoader.h"
#include "GitRevision.h"
#include "Log.h"
#include "QueryResult.h"
#include "StartProcess.h"
#include "UpdateFetcher.h"
#include <boost/filesystem/operations.hpp>
#include <fstream>
#include <iostream>
#include <vector>

std::string DBUpdaterUtil::GetCorrectedMySQLExecutable()
{
    if (!corrected_path().empty())
        return corrected_path();
    else
        return BuiltInConfig::GetMySQLExecutable();
}

bool DBUpdaterUtil::CheckExecutable()
{
    std::string const configured = GetCorrectedMySQLExecutable();

    auto isUsableValue = [](std::string const& value) -> bool
    {
        if (value.empty())
            return false;
        // CMake bakes "<VAR>-NOTFOUND" when the binary wasn't found at configure time.
        // Treat that sentinel as "not configured" so we fall through to PATH search
        // instead of treating the literal sentinel as a user path.
        static std::string const suffix = "-NOTFOUND";
        if (value.size() >= suffix.size() &&
            value.compare(value.size() - suffix.size(), suffix.size(), suffix) == 0)
            return false;
        return true;
    };

    auto toAbsoluteGeneric = [](boost::filesystem::path const& p) -> std::string
    {
        try
        {
            return boost::filesystem::absolute(p).generic_string();
        }
        catch (...)
        {
            return p.generic_string();
        }
    };

    boost::system::error_code ec;

    // 1. Configured value points directly at a file - done.
    if (isUsableValue(configured))
    {
        boost::filesystem::path const exe(configured);
        ec.clear();
        if (boost::filesystem::is_regular_file(exe, ec) && !ec)
            return true;

        // 2. Configured value is a directory (e.g. ".../mariadb/bin") - look for the
        //    client binary inside it instead of failing outright.
        ec.clear();
        if (boost::filesystem::is_directory(exe, ec) && !ec)
        {
#ifdef _WIN32
            char const* const candidates[] = { "mysql.exe", "mariadb.exe", "mysql", "mariadb" };
#else
            char const* const candidates[] = { "mysql", "mariadb" };
#endif
            for (char const* candidate : candidates)
            {
                boost::filesystem::path const full = exe / candidate;
                ec.clear();
                if (boost::filesystem::is_regular_file(full, ec) && !ec)
                {
                    // Correct the path to the cli
                    corrected_path() = toAbsoluteGeneric(full);
                    TC_LOG_INFO("sql.updates", "Resolved MySQLExecutable directory '{}' to client binary '{}'.",
                        configured, corrected_path());
                    return true;
                }
            }
        }

#ifdef _WIN32
        // 3. On Windows allow omitting ".exe" (e.g. ".../bin/mysql").
        {
            boost::filesystem::path exePath(configured);
            if (!exePath.has_extension())
            {
                boost::filesystem::path withExe = exePath;
                withExe += ".exe";
                ec.clear();
                if (boost::filesystem::is_regular_file(withExe, ec) && !ec)
                {
                    corrected_path() = toAbsoluteGeneric(withExe);
                    TC_LOG_INFO("sql.updates", "Resolved MySQLExecutable '{}' to client binary '{}'.",
                        configured, corrected_path());
                    return true;
                }
            }
        }
#endif

        // 4. Bare filename (e.g. "mysql.exe") - resolve it via PATH exactly as given.
        {
            boost::filesystem::path const exePath(configured);
            if (exePath.parent_path().empty() && exePath.has_filename())
            {
                boost::filesystem::path const found(Trinity::SearchExecutableInPath(configured));
                if (!found.empty())
                {
                    ec.clear();
                    if (boost::filesystem::is_regular_file(found, ec) && !ec)
                    {
                        corrected_path() = toAbsoluteGeneric(found);
                        return true;
                    }
                }
            }
        }
    }

    // 5. Fallback: search PATH for the known client names. This covers an empty config,
    //    a stale CMake-baked path (binary moved since compilation) and MariaDB installs
    //    where the client may be named "mariadb" instead of "mysql".
#ifdef _WIN32
    char const* const pathCandidates[] = { "mysql", "mysql.exe", "mariadb", "mariadb.exe" };
#else
    char const* const pathCandidates[] = { "mysql", "mariadb" };
#endif
    for (char const* candidate : pathCandidates)
    {
        boost::filesystem::path const found(Trinity::SearchExecutableInPath(candidate));
        if (found.empty())
            continue;

        ec.clear();
        if (boost::filesystem::is_regular_file(found, ec) && !ec)
        {
            // Correct the path to the cli
            corrected_path() = toAbsoluteGeneric(found);
            TC_LOG_INFO("sql.updates", "Resolved MySQL client '{}' from PATH to '{}'.",
                candidate, corrected_path());
            return true;
        }
    }

    // 6. Nothing worked - report the *configured* value (not the current working
    //    directory) with actionable hints.
    std::string display;
    if (configured.empty())
        display = "<empty> (MySQLExecutable is empty and no built-in CMake path is usable)";
    else if (!isUsableValue(configured))
        display = "'" + configured + "' (built-in CMake path was not found at configure time)";
    else
        display = "'" + configured + "'";

    TC_LOG_FATAL("sql.updates", "Didn't find any executable MySQL/MariaDB client binary for {} nor in PATH (searched for mysql/mariadb client), "
        "correct the path in the *.conf (\"MySQLExecutable\"). It must point to the mysql client binary itself, "
        "e.g. \"C:/Program Files/MariaDB/bin/mysql.exe\" or \"/usr/bin/mysql\".", display);

    if (isUsableValue(configured))
    {
        boost::filesystem::path const exe(configured);
        ec.clear();
        if (boost::filesystem::is_directory(exe, ec) && !ec)
        {
#ifdef _WIN32
            TC_LOG_FATAL("sql.updates", "The configured MySQLExecutable '{}' is a directory but no mysql.exe/mariadb.exe was found inside it. "
                "Point MySQLExecutable at the binary itself (e.g. '{}/mysql.exe') or add that directory to PATH.", configured, configured);
#else
            TC_LOG_FATAL("sql.updates", "The configured MySQLExecutable '{}' is a directory but no mysql/mariadb client was found inside it. "
                "Point MySQLExecutable at the binary itself (e.g. '{}/mysql') or add that directory to PATH.", configured, configured);
#endif
        }
    }

    return false;
}

std::string DBUpdaterUtil::ResolveSourceDirectory()
{
    boost::system::error_code ec;

    auto isDir = [&ec](boost::filesystem::path const& p) -> bool
    {
        if (p.empty())
            return false;
        ec.clear();
        return boost::filesystem::is_directory(p, ec) && !ec;
    };

    auto hasUpdatesTree = [&](boost::filesystem::path const& root) -> bool
    {
        return isDir(root) && isDir(root / "sql" / "updates");
    };

    // 1. Explicitly configured value - any existing directory is accepted, preserving
    //    historical behavior (update include rows may use absolute paths that do not
    //    live under the source tree at all).
    std::string const configured = sConfigMgr->GetStringDefault("SourceDirectory", "", true);
    if (!configured.empty() && isDir(boost::filesystem::path(configured)))
        return configured;

    // 2. Baked-in CMake source directory - valid when running on the build machine layout.
    char const* baked = GitRevision::GetSourceDirectory();
    if (baked && *baked && isDir(boost::filesystem::path(baked)))
        return baked;

    // 3. Portable runtime fallbacks. Everything here is derived from the process
    //    environment (working directory, config file location) - nothing hardcoded.
    //    Unlike the explicit values above, fallbacks must actually contain the
    //    sql/updates tree, otherwise resolving to them would be worse than useless:
    //    configured include directories would silently match nothing.
    std::vector<boost::filesystem::path> candidates;
    candidates.reserve(8);

    ec.clear();
    boost::filesystem::path cwd = boost::filesystem::current_path(ec);
    if (!ec && !cwd.empty())
    {
        candidates.push_back(cwd);
        for (int i = 0; i < 3; ++i)
        {
            cwd = cwd.parent_path();
            if (cwd.empty())
                break;
            candidates.push_back(cwd);
        }
    }

    try
    {
        std::string const confFile = sConfigMgr->GetFilename();
        if (!confFile.empty())
        {
            boost::filesystem::path confDir = boost::filesystem::absolute(boost::filesystem::path(confFile)).parent_path();
            for (int i = 0; i < 3; ++i)
            {
                if (confDir.empty())
                    break;
                candidates.push_back(confDir);
                confDir = confDir.parent_path();
            }
        }
    }
    catch (...)
    {
        // Ignore - fallbacks are best effort.
    }

    for (boost::filesystem::path const& candidate : candidates)
    {
        if (hasUpdatesTree(candidate))
        {
            std::string const found = candidate.generic_string();
            TC_LOG_INFO("sql.updates", "Resolved source directory to '{}' (auto-detected, no usable SourceDirectory configured).", found);
            return found;
        }
    }

    return "";
}

std::string& DBUpdaterUtil::corrected_path()
{
    static std::string path;
    return path;
}

// Auth Database
template<>
std::string DBUpdater<LoginDatabaseConnection>::GetConfigEntry()
{
    return "Updates.Auth";
}

template<>
std::string DBUpdater<LoginDatabaseConnection>::GetTableName()
{
    return "Auth";
}

template<>
std::string DBUpdater<LoginDatabaseConnection>::GetBaseFile()
{
    std::string const resolved = DBUpdaterUtil::ResolveSourceDirectory();
    std::string const root = resolved.empty() ? BuiltInConfig::GetSourceDirectory() : resolved;
    return root +
        "/sql/base/auth_database.sql";
}

template<>
bool DBUpdater<LoginDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_LOGIN) ? true : false;
}

// World Database
template<>
std::string DBUpdater<WorldDatabaseConnection>::GetConfigEntry()
{
    return "Updates.World";
}

template<>
std::string DBUpdater<WorldDatabaseConnection>::GetTableName()
{
    return "World";
}

template<>
std::string DBUpdater<WorldDatabaseConnection>::GetBaseFile()
{
    return GitRevision::GetFullDatabase();
}

template<>
bool DBUpdater<WorldDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_WORLD) ? true : false;
}

template<>
BaseLocation DBUpdater<WorldDatabaseConnection>::GetBaseLocationType()
{
    return LOCATION_DOWNLOAD;
}

// Character Database
template<>
std::string DBUpdater<CharacterDatabaseConnection>::GetConfigEntry()
{
    return "Updates.Character";
}

template<>
std::string DBUpdater<CharacterDatabaseConnection>::GetTableName()
{
    return "Character";
}

template<>
std::string DBUpdater<CharacterDatabaseConnection>::GetBaseFile()
{
    std::string const resolved = DBUpdaterUtil::ResolveSourceDirectory();
    std::string const root = resolved.empty() ? BuiltInConfig::GetSourceDirectory() : resolved;
    return root +
        "/sql/base/characters_database.sql";
}

template<>
bool DBUpdater<CharacterDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_CHARACTER) ? true : false;
}

// Hotfix Database
template<>
std::string DBUpdater<HotfixDatabaseConnection>::GetConfigEntry()
{
    return "Updates.Hotfix";
}

template<>
std::string DBUpdater<HotfixDatabaseConnection>::GetTableName()
{
    return "Hotfixes";
}

template<>
std::string DBUpdater<HotfixDatabaseConnection>::GetBaseFile()
{
    return GitRevision::GetHotfixesDatabase();
}

template<>
bool DBUpdater<HotfixDatabaseConnection>::IsEnabled(uint32 const updateMask)
{
    // This way silences warnings under msvc
    return (updateMask & DatabaseLoader::DATABASE_HOTFIX) ? true : false;
}

template<>
BaseLocation DBUpdater<HotfixDatabaseConnection>::GetBaseLocationType()
{
    return LOCATION_DOWNLOAD;
}

// All
template<class T>
BaseLocation DBUpdater<T>::GetBaseLocationType()
{
    return LOCATION_REPOSITORY;
}

template<class T>
bool DBUpdater<T>::Create(DatabaseWorkerPool<T>& pool)
{
    if (!DBUpdaterUtil::CheckExecutable())
        return false;

    TC_LOG_INFO("sql.updates", "Database \"{}\" does not exist, do you want to create it? [yes (default) / no]: ",
        pool.GetConnectionInfo()->database);

    std::string answer;
    std::getline(std::cin, answer);
    if (!answer.empty() && !(answer.substr(0, 1) == "y"))
        return false;

    TC_LOG_INFO("sql.updates", "Creating database \"{}\"...", pool.GetConnectionInfo()->database);

    // Path of temp file
    static Path const temp("create_table.sql");

    // Create temporary query to use external MySQL CLi
    std::ofstream file(temp.generic_string());
    if (!file.is_open())
    {
        TC_LOG_FATAL("sql.updates", "Failed to create temporary query file \"{}\"!", temp.generic_string());
        return false;
    }

    file << "CREATE DATABASE `" << pool.GetConnectionInfo()->database << "` DEFAULT CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci\n\n";

    file.close();

    try
    {
        DBUpdater<T>::ApplyFile(pool, pool.GetConnectionInfo()->host, pool.GetConnectionInfo()->user, pool.GetConnectionInfo()->password,
            pool.GetConnectionInfo()->port_or_socket, "", pool.GetConnectionInfo()->ssl, temp);
    }
    catch (UpdateException&)
    {
        TC_LOG_FATAL("sql.updates", "Failed to create database {}! Does the user (named in *.conf) have `CREATE`, `ALTER`, `DROP`, `INSERT` and `DELETE` privileges on the MySQL server?", pool.GetConnectionInfo()->database);
        boost::filesystem::remove(temp);
        return false;
    }

    TC_LOG_INFO("sql.updates", "Done.");
    boost::filesystem::remove(temp);
    return true;
}

template<class T>
bool DBUpdater<T>::Update(DatabaseWorkerPool<T>& pool)
{
    if (!DBUpdaterUtil::CheckExecutable())
        return false;

    TC_LOG_INFO("sql.updates", "Updating {} database...", DBUpdater<T>::GetTableName());

    std::string const resolvedSource = DBUpdaterUtil::ResolveSourceDirectory();
    if (resolvedSource.empty())
    {
        // No source tree with sql/updates anywhere. If this database has no update
        // include directories configured, the updater could not find any files even
        // with a valid source directory, so failing startup here would serve no purpose
        // (schemas/updates for such databases are applied externally) - skip with a
        // loud warning. Otherwise fail closed: pending updates might exist that we
        // cannot see without the update files.
        QueryResult const includes = Retrieve(pool, "SELECT `path` FROM `updates_include` LIMIT 1");
        if (!includes)
        {
            TC_LOG_WARN("sql.updates", "DBUpdater: No source directory containing sql/updates was found (SourceDirectory setting and built-in path are unusable and no sql/updates folder was detected next to the server binary or config file), "
                "but the {} database has no update include directories configured, so there is nothing the automatic updater could apply. Skipping automatic updates for this database. "
                "Set SourceDirectory in the *.conf to a folder containing sql/ (or ship the sql folder with the binaries) to silence this warning.",
                DBUpdater<T>::GetTableName());
            return true;
        }

        std::string const configuredSource = sConfigMgr->GetStringDefault("SourceDirectory", "", true);
        char const* bakedSource = GitRevision::GetSourceDirectory();
        TC_LOG_ERROR("sql.updates", "DBUpdater: Cannot update the {} database because no source directory was found. "
            "SourceDirectory in the *.conf is {} and the built-in source path is {}. "
            "Set SourceDirectory to the folder that contains your sql directory (the folder with sql/updates in it), "
            "or place the sql folder next to the server binaries/config, "
            "or set Updates.EnableDatabases to exclude this database if you manage its schema manually. Shutting down.",
            DBUpdater<T>::GetTableName(),
            configuredSource.empty() ? "<empty>" : "'" + configuredSource + "'",
            (!bakedSource || !*bakedSource) ? "<empty>" : std::string("'") + bakedSource + "'");
        return false;
    }

    Path const sourceDirectory(resolvedSource);

    UpdateFetcher updateFetcher(sourceDirectory, [&](std::string const& query) { DBUpdater<T>::Apply(pool, query); },
        [&](Path const& file) { DBUpdater<T>::ApplyFile(pool, file); },
            [&](std::string const& query) -> QueryResult { return DBUpdater<T>::Retrieve(pool, query); });

    UpdateResult result;
    try
    {
        result = updateFetcher.Update(
            sConfigMgr->GetBoolDefault("Updates.Redundancy", true),
            sConfigMgr->GetBoolDefault("Updates.AllowRehash", true),
            sConfigMgr->GetBoolDefault("Updates.ArchivedRedundancy", false),
            sConfigMgr->GetIntDefault("Updates.CleanDeadRefMaxCount", 3));
    }
    catch (UpdateException&)
    {
        return false;
    }

    std::string const info = Trinity::StringFormat("Containing {} new and {} archived updates.",
        result.recent, result.archived);

    if (!result.updated)
        TC_LOG_INFO("sql.updates", ">> {} database is up-to-date! {}", DBUpdater<T>::GetTableName(), info);
    else
        TC_LOG_INFO("sql.updates", ">> Applied {} {}. {}", result.updated, result.updated == 1 ? "query" : "queries", info);

    return true;
}

template<class T>
bool DBUpdater<T>::Populate(DatabaseWorkerPool<T>& pool)
{
    {
        QueryResult const result = Retrieve(pool, "SHOW TABLES");
        if (result && (result->GetRowCount() > 0))
            return true;
    }

    if (!DBUpdaterUtil::CheckExecutable())
        return false;

    TC_LOG_INFO("sql.updates", "Database {} is empty, auto populating it...", DBUpdater<T>::GetTableName());

    std::string const p = DBUpdater<T>::GetBaseFile();
    if (p.empty())
    {
        TC_LOG_INFO("sql.updates", ">> No base file provided, skipped!");
        return true;
    }

    Path const base(p);
    if (!exists(base))
    {
        switch (DBUpdater<T>::GetBaseLocationType())
        {
            case LOCATION_REPOSITORY:
            {
                TC_LOG_ERROR("sql.updates", ">> Base file \"{}\" is missing. Set SourceDirectory in the *.conf to a folder containing sql/ (or ship the sql folder with the server binaries).",
                    base.generic_string());

                break;
            }
            case LOCATION_DOWNLOAD:
            {
                std::string const filename = base.filename().generic_string();
                std::string const workdir = boost::filesystem::current_path().generic_string();
                TC_LOG_ERROR("sql.updates", ">> File \"{}\" is missing, download it from \"https://github.com/TrinityCore/TrinityCore/releases\"" \
                    " uncompress it and place the file \"{}\" in the directory \"{}\".", filename, filename, workdir);
                break;
            }
        }
        return false;
    }

    // Update database
    TC_LOG_INFO("sql.updates", ">> Applying \'{}\'...", base.generic_string());
    try
    {
        ApplyFile(pool, base);
    }
    catch (UpdateException&)
    {
        return false;
    }

    TC_LOG_INFO("sql.updates", ">> Done!");
    return true;
}

template<class T>
QueryResult DBUpdater<T>::Retrieve(DatabaseWorkerPool<T>& pool, std::string const& query)
{
    return pool.Query(query.c_str());
}

template<class T>
void DBUpdater<T>::Apply(DatabaseWorkerPool<T>& pool, std::string const& query)
{
    pool.DirectExecute(query.c_str());
}

template<class T>
void DBUpdater<T>::ApplyFile(DatabaseWorkerPool<T>& pool, Path const& path)
{
    DBUpdater<T>::ApplyFile(pool, pool.GetConnectionInfo()->host, pool.GetConnectionInfo()->user, pool.GetConnectionInfo()->password,
        pool.GetConnectionInfo()->port_or_socket, pool.GetConnectionInfo()->database, pool.GetConnectionInfo()->ssl, path);
}

template<class T>
void DBUpdater<T>::ApplyFile(DatabaseWorkerPool<T>& pool, std::string const& host, std::string const& user,
    std::string const& password, std::string const& port_or_socket, std::string const& database, std::string const& ssl,
    Path const& path)
{
    std::vector<std::string> args;
    args.reserve(9);

    // CLI Client connection info
    args.emplace_back("-h" + host);
    args.emplace_back("-u" + user);

    if (!password.empty())
        args.emplace_back("-p" + password);

    // Check if we want to connect through ip or socket (Unix only)
#ifdef _WIN32

    if (host == ".")
        args.emplace_back("--protocol=PIPE");
    else
        args.emplace_back("-P" + port_or_socket);

#else

    if (!std::isdigit(port_or_socket[0]))
    {
        // We can't check if host == "." here, because it is named localhost if socket option is enabled
        args.emplace_back("-P0");
        args.emplace_back("--protocol=SOCKET");
        args.emplace_back("-S" + port_or_socket);
    }
    else
        // generic case
        args.emplace_back("-P" + port_or_socket);

#endif

    // Set the default charset to utf8
    args.emplace_back("--default-character-set=utf8");

    // Set max allowed packet to 1 GB
    args.emplace_back("--max-allowed-packet=1GB");

#if !defined(MARIADB_VERSION_ID) && MYSQL_VERSION_ID >= 80000

    if (ssl == "ssl")
        args.emplace_back("--ssl-mode=REQUIRED");

#else

    if (ssl == "ssl")
        args.emplace_back("--ssl");

#endif

    // Execute sql file
    args.emplace_back("-e");
    args.emplace_back(Trinity::StringFormat("BEGIN; SOURCE {}; COMMIT;", path.generic_string()));

    // Database
    if (!database.empty())
        args.emplace_back(database);

    // Invokes a mysql process which doesn't leak credentials to logs
    int const ret = Trinity::StartProcess(DBUpdaterUtil::GetCorrectedMySQLExecutable(), args,
                                 "sql.updates", "", true);

    if (ret != EXIT_SUCCESS)
    {
        TC_LOG_FATAL("sql.updates", "Applying of file \'{}\' to database \'{}\' failed!" \
            " If you are a user, please pull the latest revision from the repository. "
            "Also make sure you have not applied any of the databases with your sql client. "
            "You cannot use auto-update system and import sql files from TrinityCore repository with your sql client. "
            "If you are a developer, please fix your sql query.",
            path.generic_string(), pool.GetConnectionInfo()->database);

        throw UpdateException("update failed");
    }
}

template class TC_DATABASE_API DBUpdater<LoginDatabaseConnection>;
template class TC_DATABASE_API DBUpdater<WorldDatabaseConnection>;
template class TC_DATABASE_API DBUpdater<CharacterDatabaseConnection>;
template class TC_DATABASE_API DBUpdater<HotfixDatabaseConnection>;
