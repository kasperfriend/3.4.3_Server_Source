# Playerbot packet/byte-level audit

Date: 2026-09-07

This follow-up checks the byte buffers and packet paths used by playerbots
against **this checkout's** packet writers/readers. It does not assume the old
3.3.5 layout or blindly copy a current retail layout. In particular, this tree
uses two GUID masks for packed 128-bit GUIDs, a four-byte `SpellCastVisual`, and
an eight-byte `RideTicket` timestamp.

## Fixes in this pass

| Area | Defect | Fix |
| --- | --- | --- |
| Teleport acknowledgement | `MoveTeleportAck` was constructed from a default packet with `UNKNOWN_OPCODE`, violating `ClientPacket`'s opcode assertion. | Construct with `CMSG_MOVE_TELEPORT_ACK`; retain the core's typed acknowledgement handler. |
| Quest share | A `CMSG_PUSH_QUEST_TO_PARTY` event was constructed as `QuestPushResult`, which expects a different opcode and GUID payload. | Check the event opcode, rewind it, use `PushQuestToParty`, and match the pending quest/sharer before changing state. |
| Spell interruption | The bot fabricated an additional pair of obsolete spell-failure packets after the core had already emitted the correct ones. It also used the spell pointer after cancellation and reset saved spell/time fields before calculating delay. | Let the core serialize interruption messages, capture the spell ID before cancellation, and calculate delay from saved state before reset. |
| Movement flags | A CAN_FLY notification overwrote the complete movement-flag word and forced FLYING. | Leave the authoritative flags set by `Unit::SetCanFly` intact. |
| Packed GUIDs | Deserialization ORed new bytes into reused destinations; zero masks retained old bits. The 64-bit packed writer also recorded its mask position before pending bits were flushed. | Decode into zeroed temporaries and commit only after a complete read; flush before reserving the mask byte. Remove the unused, incorrect legacy GUID decoder. |
| Bit/byte buffers | Full-width writes shifted by 64, arithmetic in range checks could wrap, and zero-byte reads indexed empty storage. | Check bit widths, handle 64-bit writes explicitly, use overflow-safe subtraction/division for bounds, and avoid indexing for zero-byte copies. |
| Variable arrays | Loot request counts were used for resize before remaining-byte validation; reused item bonus lists retained old data; bounded `Array::emplace_back` omitted its capacity check. | Validate before allocation, clear/reset reused optional data, and enforce the same capacity rule as push/resize. |
| Packet/event delivery | A LIFO queue plus a single stored trigger packet reordered and discarded messages. A failing dispatch could keep the same packet at the head forever. | Mutex-protected bounded FIFO delivery; dequeue before dispatch; queue events per trigger and consume only an event actually checked. Normalize inherited byte/bit cursors. Initialize default event owners. |
| Ready checks | Client opcodes were registered on an outgoing server-packet path; a bot tried to start another check instead of replying. The completion action key was also mismatched. | Route STARTED/COMPLETED server events, validate the group, send a filled typed ready/not-ready response, and stop on completion without starting a new check. Use the mana threshold, not the health threshold. |
| Vendor failures | Buy-result enum values were used as opcodes. | Handle `SMSG_BUY_FAILED` and decode its vendor, item and reason. |
| Loot | Money/items and guild/discount credit were applied before the whole response was parsed or inventory storage succeeded. Currency tails were ignored. Stale requests could target a newer open loot object. | Decode the entire response first, reject stale owners, use the loot-object GUID and one-based list IDs, validate the current loot identity in the core, and grant item credit on confirmed item-push notifications. |
| Trade | Filled `AcceptTrade` packets left the required partner state index at zero. Price multiplication could overflow, and one grey item zeroed the whole basket. | Include the inspected partner's current index, preserve core revalidation, use wider arithmetic/bounded item-price totals, and ignore only the grey item's value. Money formatting accepts the wider balance. |

Shared server-packet decoders and loot request builders live in
`src/plugins/playerbot/PlayerbotPackets.{h,cpp}`. They parse into temporary results
and reject truncated/trailing data before exposing a result to an action.
Embedded GUIDs, item instances, visuals and ride tickets use the core codecs.

Malformed byte-buffer exceptions are caught at the action boundary, so one bad
action packet does not abandon normal action cleanup or block subsequent
packets. This is not a blanket catch intended to conceal unrelated C++ defects.

## Byte-level verification

```bat
python -m unittest discover -s tests -p "playerbot_*_test.py" -v
```

This packet pass brought the suite to 17 tests. The later
[fresh-start/login audit](FreshLoginSafety.md) brings the combined suite to
**23 tests**. The packet tests exercise:

- All **65,536 combinations** of low/high packed-GUID masks, including reuse of
  an all-ones destination and every truncated prefix of a nontrivial GUID.
- Exact golden bytes for a sparse 128-bit GUID, a packed integer after bit data,
  and an item instance with random properties, a modifier and bonus IDs.
- Bit writes of widths **0..64** at all **eight starting offsets**, compared
  against an independent expected-bit sequence; invalid widths are rejected.
- Offset/count-overflow attempts, empty zero-byte reads, bounded array capacity,
  and oversized loot request counts.
- The production writers and decoders for cast failures, both spell-failure
  variants, item push results, LFG proposals, ready checks, and loot responses.
  Tests reject **every byte-truncated prefix** and unexpected trailing bytes.
- Loot request builders decoded by the actual core client-packet readers.
- Actual loot-action code doing no work for a truncated response and not acting
  on a stale loot window; actual ready-action code answering true/false without
  emitting a new-check request.
- FIFO order, failed-dispatch recovery, trigger consumption, bounded concurrent
  enqueue, typed teleport ACK construction, interruption delay/cancellation,
  vendor error reporting and trade-cost overflow/grey-item behavior.

These tests compile the real `ByteBuffer.cpp`, packet base/loot codec code and
bot decoder code. Codec functions/types from larger game-dependent translation
units are compiled verbatim with lightweight surrounding game-state fakes.
Logging/assert sinks are shimmed; the byte/bit/GUID codecs are not reimplemented.
GCC uses checked STL iterators and undefined-behavior checks. The packet tests
exclude alignment instrumentation for the core's explicitly packed GUID ABI.

## Apply and validate on Windows

Regenerate CMake and rebuild x64 **RelWithDebInfo**, then deploy the executable
with its matching PDB. Plugin source discovery now notices added codec files.
Keep the unified bot configuration and the existing map files in place.
No database reset or map re-extraction is part of this packet update.

On a small test population, exercise near/far teleport, failed/interrupted casts,
ready/not-ready checks, vendor failures, loot with full bags, modified item bonus
lists, and trades after either party changes an item. Keep new logs and matching
symbols for any remaining failure.

## Limits / follow-up areas

- No complete Windows/MSVC worldserver build, packet capture from the user's
  client, or live-database run was available here. Passing writer/reader tests
  demonstrates consistency for the covered packet families, not every opcode.
- Filled typed bot responses deliberately avoid guessing client wire layouts.
  The custom real-client `ReadyCheckResponseClient::Read` implementation was
  not changed; its optional-index handling needs verification against a client
  capture, particularly for non-default groups.
- The core hook registry currently exposes bot packet delivery, while the
  master-packet forwarding methods require separate end-to-end wiring review.
  Making the quest-share parser safe does not by itself prove that every
  master-snooped quest/taxi feature is operational.
- Queues are bounded and report overflow; an overloaded bot can lose its oldest
  event rather than grow memory indefinitely. These checks do not establish
  full core/game-object thread safety.
- Currency entries in loot responses are parsed and validated, but this pass
  does not invent currency-looting behavior for the core's item-loot handler.
  It also does not add gold repeatedly to per-item discount credit.
