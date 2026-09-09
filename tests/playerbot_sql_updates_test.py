"""Guard sql/updates against full-table wipes of account/character user data.

Regression test for the incident where dev test-seed dumps lived under
sql/updates/ (sql/updates/auth/3.4.3/2026_08_10_01_auth.sql and
sql/updates/characters/3.4.3/2026_08_10_00_characters.sql). Both start with
unconditional `DELETE FROM account` / `DELETE FROM characters` / ... and then
insert throwaway test rows. Setup-Database.ps1 imports every *.sql under
sql/updates/, and the server's automatic updater applies pending files there,
so applying updates deleted every existing account and character: old logins
failed while newly created accounts worked.

The seed dumps now live under sql/custom/dev-seed/ (never auto-applied).
This test fails if any file under sql/updates/ contains an unconditional
(full-table, no WHERE) DELETE or TRUNCATE of a user-data table, or if the
known dev-seed files reappear under sql/updates/.

Run: python -m unittest discover -s tests -p "playerbot_*_test.py" -v
"""

import re
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UPDATES = ROOT / "sql" / "updates"

# Tables holding irreplaceable user data. A *conditional* DELETE (with WHERE)
# on these is fine and is how legitimate data fixes look; an unconditional one
# wipes the table and must never ship as an auto-applied update.
USER_DATA_TABLES = (
    "account",
    "account_access",
    "account_data",
    "account_last_played_character",
    "battlenet_accounts",
    "character_account_data",
    "character_achievement",
    "character_action",
    "character_inventory",
    "character_queststatus",
    "character_skills",
    "character_spell",
    "character_talent",
    "characters",
    "group_member",
    "groups",
    "guild",
    "guild_member",
    "item_instance",
)

# DELETE FROM <table> ; with nothing but whitespace/quotes between the table
# name and the semicolon, i.e. no WHERE clause. Matches backticked, quoted and
# bare table names, across line breaks.
_UNCONDITIONAL_DELETE = re.compile(
    r"DELETE\s+FROM\s*[`\"']?(?P<table>[A-Za-z0-9_]+)[`\"']?\s*;",
    re.IGNORECASE,
)

_TRUNCATE = re.compile(
    r"TRUNCATE\s+(?:TABLE\s+)?[`\"']?(?P<table>[A-Za-z0-9_]+)[`\"']?",
    re.IGNORECASE,
)

# Dev-seed dumps that must never live under sql/updates/ again (matched by
# distinctive content, so a rename does not slip through either).
_SEED_MARKERS = (
    "DEV-ONLY SEED DUMP",
    "auth_dev_seed",
    "characters_dev_seed",
)


def _strip_sql_comments(text):
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.DOTALL)
    text = re.sub(r"(?m)^\s*--(?:[ \t].*)?$", " ", text)
    text = re.sub(r"(?m)^\s*#[^\n]*$", " ", text)
    return text


class SqlUpdatesSafetyTest(unittest.TestCase):
    def _update_files(self):
        self.assertTrue(UPDATES.is_dir(), "sql/updates directory is missing")
        files = sorted(UPDATES.rglob("*.sql"))
        self.assertTrue(files, "no sql/updates files found to check")
        return files

    def test_no_unconditional_user_data_wipes(self):
        offenders = []
        for path in self._update_files():
            body = _strip_sql_comments(path.read_text(encoding="utf-8"))
            hits = set()
            for match in _UNCONDITIONAL_DELETE.finditer(body):
                if match.group("table").lower() in USER_DATA_TABLES:
                    hits.add("DELETE FROM %s" % match.group("table"))
            for match in _TRUNCATE.finditer(body):
                if match.group("table").lower() in USER_DATA_TABLES:
                    hits.add("TRUNCATE %s" % match.group("table"))
            if hits:
                offenders.append(
                    "%s: unconditional wipe of user data (%s). "
                    "Auto-applied updates must use conditional DELETEs; "
                    "dev seed dumps belong in sql/custom/dev-seed/."
                    % (path.relative_to(ROOT).as_posix(), ", ".join(sorted(hits)))
                )
        self.assertEqual([], offenders, "\n".join(offenders))

    def test_dev_seed_not_under_updates(self):
        offenders = []
        for path in self._update_files():
            text = path.read_text(encoding="utf-8")
            for marker in _SEED_MARKERS:
                if marker in text:
                    offenders.append(
                        "%s: contains dev-seed marker %r; seed dumps must live "
                        "in sql/custom/dev-seed/, never under sql/updates/."
                        % (path.relative_to(ROOT).as_posix(), marker)
                    )
        self.assertEqual([], offenders, "\n".join(offenders))

    def test_conditional_deletes_still_allowed(self):
        # Sanity check that the regex does not flag legitimate conditional
        # DELETEs of the kind real data-fix updates use.
        sample = (
            "DELETE FROM `lfg_dungeon_template` WHERE `dungeonId` IN (1, 2);\n"
            "DELETE FROM `creature_template_difficulty` WHERE `DifficultyID`=0 AND `Entry` IN (1);"
        )
        self.assertEqual([], _UNCONDITIONAL_DELETE.findall(sample))


if __name__ == "__main__":
    unittest.main()
