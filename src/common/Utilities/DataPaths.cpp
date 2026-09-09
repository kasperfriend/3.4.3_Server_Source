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

#include "DataPaths.h"
#include "Config.h"
#include <algorithm>
#include <boost/dll/runtime_symbol_info.hpp>
#include <boost/filesystem/operations.hpp>
#include <functional>

namespace
{
namespace fs = boost::filesystem;

bool IsRegularFile(fs::path const& path)
{
    boost::system::error_code ec;
    return fs::is_regular_file(path, ec) && !ec;
}

bool IsDirectory(fs::path const& path)
{
    boost::system::error_code ec;
    return fs::is_directory(path, ec) && !ec;
}

/// Absolutize, normalize and ignore anything already in the list, so that the
/// candidates of a run where working directory, executable and configuration all
/// live in the same place collapse into a single entry.
void AddUniquePath(std::vector<fs::path>& paths, fs::path const& path)
{
    if (path.empty())
        return;

    boost::system::error_code ec;
    fs::path absolutePath = fs::absolute(path, ec);
    if (ec)
        return;

    absolutePath = absolutePath.lexically_normal();
    if (std::find(paths.begin(), paths.end(), absolutePath) == paths.end())
        paths.push_back(std::move(absolutePath));
}

bool HasRequiredContent(fs::path const& directory, std::vector<std::string> const& requiredContents)
{
    if (requiredContents.empty())
        return true;

    return std::any_of(requiredContents.begin(), requiredContents.end(), [&directory](std::string const& entry)
    {
        fs::path const child = directory / entry;
        return IsRegularFile(child) || IsDirectory(child);
    });
}

Trinity::ResolvedDataPath ResolvePath(std::string const& configName, std::string const& configuredValue, std::function<bool(fs::path const&)> const& matches, bool includeEtcSiblings)
{
    Trinity::ResolvedDataPath result{ configName, configuredValue, fs::path(configuredValue), false, {} };

    fs::path const requested{ configuredValue };
    if (requested.empty())
        return result;

    // An absolute path (on Windows: also one carrying a drive, such as "E:cert.pem")
    // is exactly what the operator asked for, so never look anywhere else. Reporting
    // it as-is keeps "your configured file is missing" honest instead of hiding the
    // reason behind a fallback that was never asked for.
    if (requested.is_absolute() || requested.has_root_path())
    {
        result.Candidates.push_back(requested);
        if (matches(requested))
        {
            result.Path = requested;
            result.Found = true;
        }
        return result;
    }

    for (fs::path const& directory : Trinity::GetConfigurationDataDirectories(includeEtcSiblings))
    {
        fs::path candidate = (directory / requested).lexically_normal();
        if (std::find(result.Candidates.begin(), result.Candidates.end(), candidate) == result.Candidates.end())
            result.Candidates.push_back(std::move(candidate));
    }

    for (fs::path const& candidate : result.Candidates)
    {
        if (matches(candidate))
        {
            result.Path = candidate;
            result.Found = true;
            return result;
        }
    }

    // Nothing matched: hand back the first candidate, which is what the operator
    // configured resolved against the working directory. Callers use it to either
    // report the failure themselves or to let the failing API name the same path.
    if (!result.Candidates.empty())
        result.Path = result.Candidates.front();

    return result;
}
}

std::vector<boost::filesystem::path> Trinity::GetConfigurationDataDirectories(bool includeEtcSiblings)
{
    std::vector<fs::path> directories;

    boost::system::error_code ec;
    AddUniquePath(directories, fs::current_path(ec));           ///< the working directory (historic behaviour)
    AddUniquePath(directories, boost::dll::program_location().parent_path()); ///< next to the executable
    AddUniquePath(directories, fs::path(sConfigMgr->GetFilename()).parent_path()); ///< next to the loaded .conf

    if (includeEtcSiblings)
    {
        std::vector<fs::path> const base = directories;
        for (fs::path const& directory : base)
            AddUniquePath(directories, directory / ".." / "etc"); ///< ..\etc of each of the above
    }

    return directories;
}

Trinity::ResolvedDataPath Trinity::ResolveDataFile(std::string const& configName, std::string const& configuredValue)
{
    return ResolvePath(configName, configuredValue, [](fs::path const& path) { return IsRegularFile(path); }, true);
}

Trinity::ResolvedDataPath Trinity::ResolveDataDirectory(std::string const& configName, std::string const& configuredValue, std::vector<std::string> const& requiredContents)
{
    return ResolvePath(configName, configuredValue, [&requiredContents](fs::path const& path) { return IsDirectory(path) && HasRequiredContent(path, requiredContents); }, false);
}

bool Trinity::EnsureDirectoryExists(boost::filesystem::path const& directory)
{
    if (directory.empty())
        return true; ///< the working directory, which exists by definition

    if (IsDirectory(directory))
        return true;

    boost::system::error_code ec;
    fs::create_directories(directory, ec);
    return IsDirectory(directory);
}

bool Trinity::EnsureParentDirectoryExists(boost::filesystem::path const& file)
{
    return EnsureDirectoryExists(file.parent_path());
}

std::string Trinity::DescribeDataPathCandidates(std::vector<boost::filesystem::path> const& candidates)
{
    if (candidates.empty())
        return " no candidate paths";

    std::string result;
    for (fs::path const& candidate : candidates)
    {
        result += "\n";
        result += "        * ";
        result += candidate.generic_string();
    }
    return result;
}
