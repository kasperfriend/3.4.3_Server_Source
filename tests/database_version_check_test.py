"""Guard the database server minimum-version check against MariaDB misparding.

MariaDB prefixes its server version string with "5.5.5-" for compatibility
with old MySQL clients ("5.5.5-10.11.19-MariaDB"). mysql_get_server_version()
parses only the leading "5.5.5" and reports 50505, so a build linked against
the MySQL client library rejected a perfectly fine MariaDB 10.11 server with:

    TrinityCore does not support MySQL versions below 5.7
    (found id 50505, need id >= 50700)

The check must therefore detect the server flavor from the version string at
runtime, skip the "5.5.5-" compatibility prefix when parsing the numeric id,
and compare MariaDB servers against the MariaDB minimum (10.2.9) instead of
the MySQL one (5.7).

Run: python -m unittest discover -s tests -p "*_test.py" -v
"""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
WORKER_POOL = ROOT / "src" / "server" / "database" / "Database" / "DatabaseWorkerPool.cpp"
CONNECTION_H = ROOT / "src" / "server" / "database" / "Database" / "MySQLConnection.h"
CONNECTION_CPP = ROOT / "src" / "server" / "database" / "Database" / "MySQLConnection.cpp"


class ServerVersionCheckTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = WORKER_POOL.read_text(encoding="utf-8", errors="replace")

    def test_flavor_detected_from_version_string(self):
        """The check must detect MariaDB from the server version string."""
        self.assertIn("GetServerInfo()", self.text,
                      "version check does not read the server version string")
        self.assertIn("MariaDB", self.text,
                      "version check does not detect MariaDB servers")

    def test_compat_prefix_skipped(self):
        """The parser must skip MariaDB's '5.5.5-' compatibility prefix."""
        self.assertIn("5.5.5-", self.text,
                      "version parser does not handle MariaDB's '5.5.5-' prefix")

    def test_mariadb_compared_against_mariadb_minimum(self):
        """MariaDB servers must be checked against MIN_MARIADB_SERVER_VERSION."""
        self.assertIn("MIN_MARIADB_SERVER_VERSION", self.text,
                      "MariaDB minimum version is not used by the check")

    def test_check_not_based_on_parsed_client_number(self):
        """OpenConnections must not gate on mysql_get_server_version's number."""
        start = self.text.index("DatabaseWorkerPool<T>::OpenConnections")
        end = self.text.index("DatabaseWorkerPool<T>::EscapeString", start)
        body = self.text[start:end]
        self.assertNotIn("GetServerVersion()", body,
                         "version check still uses the misparsed numeric server version")

    def test_server_info_accessor_exists(self):
        """MySQLConnection must expose the raw server version string."""
        header = CONNECTION_H.read_text(encoding="utf-8", errors="replace")
        impl = CONNECTION_CPP.read_text(encoding="utf-8", errors="replace")
        self.assertIn("GetServerInfo", header,
                      "MySQLConnection::GetServerInfo is not declared")
        self.assertIn("mysql_get_server_info", impl,
                      "MySQLConnection::GetServerInfo is not implemented")

    def test_misparsing_accessor_removed(self):
        """The numeric GetServerVersion accessor must stay removed.

        It wraps mysql_get_server_version(), which misparses MariaDB's
        "5.5.5-..." compatibility prefix as 50505 - any future caller would
        reintroduce the false rejection. Version checks must go through
        GetServerInfo() + ParseServerVersionId() instead.
        """
        header = CONNECTION_H.read_text(encoding="utf-8", errors="replace")
        impl = CONNECTION_CPP.read_text(encoding="utf-8", errors="replace")
        self.assertNotIn("GetServerVersion", header,
                         "misparsing GetServerVersion accessor reintroduced")
        self.assertNotIn("GetServerVersion", impl,
                         "misparsing GetServerVersion accessor reintroduced")


if __name__ == "__main__":
    unittest.main()
