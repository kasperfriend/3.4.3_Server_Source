# World of Warcraft 3.4.3 Source Source code.

## Prerequisites

OpenSSL 3.5.7 - [Download](https://slproweb.com/download/Win64OpenSSL-3_4_6.exe)

Visual Studio 2022 Community - [Download](https://aka.ms/vs/17/release/vs_community.exe)

Boost 1.83 - [Download](https://archives.boost.io/release/1.83.0/binaries/boost_1_83_0-msvc-14.3-64.exe)

> **Boost version matters:** use **1.78 – 1.87**. Boost **1.88 and newer removed
> the Boost.Process v1 headers** (`boost/process/args.hpp` and friends) that
> `src/common/Utilities/StartProcess.cpp` needs, so the build fails with
> `error C1083: Cannot open include file: 'boost/process/args.hpp'`. CI pins
> Boost 1.87.0 by checking out vcpkg tag `2025.04.09`.


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
