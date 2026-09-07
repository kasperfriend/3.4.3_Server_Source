# MMAP loading and bot world-data fixes

## A load warning does not prove a file is missing

The old code could print `Could not load MMAP` even with correctly placed,
compatible files:

1. A height/area query loaded a terrain grid and its MMAP tile.
2. A Map/instance later referenced the same grid and requested it again.
3. `MMapManager::loadMap` returned **false** for an already-loaded tile.
4. The caller reported that as an MMAP load failure.

Repeated loads now succeed without incrementing tile counts, and terrain loading
rechecks the loaded-grid flag under its load lock. MMAP statistics count actual
loaded meshes rather than every map ID registered at startup.

**Do not move or re-extract all your maps merely because of that old warning.**

## Reader and generator changes

- The MMAP reader accepts data paths with or without a trailing separator, and
  uses the wide-character file API for UTF-8 MMAP paths on Windows.
- It tries the current four-digit map-ID filenames first, then compatible
  three-digit names **only if the current filename is absent**. It does not
  fall back around permission errors or a corrupt/incompatible current file.
  This is filename compatibility, not permission to load an old binary format.
- Missing child-map parameters and tiles can inherit through the parent chain.
  The reader does not swap tile axes or rename existing data files.
- Generator version, Detour version, lengths, element counts and payload layout
  are checked before Detour uses file data. An existing file can be rejected for
  these reasons; logs now identify the actual path and reason. Bad reads release
  their allocations, and partial Detour initialization can be cleaned up safely.
- The MMAP generator's VMAP-only discovery used transposed coordinates. It now
  agrees with the terrain-map branch. `.tilelist` files are no longer treated as
  terrain tiles.
- Generator initialization checks Detour's status bits correctly, rather than
  treating a nonzero failure status as success. It avoids writing empty mesh
  parameters and no longer computes signed `1 << 31` for polygon capacity.
- Incremental generation checks cached tile contents and the current mesh origin,
  not just the version header. Truncated/incompatible tiles, or tiles tied to an
  old origin, are rebuilt rather than silently retained with new `.mmap` params.
- Empty movement paths return length zero rather than underflowing a container
  index.

The expected format remains **MMAP version 15 / Detour version 7**, with this
repository's **64-bit polygon references**. Version checks were not disabled or
relaxed. The older generator's `maxPolys = 0x80000000` parameter sentinel remains
accepted by this 64-bit reader; that field is not used for its ID bit allocation.

## World data: use what the core actually loaded

Random-bot teleport/zone-level selection no longer runs its own spatial SQL
queries against raw `creature` / `creature_template_difficulty` rows.

It uses the core's registered normal-difficulty spawn grids and loaded creature
templates instead. This avoids pulling rejected/unregistered rows or other
spawn difficulties back into bot initialization, avoids the old SQL aggregate
conversions, and respects the core's corrected level ranges. Unsupported maps,
invalid positions, missing templates and phase-specific/terrain-swap spawns are
excluded from this generic teleport pool.

The core creature loader also now:

- Rejects invalid coordinates before grid/VMAP access.
- Reads creature levels into a wide integer before narrowing, avoiding a uint8
  truncation assertion on out-of-range world rows.
- Corrects an inverted level range by setting **MaxLevel to MinLevel**, matching
  its diagnostic, rather than mistakenly lowering MinLevel.

These are runtime validation/selection fixes. They do not delete or rewrite
world database rows. Out-of-range values are logged and clamped in memory.

Nearby higher-level spawns are now treated as danger, instead of lower-level
ones. When `mmap.enablePathFinding` is enabled, a random teleport candidate must
also have a navigable ground/steep-ground polygon in the destination map's MMAP
query. An existing file alone does not mean every point is walkable. Normal
explicit pathfinding-disable behavior is preserved; it is not the recommended
way to conceal a data error.

Spawn snapshots and level-location caches last for the server run. Restart after
importing/changing world data when testing these selections.

## Applying this update

1. Keep your existing `maps`, `vmaps`, and `mmaps` directories in place.
2. Stop worldserver, regenerate CMake, rebuild x64 **RelWithDebInfo**, and deploy
   the matching `worldserver.exe` and PDB. The source still uses the unified
   `worldserver.conf` for all bot settings.
3. Test a small bot population. Check the `maps.mmaps`, `mmaps.tiles`, `playerbot`
   and `sql.sql` log categories.
4. Only if a specific file is genuinely rejected for format/content, investigate
   that file/generation. If using `mmaps_generator`, use the rebuilt tool and
   matching terrain/VMAP inputs; do not change version bytes to force a load or
   copy unrelated tiles into a failing grid position.

Useful diagnostic distinctions:

| Message | Meaning |
| --- | --- |
| `cannot open ... OS error ...` | The OS could not open the exact reported path after filename/parent resolution. Not a Detour format diagnosis. |
| `incompatible tile ... File exists` | The file was opened, but its MMAP/Detour version differs. |
| `payload size/layout mismatch` | File contents do not match the reader's serialized structure, including polygon-reference width. |
| `cannot add ... Detour status ...` | File validation succeeded but Detour rejected insertion; the log includes navmesh coordinates and status. Mixed/stale params and tiles are one possible cause. |
| `no candidate passed terrain/area/MMAP checks` | The bot found candidate spawn locations but none passed destination validation. This does not claim that all map files are absent. |
| `Bot world-data cache: map ... has ... usable ... spawns` | The size of the core-derived bot teleport pool, not a raw database row count. |

## Verification and limits

```bat
python -m unittest discover -s tests -p "playerbot_*_test.py" -v
```

The MMAP follow-up added four tests (14 at that point). The subsequent
[packet audit](PacketCompatibility.md) and [login audit](FreshLoginSafety.md)
bring the combined suite to **23 tests**. The MMAP test compiles the real
loader and bundled Detour, builds navigation data, writes real fixture files,
and performs real load/unload and polygon queries. It covers repeated loading,
parent inheritance, both filename widths, spaces/Unicode paths, corrupt/version
mismatches, allocation failures, destination navigation, and incremental-cache
origin checks. Generator discovery and terrain/world-selection tests exercise
production functions with isolated surrounding state.

GCC checks undefined behavior and floating-to-integer conversion; the MMAP
integration test excludes Detour's established 4-byte/64-bit-link alignment
issue so it does not change the existing on-disk ABI. Tests elsewhere retain
checked STL iterators and their existing sanitizer coverage.

A full Windows build, Windows wide-path execution and a run against the user's
actual map set/database have **not** been performed here. The live files and
world dump are not in this repository checkout. Remaining spell/script/opcode
warnings require their specific updated log entries and affected IDs; file
placement alone cannot identify which database rows need correction. No bulk
world-data replacement or guessed SQL deletion was performed.
