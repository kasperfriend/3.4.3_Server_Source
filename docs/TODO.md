# TODO — Server.log follow-ups (deferred from PR #15)

These items need live-server data (log excerpts / DB contents) before they can
be fixed at the root cause. Nothing here blocks the PR #15 merge.

## How to collect the needed data

In PowerShell, from the server folder:

```powershell
Select-String -Path Server.log -Pattern "did not match dbc|does not exist|Missing name|ArenaSeason|NOT implemented|spell.*valid" | Select-Object -ExpandProperty Line | Set-Content log-excerpts.txt
```

Plus, against the **world** database:

```sql
SELECT * FROM game_event_arena_seasons;
```

## Items

1. **Spell-script validation failures (~40 lines).** Mismatched scripts never
   attach, so the affected spells run without their custom logic. Seen in the
   2026-08-12 log: Wild Growth (4 ranks), Prayer of Mending (2), Leader of the
   Pack, Blood Tap, Bloodworms (3), Freezing Circle, Skyguard Flare, plus
   store-mount/invincible scripts. Needs the exact `did not match dbc` lines
   (spell ID, effect index, hook name) to fix each script's effect binding.
2. **Spell corrections for non-existing spells.** Rows in the world DB
   (`spell_dbc` overrides or similar) referencing spells absent from the DBCs.
   Needs the exact spell IDs for cleanup SQL — or, if there are many, suspect
   a DBC/client-version mismatch instead.
3. **Stale `.conf` keys (`Missing name …`).** Needs the exact key names: if the
   `.dist` files lack them, patch the `.dist`; if only the live `.conf` is
   stale, append the missing block from the `.dist`.
4. **ArenaSeason (32) invalid.** `GameEventMgr::StartArenaSeason` found no row
   for season 32 in `game_event_arena_seasons`, so no season game-event
   started (client display of the season ID is unaffected). Needs the table
   contents to recommend the correct `Arena.ArenaSeason.ID`.

## Deliberately not fixed (benign by design)

- `ResourcesService.GetContentHandle` — the server already answers with
  `ERROR_RPC_NOT_IMPLEMENTED`; the client tolerates it. A real implementation
  needs a content backend that does not exist.
- MMAP `Could not load` lines — already `TC_LOG_DEBUG`; ocean tiles
  legitimately have no navmesh.
- `Automatic database updates are disabled` — operator config choice. If the
  DB is also old, enabling updates may resolve data-driven items above.
