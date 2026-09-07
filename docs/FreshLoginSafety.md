# Fresh-start connection and login safety audit

Date: 2026-09-07

## Scope

This pass follows a first user from a freshly started server through the
Battle.net/world connection, authentication, character-load completion, entry
into the world, interaction with the bot system, and disconnect/logout. It also
checks a returning character with stale/corrupt saved state. It is not a claim
that every possible script, packet, database combination or machine failure has
been simulated.

No live Windows realm, client assets, credentials or database were available in
this checkout. The fixes below are source-level findings backed by deterministic
component regressions; they still require a Windows build and live smoke test.

## Findings fixed

### 1. Connection framing and authentication

- **Undersized world frames:** sizes 0 and 1 were accepted even though the frame
  already contained a two-byte opcode. This could write past the logical buffer
  and underflow its remaining-space calculation. They are rejected before resize
  or copy. Large hotfix frames retain their special bound and require completed
  authentication.
- **Premature/repeated encryption ACKs:** an ACK could publish a null session to
  World, or attempt to publish a session more than once. The socket now follows
  explicit `AwaitAuth -> Authenticating -> AwaitEncryptionAck -> Encrypted`
  phases. An ACK consumes the expected phase exactly once; ordinary gameplay
  packets are not accepted before the handshake completes.
- **Disconnect during authentication/RBAC:** the socket now owns its preliminary
  WorldSession until handing it to World. Closing the socket releases that
  preliminary session outside the session mutex; late callbacks check liveness
  and state before dereferencing it. Completed sessions remain owned by World.
- **Stalled handshakes:** world connections that do not complete authentication
  within 60 seconds are closed, rather than retaining preliminary sessions
  indefinitely. This does not replace character-selection/in-world idle timers.
- **Malformed stored authentication keys:** invalid 64-byte realm-key or
  continued-session-key lengths now reject authentication rather than invoking
  the database Field truncation/size assertion. Key material is not logged.
- **Compressed login bursts:** send-buffer capacity now includes the compressed
  packet's own two-byte opcode in addition to the compression wrapper and zlib
  bound. The old calculation could underallocate by two bytes.
- **Battle.net RPC allocation:** untrusted body lengths are capped at 1 MiB before
  allocation; zero-length headers and failed header parses reject the connection
  instead of proceeding to an assertion. Normal zero-body RPCs remain supported.

HMAC/digest validation and encryption were not bypassed or replaced.

### 2. Character selection and saved character loading

- Login completion verifies that the requested character is still being loaded,
  the session has no Player already, and a real client's sockets are still
  connected. Stale/duplicate callbacks do not allocate another Player.
- An already connected character is rejected before creating/loading a duplicate
  object. Socket-less bot sessions remain exempt from the real-client socket
  check, not from duplicate-character checking.
- Failed character loads run pre-delete cleanup before deleting a partially
  constructed Player.
- Saved player levels, inventory/bank bag counts, create mode, active talent
  group and bonus-group count are validated before index-using setters run.
- Saved talent rank/group values are read before narrowing and invalid rows are
  logged/skipped. `AddTalent` also checks its own arguments before indexing.
- Glyph rows are stored in their saved talent group, rather than overwriting the
  active group's slots. Invalid indices/IDs are skipped before narrowing.
- Invalid corpse map IDs/positions are ignored without changing the character's
  dead/alive state or reaching a map-store assertion.
- Invalid saved aura/pet-aura GUID blobs are skipped through a checked raw-GUID
  API; invalid stored group target icons are cleared in memory. The strict raw
  GUID setter remains available for internal invariants.
- Saved hunter-pet levels cannot reach level-zero stats indexing. The pet-level
  lookup itself also rejects level zero.
- Optional first-login faction lookups cannot dereference a null faction when
  `StartAllReputation` is used with incomplete data.
- Learned-spell validation is iterative and detects cycles. A corrupt/deep
  learn-spell graph reached while loading spells/talents no longer consumes the
  C++ call stack. Craft-item and reagent checks are preserved.

These checks log invalid saved values and normalize/skip them in memory. They
are not a guessed world/character database migration and do not delete accounts
or reset characters.

### 3. First player/bot updates and teardown

- Only real sessions receive an owned-bot manager. A socket-less bot that has
  finished loading but not yet received its AI is no longer mistaken for a human.
- Owned bot session/query completion now runs from the world-thread hook,
  alongside random-bot sessions—not inside a master's map-worker Player update.
  Per-bot AI still runs in the normal player update path.
- Pending bot GUID reservations cover all holders, preventing two owners from
  starting the same bot concurrently.
- Pending sessions remain owned until AI setup succeeds; failures clean up the
  session/player rather than leaving an orphaned logged-in character.
- A redirected pending login can complete its worldport before AI attachment.
- Cancelling a bot or logging out its owner also destroys pending sessions and
  their callbacks. A delayed query cannot later attach a bot to a deleted owner.
- Completed bot sessions have explicit ownership independent of the raw Player
  registry. Iteration uses GUID snapshots across callbacks, and sessions are
  reclaimed only after their packet/query callbacks return.
- Real-player discovery stores GUIDs and resolves current players on demand;
  duplicate login notices and removed players do not leave dangling roster
  pointers.
- Player controller teardown runs before inventory/quest state is destroyed.
  Direct core deletion also detaches a bot from its session and holder entries.
- Shutdown drains owned, random and pending bots while teardown hooks are still
  active, then disables the bot hooks.
- Bot commands parse immutable/null input safely instead of modifying a const
  command string with `strtok`.

## Regression checks

```bat
python -m unittest discover -s tests -p "playerbot_*_test.py" -v
```

The combined suite has **23 tests**, including six new login/lifecycle tests.
They exercise production methods/validation blocks with deterministic state:

- Fragmented and coalesced world frames, including an opcode-only frame; sizes
  0/1; allowed and excessive hotfix bounds.
- ACK before authentication, ACK before RBAC/challenge completion, one successful
  handoff, replayed ACK, realm/instance paths, timeout boundaries, disconnect and
  a late permission callback. A destructor re-entry test checks mutex ordering.
- Worst-case compression output filling its advertised bound, including packets
  near the compression threshold and larger than the normal send buffer.
- Fragmented Battle.net frames, valid zero-body frames and preallocation bounds.
- Successful, cancelled, disconnected and duplicate login completions; the
  distinction between real clients and socket-less bots.
- Valid/corrupt saved levels, bag counts, create mode, talent ranks/groups,
  inactive-group glyphs, corpse locations and raw GUID blob lengths.
- Shared/cyclic/missing/deep learned-spell graphs, including a 20,000-spell chain;
  valid/invalid crafting and reagent data.
- Two holders requesting the same bot; cancellation while loading; failed AI
  setup; pending worldport; a Player removed by a session callback; owner
  disconnect/shutdown; stale player GUIDs; immutable/null command text.

The framing tests use the real MessageBuffer implementation. Socket, crypto,
protobuf-result, database and game-state dependencies are lightweight fakes;
these tests do not constitute a real authentication, TLS or full-world run. The
compression test checks the writer against a worst-case compressor contract,
not a replacement zlib implementation.

## Windows smoke-test checklist

Use a test realm and back up the database/configuration first. Reconfigure CMake,
rebuild **bnetserver and worldserver** in x64 RelWithDebInfo, and deploy matching
executables/PDBs. Existing maps and the unified bot configuration stay in place.

1. Start cold with a small bot population; authenticate a real client and enter
   an existing character, then a newly created character.
2. Cancel/disconnect during authentication and character loading, reconnect,
   and verify that only one Player exists for the character.
3. Exercise a character with dual specs/glyphs, a hunter pet and a dead character
   with a valid corpse. Verify persistent state after reconnecting.
4. Add an owned bot and immediately remove it or log out its owner before login
   completes. Confirm no late bot login or orphaned session occurs.
5. Exercise normal bot login, teleport redirection, player logout and server
   shutdown. Check the new rejection/cleanup logs rather than resetting data.
6. Increase bot load gradually while measuring world-tick and database latency.
   Randomization still performs synchronous world work; a slow database or large
   batch can still stall the server. Do not disable the freeze detector to hide
   a stall.

## Remaining verification limits

A complete Windows/MSVC build, actual client login, live database compatibility,
script execution, TLS/crypto integration and a full race/load test have not been
performed in this workspace. This pass addresses identified failure paths; it
cannot establish that *everything* a user might do is crash-free. Keep updated
logs and matching crash symbols for remaining failures. The packet-forwarding
and client-specific limitations in [PacketCompatibility.md](PacketCompatibility.md)
also remain outside the verified end-to-end coverage.
