# AI Playerbots (ike3/mangosbot port)

This server embeds a port of [ike3/mangosbot](https://github.com/ike3/mangosbot)
("AI Playerbot") adapted to this WoW 3.4.3 TrinityCore-derived core.

Bots are *real characters*: they are loaded from the `characters` database
through a socket-less `WorldSession`, they walk, fight, loot, quest, trade,
train, use the auction house, and answer chat commands from their master.

---

## 1. Layout

| Path | Contents |
| --- | --- |
| `src/plugins/playerbot/` | the bot implementation (AI, strategies, actions, values, triggers, random bot manager) |
| `src/plugins/ahbot/` | minimal item-pricing helpers used by the bots |
| `src/plugins/CMakeLists.txt` | builds everything above into the static `plugins` library |
| `src/server/game/AI/Playerbot/PlayerbotHooks.{h,cpp}` | core-side hook registry (function pointers) |
| `src/plugins/playerbot/PlayerbotHookImpl.cpp` | plugin-side implementation that fills the hooks in |
| `src/plugins/playerbot/aiplayerbot.conf.dist` | configuration template |
| `sql/custom/playerbot/characters_playerbot.sql` | the `ai_playerbot_*` tables |

### Why hooks instead of direct calls?

`plugins` is linked *after* `game`, so the core must never reference plugin
symbols directly. Instead the plugin registers a `Playerbot::Hooks` struct of
function pointers at startup, and the core calls them through inline,
always-null-safe wrappers. If the bot system is disabled (or the library is not
linked), every hook is a no-op branch.

Core call sites (all marked with a `playerbot mod` comment):

| Core location | Hook |
| --- | --- |
| `World::Update` | `OnWorldUpdate` — drives `RandomPlayerbotMgr` |
| `Player::Update` | `OnPlayerUpdate` — drives a bot's AI and a master's bot manager |
| `WorldSession::HandlePlayerLogin` | `OnPlayerLogin` |
| `WorldSession::LogoutPlayer` | `OnPlayerLogout` |
| `Player::~Player` | `OnPlayerDelete` |
| `WorldSession::SendPacket` | `OnBotPacketSent` — bot sessions have no socket, packets go to the AI |
| `WorldSession::HandleChatMessage` | `OnPlayerChat` — whisper/party/raid commands |
| `worldserver/Main.cpp` | `Playerbot::InitializePlayerbots()` / `ShutdownPlayerbots()` / `RegisterPlayerbotScripts()` |

Two small accessors were added to core headers so bots can read data the client
normally receives over the wire; both are marked `// playerbot`:

* `Trainer::Trainer::GetSpells()` / `GetSpellStateForPlayer()` (`Entities/Creature/Trainer.h`)
* `Group::GetRolls()` (`Groups/Group.h`)

---

## 2. Building

Nothing special — the `plugins` target is part of the normal CMake build and is
linked into `worldserver` (link order: `scripts plugins game`).

```sh
cmake -B build -S . -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build build -j$(nproc)
cmake --install build
```

`cmake --install` also drops `aiplayerbot.conf.dist` into
`<conf dir>/worldserver.conf.d/`.

> **Note:** build with the default static linking (`WITH_DYNAMIC_LINKING=0`).
> With shared libraries the core is compiled with `-fvisibility=hidden` and the
> bots reference plenty of core functions that are not marked `TC_GAME_API`,
> which would fail to link.

---

## 3. Database setup

Apply the schema to the **characters** database once:

```sh
mysql -u trinity -p characters < sql/custom/playerbot/characters_playerbot.sql
```

It creates:

| Table | Purpose |
| --- | --- |
| `ai_playerbot_random_bots` | which characters are random bots + scheduled events |
| `ai_playerbot_names` | name pool used when generating random bot characters |
| `ai_playerbot_guild_names` | name pool for generated bot guilds |
| `ai_playerbot_custom_strategy` | user-defined strategies (`action_line`) |
| `ai_playerbot_tellitem` | remembers items a bot already reported |
| `ai_playerbot_guild_tasks` | guild task state |
| `ai_playerbot_texts` | localized bot chat lines |

The script is idempotent (`CREATE TABLE IF NOT EXISTS` + `INSERT IGNORE`) so it
can be re-run safely.

---

## 4. Configuration

Copy `aiplayerbot.conf.dist` to `aiplayerbot.conf` in your
`worldserver.conf.d/` directory (the installer already places the `.dist`
file there). Every key also has a sane default compiled in, so an empty file
still works.

Key settings:

```ini
AiPlayerbot.Enabled = 1                 # master switch
AiPlayerbot.AllowGuildBots = 1
AiPlayerbot.RandomBotAutologin = 1      # keep a population of random bots online
AiPlayerbot.MinRandomBots = 50
AiPlayerbot.MaxRandomBots = 200
AiPlayerbot.RandomBotAccountPrefix = rndbot
AiPlayerbot.RandomBotAccountCount = 50
AiPlayerbot.RandomBotMinLevel = 1
AiPlayerbot.RandomBotMaxLevel = 80
AiPlayerbot.CommandPrefix =             # e.g. "!" if you want "!follow"
AiPlayerbot.CommandServerPort = 0       # 0 disables the TCP command server
```

Spec probabilities per class are configured with
`AiPlayerbot.RandomClassSpecProbability.<class>.<spec>`.

---

## 5. Using bots

### In-game commands

```
.bot add <name>          add one of your own characters as a bot
.bot addclass <class>    add a random bot of the given class
.bot remove <name>       log the bot out
.bot list                list your bots
.bot init=<level>        gear/level a bot up
.rndbot <subcommand>     control the random bot population (GM/console)
```

`.bot` requires GM permission by default (`RBAC_PERM_COMMAND_GM`); the security
level required for *other people's* bots is additionally checked by
`PlayerbotSecurity`.

### Chat commands

Whisper a bot, or talk in party/raid chat, to steer it:

```
follow            follow the master
stay              hold position
flee              run away when in trouble
attack my target  focus the master's target
grind             kill nearby mobs
los / nc          list strategies (combat / non-combat)
+dps -threat      enable / disable a strategy
quests            report quest log
talk              talk to the selected quest giver
trade / buy / sell / repair / train
stats / spells / items
```

Prefix them with `AiPlayerbot.CommandPrefix` if you configured one.

### Random bots

With `AiPlayerbot.RandomBotAutologin = 1` the `RandomPlayerbotMgr` keeps
`MinRandomBots`..`MaxRandomBots` characters online, creating accounts named
`<RandomBotAccountPrefix><n>` and characters from `ai_playerbot_names` as
needed, then teleporting/levelling/gearing them and cycling them over time.

---

## 6. Porting notes (3.3.5 → 3.4.3)

The upstream mangosbot code targets MaNGOS/TrinityCore 3.3.5. The main API
migrations applied during the port:

* **Packets** — every `WorldSession::Handle*Opcode(WorldPacket&)` call became a
  typed `WorldPackets::X::Y` structure, e.g.
  `WorldPackets::NPC::Hello hello{WorldPacket(CMSG_TALK_TO_GOSSIP)}; hello.Unit = guid;`
* **GUIDs** — 64-bit `uint64` GUIDs replaced by 128-bit `ObjectGuid`
  (`IsEmpty()`, `GetCounter()`, `ObjectGuid::Create<HighGuid::Player>(...)`),
  including the chat-link (`|Hitem:`/`|Hplayer:`) parsing in `ChatHelper`.
* **Items** — `ItemTemplate` is accessor-only (`GetClass()`, `GetSubClass()`,
  `GetBonding()`, `GetAllowableRace()` → `Trinity::RaceMask`); item spells are
  read through the `ItemEffect*` helpers added in `playerbot.h`.
* **Quests** — `Quest::GetObjectives()` (`QuestObjective`) replaced the fixed
  `RequiredItemId[]`/`RequiredNpcOrGo[]` arrays; progress is read with
  `Player::GetQuestObjectiveData()`; `CanRewardQuest`/`RewardQuest` take a
  `LootItemType`.
* **Trainers** — `sObjectMgr->GetTrainer(entry)` plus the new `Trainer` accessors.
* **Spells** — `sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE)`,
  `SpellInfo::SpellName->Str[loc]`, `SpellInfo::CalcPowerCost(...)`.
* **Loot** — `WorldObject::HasDynamicFlag()`, `CreatureTemplate::GetDifficulty(DIFFICULTY_NONE)->GetRequiredLootSkill()`,
  `Group::CountRollVote(playerGuid, lootObjGuid, lootListId, vote)`.
* **Talents/specs** — `ActivateTalentGroup()` / `GetActiveTalentGroup()`.
* **Database** — `PQuery`/`PExecute` use `{}` fmt placeholders instead of `%s`/`%u`.
* **Bit-packed server packets** (e.g. `SMSG_TRADE_STATUS`) are parsed with
  `ResetBitPos()` / `ReadBit()` / `ReadBits(n)`.

---

## 7. Limitations & known issues

* **Spell rotations need tuning** — the class combat strategies (warrior, mage,
  priest, etc.) were ported from the 3.3.5 mangosbot codebase and still
  reference spell names that may differ between 3.3.5 and 3.4.3.  The bots will
  fight, but their rotations may not be optimal.  The spell names are defined
  in the strategy files under `strategy/<class>/` and can be adjusted.
* **Static linking required** — the plugin calls many core functions that lack
  `TC_GAME_API` exports.  Build with `-DWITH_DYNAMIC_LINKING=0` (the default).
  If `BUILD_SHARED_LIBS` is detected, CMake will emit a warning.
* **3.3.5 features without 3.4.3 equivalents** — ranged ammo checks, numeric
  gossip `OptionType` matching, and `GameObject` spellcaster teleports were
  removed rather than emulated.
* **No AhBot** — ike3's auction-house bot is not ported.  Use the core's own
  `AuctionHouseBot` module.  Only the item-pricing helpers the bots need were
  kept in `src/plugins/ahbot/`.
* **Windows builds** — Windows support is best-effort.  The CI runs a Windows
  build using vcpkg for dependencies, but it is marked non-blocking due to the
  fragility of the Windows dependency chain.  Linux is the primary supported
  platform.

---

## 8. CI/CD

This repository includes two GitHub Actions workflows:

### Build (`build.yml`)

Runs automatically on every push to `main` and on every pull request.

* **Linux** (Ubuntu 22.04, GCC 12): full build with all servers and tools.
  This is the primary CI check and **must pass**.
* **Windows** (Windows Server 2022, MSVC 2022): best-effort build using
  vcpkg for dependencies.  Marked `continue-on-error` because the Windows
  dependency chain (Boost via vcpkg) is fragile.

### Release (`release.yml`)

Triggered **manually** from the Actions tab (`Run workflow` button).

Inputs:
* **tag** — release tag name (e.g. `v3.4.3-bots-1`), or leave empty for auto
* **prerelease** — mark as pre-release (default: true)
* **build_type** — `RelWithDebInfo` or `Release`
* **build_windows** — also build Windows binaries (adds ~45 min)

The workflow builds the server, packages the binaries together with SQL
schemas, configuration files, and documentation, strips debug symbols, and
creates a GitHub Release with downloadable `.tar.gz` (Linux) and `.zip`
(Windows) archives.
