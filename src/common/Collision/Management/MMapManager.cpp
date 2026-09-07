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

#include "MMapManager.h"
#include "Errors.h"
#include "Log.h"
#include "MMapDefines.h"
#include "MMapDataValidation.h"
#include <cerrno>
#include <cstdio>
#include <filesystem>
#include <memory>
#include <unordered_set>

namespace MMAP
{
    namespace
    {
        using File = std::unique_ptr<FILE, decltype(&fclose)>;

        FILE* OpenFile(std::string const& name)
        {
#if TRINITY_PLATFORM == TRINITY_PLATFORM_WINDOWS
            // Config paths are UTF-8; narrow fopen uses the Windows ANSI codepage.
            return _wfopen(std::filesystem::u8path(name).c_str(), L"rb");
#else
            return fopen(name.c_str(), "rb");
#endif
        }

        File OpenMapFile(std::string basePath, uint32 mapId, bool tile, int32 x, int32 y,
            std::unordered_map<uint32, uint32> const& parents, std::string& fileName)
        {
            if (!basePath.empty() && basePath.back() != '/' && basePath.back() != '\\')
                basePath += '/';

            std::unordered_set<uint32> visited;
            do
            {
                fileName = tile ? Trinity::StringFormat("{}mmaps/{:04}{:02}{:02}.mmtile", basePath, mapId, x, y)
                                : Trinity::StringFormat("{}mmaps/{:04}.mmap", basePath, mapId);
                File file(OpenFile(fileName), &fclose);
                if (file || errno != ENOENT)
                    return file;

                // Older extractors used three-digit map IDs. Only try that name
                // when the canonical file is absent, never to bypass a bad header
                // or access error. Both names must pass the same format checks.
                if (mapId < 1000)
                {
                    fileName = tile ? Trinity::StringFormat("{}mmaps/{:03}{:02}{:02}.mmtile", basePath, mapId, x, y)
                                    : Trinity::StringFormat("{}mmaps/{:03}.mmap", basePath, mapId);
                    file.reset(OpenFile(fileName));
                    if (file || errno != ENOENT)
                        return file;
                }

                if (!visited.insert(mapId).second)
                    break;
                auto parent = parents.find(mapId);
                if (parent == parents.end())
                    break;
                mapId = parent->second;
            } while (true);
            return File(nullptr, &fclose);
        }
    }

    // ######################## MMapManager ########################
    MMapManager::~MMapManager()
    {
        for (std::pair<uint32 const, MMapData*>& loadedMMap : loadedMMaps)
            delete loadedMMap.second;

        // by now we should not have maps loaded
        // if we had, tiles in MMapData->mmapLoadedTiles, their actual data is lost!
    }

    uint32 MMapManager::getLoadedMapsCount() const
    {
        uint32 count = 0;
        for (auto const& map : loadedMMaps)
            if (map.second)
                ++count;
        return count;
    }

    void MMapManager::InitializeThreadUnsafe(std::unordered_map<uint32, std::vector<uint32>> const& mapData)
    {
        // the caller must pass the list of all mapIds that will be used in the VMapManager2 lifetime
        for (std::pair<uint32 const, std::vector<uint32>> const& mapId : mapData)
        {
            loadedMMaps.insert(MMapDataSet::value_type(mapId.first, nullptr));
            for (uint32 childMapId : mapId.second)
            {
                loadedMMaps.try_emplace(childMapId, nullptr);
                parentMapData[childMapId] = mapId.first;
            }
        }

        thread_safe_environment = false;
    }

    MMapDataSet::const_iterator MMapManager::GetMMapData(uint32 mapId) const
    {
        // return the iterator if found or end() if not found/NULL
        MMapDataSet::const_iterator itr = loadedMMaps.find(mapId);
        if (itr != loadedMMaps.cend() && !itr->second)
            itr = loadedMMaps.cend();

        return itr;
    }

    bool MMapManager::loadMapData(std::string const& basePath, uint32 mapId)
    {
        // we already have this map loaded?
        MMapDataSet::iterator itr = loadedMMaps.find(mapId);
        if (itr != loadedMMaps.end())
        {
            if (itr->second)
                return true;
        }
        else
        {
            if (thread_safe_environment)
                itr = loadedMMaps.insert(MMapDataSet::value_type(mapId, nullptr)).first;
            else
            {
                TC_LOG_ERROR("maps.mmaps", "MMAP: map {} is not registered in loaded map data; refusing to load it", mapId);
                return false;
            }
        }

        // Child terrain inherits the root's coordinate system. Missing child
        // params/tiles may be inherited through more than one parent level.
        std::string fileName;
        File file = OpenMapFile(basePath, mapId, false, 0, 0, parentMapData, fileName);
        if (!file)
        {
            int error = errno;
            TC_LOG_WARN("maps.mmaps", "MMAP: cannot open params for map {} in data directory '{}' (last path '{}', OS error {}: {})",
                mapId, basePath, fileName, error, std::strerror(error));
            return false;
        }

        dtNavMeshParams params{};
        if (fread(&params, sizeof(params), 1, file.get()) != 1)
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: truncated navmesh parameters in '{}'", fileName);
            return false;
        }
        // World maps have at most 64x64 grid tiles. maxPolys is unused in this
        // 64-bit Detour build; old generators wrote the 0x80000000 sentinel.
        if (params.maxTiles <= 0 || params.maxTiles > 64 * 64 ||
            !std::isfinite(params.tileWidth) || params.tileWidth <= 0 ||
            !std::isfinite(params.tileHeight) || params.tileHeight <= 0 ||
            !std::isfinite(params.orig[0]) || !std::isfinite(params.orig[1]) || !std::isfinite(params.orig[2]))
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: invalid navmesh parameters in '{}' (tiles {}, tile size {}x{})",
                fileName, params.maxTiles, params.tileWidth, params.tileHeight);
            return false;
        }

        dtNavMesh* mesh = dtAllocNavMesh();
        if (!mesh)
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: cannot allocate navmesh for '{}'", fileName);
            return false;
        }
        dtStatus status = mesh->init(&params);
        if (dtStatusFailed(status))
        {
            dtFreeNavMesh(mesh);
            TC_LOG_ERROR("maps.mmaps", "MMAP: cannot initialize navmesh from '{}' (Detour status {:#x})", fileName, status);
            return false;
        }

        TC_LOG_DEBUG("maps.mmaps", "MMAP: loaded map {} params from '{}'", mapId, fileName);

        // store inside our map list
        MMapData* mmap_data = new MMapData(mesh);

        itr->second = mmap_data;
        return true;
    }

    uint32 MMapManager::packTileID(int32 x, int32 y)
    {
        return (uint32(x) << 16) | uint32(y);
    }

    bool MMapManager::loadMap(std::string const& basePath, uint32 mapId, int32 x, int32 y)
    {
        if (x < 0 || x >= 64 || y < 0 || y >= 64)
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: invalid grid coordinates {},{} for map {}", x, y, mapId);
            return false;
        }
        if (!loadMapData(basePath, mapId))
            return false;

        MMapData* mmap = loadedMMaps[mapId];
        uint32 packedGridPos = packTileID(x, y);
        if (mmap->loadedTileRefs.find(packedGridPos) != mmap->loadedTileRefs.end())
            return true; // Already available is success, not a missing/broken MMAP.

        std::string fileName;
        File file = OpenMapFile(basePath, mapId, true, x, y, parentMapData, fileName);
        if (!file)
        {
            int error = errno;
            TC_LOG_WARN("maps.mmaps", "MMAP: cannot open tile for map {} grid {},{} (last path '{}', OS error {}: {})",
                mapId, x, y, fileName, error, std::strerror(error));
            return false;
        }

        MmapTileHeader fileHeader;
        if (fread(&fileHeader, sizeof(fileHeader), 1, file.get()) != 1 || fileHeader.mmapMagic != MMAP_MAGIC)
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: invalid/truncated tile header in '{}'", fileName);
            return false;
        }
        if (fileHeader.mmapVersion != MMAP_VERSION || fileHeader.dtVersion != DT_NAVMESH_VERSION)
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: incompatible tile '{}' (generator v{}, Detour v{}; expected v{}, v{}). File exists but its format does not match this server.",
                fileName, fileHeader.mmapVersion, fileHeader.dtVersion, MMAP_VERSION, DT_NAVMESH_VERSION);
            return false;
        }

        long pos = ftell(file.get());
        if (pos < 0 || fseek(file.get(), 0, SEEK_END) != 0)
            return false;
        long end = ftell(file.get());
        if (end < pos || fileHeader.size < sizeof(dtMeshHeader) || fileHeader.size > uint32(std::numeric_limits<int>::max()) ||
            uint64(fileHeader.size) != uint64(end - pos) || fseek(file.get(), pos, SEEK_SET) != 0)
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: invalid tile payload size in '{}' (declared {}, available {})", fileName, fileHeader.size, end - pos);
            return false;
        }

        std::unique_ptr<unsigned char, decltype(&dtFree)> data(static_cast<unsigned char*>(dtAlloc(fileHeader.size, DT_ALLOC_PERM)), &dtFree);
        if (!data || fread(data.get(), fileHeader.size, 1, file.get()) != 1)
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: cannot allocate/read {} bytes from '{}'", fileHeader.size, fileName);
            return false;
        }
        if (char const* problem = ValidateMMapTileData(data.get(), fileHeader.size))
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: rejected '{}': {}", fileName, problem);
            return false;
        }

        dtMeshHeader const* header = reinterpret_cast<dtMeshHeader const*>(data.get());
        dtTileRef tileRef = 0;
        dtStatus status = mmap->navMesh->addTile(data.get(), int(fileHeader.size), DT_TILE_FREE_DATA, 0, &tileRef);
        if (dtStatusFailed(status))
        {
            TC_LOG_ERROR("maps.mmaps", "MMAP: cannot add '{}' to map {} (Detour status {:#x}, navmesh tile {},{}). Check that .mmap params and .mmtile files belong to the same generation.",
                fileName, mapId, status, header->x, header->y);
            return false;
        }

        data.release(); // Detour now owns the allocation.
        mmap->loadedTileRefs.emplace(packedGridPos, tileRef);
        ++loadedTiles;
        TC_LOG_DEBUG("maps.mmaps", "MMAP: loaded '{}' for map {} grid {},{} (navmesh {},{})", fileName, mapId, x, y, header->x, header->y);
        return true;
    }

    bool MMapManager::loadMapInstance(std::string const& basePath, uint32 meshMapId, uint32 instanceMapId, uint32 instanceId)
    {
        if (!loadMapData(basePath, meshMapId))
            return false;

        MMapData* mmap = loadedMMaps[meshMapId];
        auto [queryItr, inserted] = mmap->navMeshQueries.try_emplace({ instanceMapId, instanceId }, nullptr);
        if (!inserted)
            return true;

        // allocate mesh query
        dtNavMeshQuery* query = dtAllocNavMeshQuery();
        if (!query || dtStatusFailed(query->init(mmap->navMesh, 1024)))
        {
            dtFreeNavMeshQuery(query);
            mmap->navMeshQueries.erase(queryItr);
            TC_LOG_ERROR("maps", "MMAP:GetNavMeshQuery: Failed to initialize dtNavMeshQuery for mapId {:04} instanceId {}", instanceMapId, instanceId);
            return false;
        }

        TC_LOG_DEBUG("maps", "MMAP:GetNavMeshQuery: created dtNavMeshQuery for mapId {:04} instanceId {}", instanceMapId, instanceId);
        queryItr->second = query;
        return true;
    }

    bool MMapManager::unloadMap(uint32 mapId, int32 x, int32 y)
    {
        // check if we have this map loaded
        MMapDataSet::const_iterator itr = GetMMapData(mapId);
        if (itr == loadedMMaps.end())
        {
            // file may not exist, therefore not loaded
            TC_LOG_DEBUG("maps", "MMAP:unloadMap: Asked to unload not loaded navmesh map. {:04}{:02}{:02}.mmtile", mapId, x, y);
            return false;
        }

        MMapData* mmap = itr->second;

        // check if we have this tile loaded
        uint32 packedGridPos = packTileID(x, y);
        auto tileRefItr = mmap->loadedTileRefs.find(packedGridPos);
        if (tileRefItr == mmap->loadedTileRefs.end())
        {
            // file may not exist, therefore not loaded
            TC_LOG_DEBUG("maps", "MMAP:unloadMap: Asked to unload not loaded navmesh tile. {:04}{:02}{:02}.mmtile", mapId, x, y);
            return false;
        }

        // unload, and mark as non loaded
        if (dtStatusFailed(mmap->navMesh->removeTile(tileRefItr->second, nullptr, nullptr)))
        {
            // this is technically a memory leak
            // if the grid is later reloaded, dtNavMesh::addTile will return error but no extra memory is used
            // we cannot recover from this error - assert out
            TC_LOG_ERROR("maps", "MMAP:unloadMap: Could not unload {:04}{:02}{:02}.mmtile from navmesh", mapId, x, y);
            ABORT();
        }
        else
        {
            mmap->loadedTileRefs.erase(tileRefItr);
            --loadedTiles;
            TC_LOG_DEBUG("maps", "MMAP:unloadMap: Unloaded mmtile {:04}[{:02}, {:02}] from {:03}", mapId, x, y, mapId);
            return true;
        }

        return false;
    }

    bool MMapManager::unloadMap(uint32 mapId)
    {
        MMapDataSet::iterator itr = loadedMMaps.find(mapId);
        if (itr == loadedMMaps.end() || !itr->second)
        {
            // file may not exist, therefore not loaded
            TC_LOG_DEBUG("maps", "MMAP:unloadMap: Asked to unload not loaded navmesh map {:04}", mapId);
            return false;
        }

        // unload all tiles from given map
        MMapData* mmap = itr->second;
        for (MMapTileSet::iterator i = mmap->loadedTileRefs.begin(); i != mmap->loadedTileRefs.end(); ++i)
        {
            uint32 x = (i->first >> 16);
            uint32 y = (i->first & 0x0000FFFF);
            if (dtStatusFailed(mmap->navMesh->removeTile(i->second, nullptr, nullptr)))
                TC_LOG_ERROR("maps", "MMAP:unloadMap: Could not unload {:04}{:02}{:02}.mmtile from navmesh", mapId, x, y);
            else
            {
                --loadedTiles;
                TC_LOG_DEBUG("maps", "MMAP:unloadMap: Unloaded mmtile {:04}[{:02}, {:02}] from {:04}", mapId, x, y, mapId);
            }
        }

        delete mmap;
        itr->second = nullptr;
        TC_LOG_DEBUG("maps", "MMAP:unloadMap: Unloaded {:04}.mmap", mapId);

        return true;
    }

    bool MMapManager::unloadMapInstance(uint32 meshMapId, uint32 instanceMapId, uint32 instanceId)
    {
        // check if we have this map loaded
        MMapDataSet::const_iterator itr = GetMMapData(meshMapId);
        if (itr == loadedMMaps.end())
        {
            // file may not exist, therefore not loaded
            TC_LOG_DEBUG("maps", "MMAP:unloadMapInstance: Asked to unload not loaded navmesh map {:04}", meshMapId);
            return false;
        }

        MMapData* mmap = itr->second;
        auto queryItr = mmap->navMeshQueries.find({ instanceMapId, instanceId });
        if (queryItr == mmap->navMeshQueries.end())
        {
            TC_LOG_DEBUG("maps", "MMAP:unloadMapInstance: Asked to unload not loaded dtNavMeshQuery mapId {:04} instanceId {}", instanceMapId, instanceId);
            return false;
        }

        dtFreeNavMeshQuery(queryItr->second);
        mmap->navMeshQueries.erase(queryItr);
        TC_LOG_DEBUG("maps", "MMAP:unloadMapInstance: Unloaded mapId {:04} instanceId {}", instanceMapId, instanceId);

        return true;
    }

    dtNavMesh const* MMapManager::GetNavMesh(uint32 mapId)
    {
        MMapDataSet::const_iterator itr = GetMMapData(mapId);
        if (itr == loadedMMaps.end())
            return nullptr;

        return itr->second->navMesh;
    }

    dtNavMeshQuery const* MMapManager::GetNavMeshQuery(uint32 meshMapId, uint32 instanceMapId, uint32 instanceId)
    {
        auto itr = GetMMapData(meshMapId);
        if (itr == loadedMMaps.end())
            return nullptr;

        auto queryItr = itr->second->navMeshQueries.find({ instanceMapId, instanceId });
        if (queryItr == itr->second->navMeshQueries.end())
            return nullptr;

        return queryItr->second;
    }
}
