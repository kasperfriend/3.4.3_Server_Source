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

#ifndef TRINITY_DATA_PATHS_H
#define TRINITY_DATA_PATHS_H

#include <boost/filesystem/path.hpp>
#include <string>
#include <vector>

namespace Trinity
{
/*
 * Paths taken from a configuration file used to be resolved strictly against the
 * working directory, which silently breaks every layout where the data lives next
 * to the executable while the server is started from somewhere else: bnetserver
 * refusing to start without its TLS certificate, worldserver not finding its maps,
 * log files vanishing into whatever directory the process happened to be started
 * from. ConfigMgr::ResolveConfigPath in the two Main.cpp files already searches the
 * usual locations for the .conf; these helpers do the same for the remaining
 * file and directory settings, and report what was looked at when nothing matches.
 */

/// Everything needed to resolve and to report a configured data path.
struct ResolvedDataPath
{
    std::string ConfigName;                    ///< configuration field the value came from
    std::string ConfiguredValue;               ///< value as written in the configuration file
    boost::filesystem::path Path;              ///< the path to use
    bool Found;                                ///< false when nothing matched
    std::vector<boost::filesystem::path> Candidates; ///< every location that was considered
};

/// The directories relative configuration paths are resolved against, in priority
/// order: the working directory (the historic behaviour, and the one the servers are
/// normally started from), the directory of the executable, the directory of the
/// loaded configuration file - plus the "etc" sibling of each of those, which is the
/// configuration directory of the portable layout.
std::vector<boost::filesystem::path> GetConfigurationDataDirectories(bool includeEtcSiblings = true);

/// Resolves a configuration field pointing at a file. An absolute value (on Windows
/// also one carrying a drive) is used exactly as written, everything else is
/// searched in GetConfigurationDataDirectories. When nothing is found, Path is the
/// first candidate, i.e. the configured value resolved against the working
/// directory, so the caller can both open it (and let the API report the error) and
/// explain where it looked.
ResolvedDataPath ResolveDataFile(std::string const& configName, std::string const& configuredValue);

/// Resolves a configuration field pointing at a directory. When requiredContents is
/// not empty a candidate only matches if it holds at least one of those entries, so
/// that DataDir can be required to actually contain a data tree instead of just
/// being an existing directory.
ResolvedDataPath ResolveDataDirectory(std::string const& configName, std::string const& configuredValue, std::vector<std::string> const& requiredContents = {});

/// Creates the directory when it does not exist yet. Returns false when it is still
/// not a usable directory afterwards, which lets the caller report a message that
/// names the directory it tried to use instead of the caller's own failure.
bool EnsureDirectoryExists(boost::filesystem::path const& directory);

/// Same, for the directory a file path points into.
bool EnsureParentDirectoryExists(boost::filesystem::path const& file);

/// Newline separated, indented list of the searched locations, for error messages.
std::string DescribeDataPathCandidates(std::vector<boost::filesystem::path> const& candidates);
}

#endif
