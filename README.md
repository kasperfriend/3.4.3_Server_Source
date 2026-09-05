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

### Building on Linux (local)

```bash
# Debian / Ubuntu
sudo apt install build-essential gcc-12 g++-12 cmake \
  libssl-dev libmysqlclient-dev libboost-all-dev \
  libreadline-dev zlib1g-dev libbz2-dev libncurses-dev

cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DWITH_DYNAMIC_LINKING=0 -DSCRIPTS=static
cmake --build build -j$(nproc)
cmake --install build --prefix $HOME/trinitycore
```
