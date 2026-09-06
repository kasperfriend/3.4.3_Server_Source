# World of Warcraft 3.4.3 Source Source code.

## Prerequisites

OpenSSL 3.5.7 - [Download](https://slproweb.com/download/Win64OpenSSL-3_4_6.exe)

Visual Studio 2022 Community - [Download](https://aka.ms/vs/17/release/vs_community.exe)

Boost 1.83 - [Download](https://archives.boost.io/release/1.83.0/binaries/boost_1_83_0-msvc-14.3-64.exe)

> **Any Boost from 1.78 upwards works**, including current releases. Boost 1.86
> moved the Boost.Process v1 headers under `boost/process/v1/` and Boost 1.88
> removed the old top-level paths; `src/common/Utilities/StartProcess.cpp`
> selects the right set via `BOOST_VERSION`, and `dep/boost/CMakeLists.txt`
> defines `BOOST_PROCESS_VERSION=1` so the v1 API stays visible. Boost 1.87 also
> stopped pulling `<boost/filesystem/directory.hpp>` in from
> `<boost/filesystem/operations.hpp>` and dropped `<boost/asio/io_service.hpp>`;
> both are handled in the source as well.


Latest version of CMake - [Download](https://cmake.org/download/)

MySQL 8.0 - [Download](https://dev.mysql.com/downloads/windows/installer/8.0.html)

3.4.3 Client - [Download](https://drive.google.com/file/d/1rbu3qp2AIk6j2VFmx2yAb8nTIANRZLqp/view)

## AI Playerbots

This source tree includes a port of [ike3/mangosbot](https://github.com/ike3/mangosbot)
AI Playerbots, living in `src/plugins/playerbot`.

Quick start:

1. Build normally — the `plugins` library is part of the CMake build.
2. Import `sql/custom/playerbot/characters_playerbot.sql` into your **characters** database.
3. Copy `src/plugins/playerbot/aiplayerbot.conf.dist` to
   `<config dir>/worldserver.conf.d/aiplayerbot.conf` (`cmake --install` does this for you)
   and set `AiPlayerbot.Enabled = 1`.
4. In game: `.bot add <charactername>`, then whisper the bot `follow`, `attack my target`, `stay`, …

See [docs/Playerbots.md](docs/Playerbots.md) for the full documentation:
architecture, hook points, configuration reference, chat commands, random bot
population management and the 3.3.5 → 3.4.3 porting notes.

## Continuous Integration & Releases

This repository uses GitHub Actions:

* **Build workflow** (`.github/workflows/build.yml`) — automatically builds the
  project on every push to `main` and on every pull request, verifying that the
  server and the playerbot plugin compile and link correctly with MSVC on
  Windows Server 2022.  Windows is the only CI target and the job is blocking;
  a failure posts the extracted compiler errors back to the pull request.
* **Release workflow** (`.github/workflows/release.yml`) — triggered manually
  from the **Actions** tab (`Run workflow`).  Builds the full server with
  playerbots for Windows x64, packages the binaries together with the SQL
  schemas, configuration files, documentation and every required DLL, and
  creates a GitHub Release with a downloadable `.zip`.
* **Dependencies** are installed with the vcpkg copy that ships with the GitHub
  runner image. It is *deliberately not pinned* to an older release: an old
  vcpkg asks the MSYS2 mirrors for package revisions that were deleted upstream
  (`msys2-runtime-3.5.4-2`), so every port running a pkgconfig fixup — bzip2,
  and openssl — fails with HTTP 404 and the dependency step dies. Keeping vcpkg
  current and supporting modern Boost in the source is the only combination that
  stays green over time.

## Building (Windows only)

**This tree builds on Windows with MSVC only.** Linux / macOS support was
removed on purpose — the Unix build files, compiler settings and the Linux CI
job are gone, and `cmake` now stops with a clear error if you configure it on
anything but Windows. If you need a Unix build, use upstream TrinityCore.

Install the prerequisites listed at the top of this file, then from a
*Developer Command Prompt for VS 2022* (or with CMake GUI):

```bat
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_BUILD_TYPE=RelWithDebInfo ^
  -DSERVERS=1 -DTOOLS=1 -DSCRIPTS=static ^
  -DWITH_DYNAMIC_LINKING=0 ^
  -DUSE_COREPCH=1 -DUSE_SCRIPTPCH=1

cmake --build build --config RelWithDebInfo
cmake --install build --config RelWithDebInfo --prefix C:\TrinityCore
```

> **Keep the precompiled headers enabled** (`USE_COREPCH=1`, `USE_SCRIPTPCH=1`,
> which are the defaults). The core headers rely on the include set the PCHs
> provide; with `-DUSE_COREPCH=0` MSVC 14.4x no longer pulls most of the
> standard library in transitively and the build dies in headers that are
> otherwise fine, e.g. `SharedDefines.h: error C2039: 'unordered_map': is not a
> member of 'std'` followed by thousands of cascading errors.

### Troubleshooting

**"Error: generator toolset: Does not match the toolset used previously: host=x64"**

This means CMake is finding a stale `CMakeCache.txt` in your **build directory**
that was created by an earlier configure with different settings.  Removing and
re-cloning the *source* directory does not fix it — the cache lives in the
build directory (the folder you passed to `-B`), not in the source tree.

Fix: delete the build directory and start over:

```bat
rmdir /s /q build
cmake -B build -S . -G "Visual Studio 17 2022" -A x64 ...
```

Or, if you are using the CMake GUI, change the **build directory** path to a
fresh, empty folder.  The source directory should point to the cloned source
tree; the build directory must be a separate, empty folder.
