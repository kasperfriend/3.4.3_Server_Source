# Dev-only database seed dumps — DO NOT AUTO-APPLY

> **DANGER: these files DELETE production data.** Each file starts by wiping
> whole tables (`DELETE FROM account`, `DELETE FROM characters`, …) and then
> inserts throwaway developer test rows (test accounts, test characters with
> personal UI settings, etc.).

They used to live under `sql/updates/`, where both `Setup-Database.ps1`
(`Apply-Updates` imports every `*.sql` under `sql/updates/`) and the server's
automatic DB updater would apply them — **deleting every existing account and
character**. Existing logins then failed ("unknown account") while newly
created accounts worked, and all characters were gone. That placement was the
bug; these files must **never** be moved back under `sql/updates/`.

Rules:

- Apply them only by hand, only to a **disposable dev database**, never to a
  realm with real players:
  ```bat
  mysql -u root -p auth < sql\custom\dev-seed\auth_dev_seed.sql
  mysql -u root -p characters < sql\custom\dev-seed\characters_dev_seed.sql
  ```
- `tests/playerbot_sql_updates_test.py` fails CI if any `sql/updates/**/*.sql`
  file contains a full-table wipe of account/character user data, so this
  cannot silently regress.
- If a production database already had the old update files applied, the
  deleted rows cannot be recovered from SQL — restore `auth` and `characters`
  from backup, then create any accounts/characters made since the backup.
