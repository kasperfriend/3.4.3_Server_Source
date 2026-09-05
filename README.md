# World of Warcraft 3.4.3 Source Source code.

## Prerequisites

OpenSSL 3.5.7 - [Download](https://slproweb.com/download/Win64OpenSSL-3_4_6.exe)

Visual Studio 2022 Community - [Download](https://aka.ms/vs/17/release/vs_community.exe)

Boost 1.83 - [Download](https://archives.boost.io/release/1.83.0/binaries/boost_1_83_0-msvc-14.3-64.exe)

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
