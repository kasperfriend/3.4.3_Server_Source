"""MMAP IO/Detour integration and world-spawn regressions, without client data.

The loader test compiles the real MMapManager and bundled Detour implementation.
Only log/error sinks and an unused game-specific filter header are shimmed.
"""

import os
from pathlib import Path
import unittest

from playerbot_logic_test import ROOT, run_cpp, source_slice
from playerbot_safety_test import function


MANAGER = "src/plugins/playerbot/RandomPlayerbotMgr.cpp"


class PlayerbotMMapTest(unittest.TestCase):
    def test_real_mmap_loader_and_detour(self):
        headers = {
            "Log.h": '''#pragma once
#include "StringFormat.h"
#include <fmt/format.h>
#include <vector>
namespace TestLog { inline std::vector<std::string> messages; }
#define TC_LOG_DEBUG(category, ...) TestLog::messages.push_back(Trinity::StringFormat(__VA_ARGS__))
#define TC_LOG_WARN(category, ...) TestLog::messages.push_back(Trinity::StringFormat(__VA_ARGS__))
#define TC_LOG_ERROR(category, ...) TestLog::messages.push_back(Trinity::StringFormat(__VA_ARGS__))
''',
            "Errors.h": '''#pragma once
#include <stdexcept>
#define ASSERT(condition, ...) do { if (!(condition)) throw std::runtime_error("core assertion: " #condition); } while(false)
#define ABORT() throw std::runtime_error("core abort")
''',
            "DetourFilters.h": '#pragma once\n#include "DetourNavMeshQuery.h"\n',
        }
        sources = ["src/common/Collision/Management/MMapManager.cpp"]
        sources.extend(str(p.relative_to(ROOT)) for p in sorted((ROOT / "dep/recastnavigation/Detour/Source").glob("*.cpp")))
        text = Path(__file__).with_name("mmap_loader_test.cpp.in").read_text()
        text = text.replace("@TILE_CACHE@", function("src/tools/mmaps_generator/MapBuilder.cpp", "bool TileBuilder::shouldSkipTile("))
        run_cpp(self, text, sources=sources,
                includes=["src/common", "src/common/Utilities", "src/common/Collision/Management", "src/common/Collision/Maps",
                          "dep/recastnavigation/Detour/Include", "dep/fmt/include"],
                headers=headers, defines=["FMT_HEADER_ONLY"],
                # Detour's established on-disk ABI uses 4-byte alignment even
                # for 64-bit links. Keep its binary format, exclude that known
                # legacy alignment issue from this test's UBSan instrumentation.
                gcc_flags=["-fno-sanitize=alignment"])

    def test_world_spawn_selection(self):
        text = Path(__file__).with_name("world_spawn_test.cpp.in").read_text()
        text = text.replace("@WORLD_DATA@", source_slice(MANAGER, "namespace\n{\n    struct BotWorldSpawn", "RandomPlayerbotMgr::RandomPlayerbotMgr()"))
        text = text.replace("@ZONE_LEVEL@", function(MANAGER, "uint32 RandomPlayerbotMgr::GetZoneLevel("))
        text = text.replace("@LEVEL_READS@", source_slice(
            "src/server/game/Globals/ObjectMgr.cpp", "        // Read before narrowing:", "        creatureDifficulty.HealthScalingExpansion"))
        text = text.replace("@LEVEL_ORDER@", source_slice(
            "src/server/game/Globals/ObjectMgr.cpp", "        if (creatureDifficulty.MinLevel > creatureDifficulty.MaxLevel)", "        if (creatureDifficulty.HealthScalingExpansion"))
        run_cpp(self, text)

    def test_generator_discovery_and_terrain_loads(self):
        text = Path(__file__).with_name("terrain_discovery_test.cpp.in").read_text()
        for marker, path, signature in [
            ("DISCOVER", "src/tools/mmaps_generator/MapBuilder.cpp", "void MapBuilder::discoverTiles()"),
            ("TERRAIN_LOAD", "src/server/game/Maps/TerrainMgr.cpp", "void TerrainInfo::LoadMapAndVMap("),
            ("GET_GRID", "src/server/game/Maps/TerrainMgr.cpp", "GridMap* TerrainInfo::GetGrid("),
            ("PATH_LENGTH", "src/server/game/Movement/PathGenerator.cpp", "float PathGenerator::GetPathLength() const"),
        ]:
            text = text.replace(f"@{marker}@", function(path, signature))
        run_cpp(self, text, includes=["src/common", "src/common/Utilities", "src/tools/mmaps_generator", "dep/fmt/include"],
                defines=["FMT_HEADER_ONLY", *(["WIN32"] if os.name == "nt" else [])])

    def test_no_raw_world_spatial_sql_or_false_mmap_failure(self):
        source = (ROOT / MANAGER).read_text()
        self.assertNotIn("WorldDatabase.PQuery", source)
        loader = function("src/common/Collision/Management/MMapManager.cpp", "bool MMapManager::loadMap(")
        duplicate = loader.index("mmap->loadedTileRefs.find")
        self.assertLess(loader.index("return true", duplicate), loader.index("OpenMapFile", duplicate))
        terrain = function("src/server/game/Maps/TerrainMgr.cpp", "void TerrainInfo::LoadMapAndVMap(")
        self.assertIn("if (!_loadedGrids[GetBitsetIndex(gx, gy)])", terrain)
        creatures = function("src/server/game/Globals/ObjectMgr.cpp", "void ObjectMgr::LoadCreatures()")
        self.assertLess(creatures.index("MapManager::IsValidMapCoord"), creatures.index("Trinity::ComputeGridCoord"))
        builder = (ROOT / "src/tools/mmaps_generator/MapBuilder.cpp").read_text()
        self.assertNotIn("if (!navMesh->init", builder)
        self.assertNotIn("1 << polyBits", builder)


if __name__ == "__main__":
    unittest.main()
