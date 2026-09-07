"""Check the unified bot config without needing a database or server build.

Run: python tests/playerbot_config_test.py
Check a build/install copy: python tests/playerbot_config_test.py --config <path>
"""

import argparse
import ast
import configparser
from pathlib import Path
import re
import unittest


ROOT = Path(__file__).resolve().parents[1]
TEMPLATE = ROOT / "src/server/worldserver/worldserver.conf.dist"
CONFIG = TEMPLATE
SPEC_PREFIX = "AiPlayerbot.RandomClassSpecProbability."
WRATH_CLASSES = (*range(1, 10), 11)
GETTER = re.compile(
    r'Get(Bool|Int64|Int|Float|String)Default\("((?:AiPlayerbot|AhBot)\.[^"]+)",'
    r'\s*("[^"]*"|[^,)]+)'
)


def numeric_default(expression):
    """Evaluate only the numeric constants/products used by the config reader."""
    node = ast.parse(expression.strip().removesuffix("f"), mode="eval").body

    def value(n):
        if isinstance(n, ast.Constant) and type(n.value) in (int, float):
            return n.value
        if isinstance(n, ast.BinOp) and isinstance(n.op, ast.Mult):
            return value(n.left) * value(n.right)
        raise ValueError(f"Unsupported numeric default: {expression}")

    return value(node)


class PlayerbotConfigTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        # Preserve case and reject duplicate keys. As with Boost's INI reader,
        # comments after values are NOT stripped (e.g. '80 # comment' is invalid).
        cls.parser = configparser.ConfigParser(interpolation=None, strict=True)
        cls.parser.optionxform = str
        cls.parser.read_string(CONFIG.read_text(encoding="utf-8"))
        cls.settings = cls.parser["worldserver"]
        cls.reads = []
        for source in (ROOT / "src/plugins").rglob("*.cpp"):
            cls.reads.extend(GETTER.findall(source.read_text(encoding="utf-8")))

    def test_only_worldserver_ini_section(self):
        self.assertEqual(self.parser.sections(), ["worldserver"])
        self.assertNotIn("ConfVersion", self.settings)

    def test_all_read_settings_are_present_and_active(self):
        self.assertGreater(len(self.reads), 70, "Config-reader scan unexpectedly found no settings")
        required = {key for _, key, _ in self.reads}
        required.update(f"{SPEC_PREFIX}{cls}.{spec}" for cls in WRATH_CLASSES for spec in range(3))
        actual = {key for key in self.settings if key.startswith(("AiPlayerbot.", "AhBot."))}
        self.assertEqual(required, actual)

    def test_scalar_defaults_match_code_and_have_valid_types(self):
        for kind, key, default in self.reads:
            with self.subTest(key=key):
                actual = self.settings[key]
                if kind == "String":
                    # ConfigMgr::GetStringDefault removes the enclosing quotes.
                    self.assertEqual(actual.strip('"'), ast.literal_eval(default))
                elif kind == "Bool":
                    self.assertIn(actual, ("0", "1"))
                    self.assertEqual(actual == "1", default.strip() == "true")
                elif kind in ("Int", "Int64"):
                    self.assertRegex(actual, r"^-?\d+$")
                    self.assertEqual(int(actual), numeric_default(default))
                else:
                    self.assertAlmostEqual(float(actual), numeric_default(default))

    def test_existing_talent_weights_are_preserved(self):
        weights = {
            1: (20, 30, 50), 2: (20, 50, 30), 3: (25, 50, 25),
            4: (40, 50, 10), 5: (40, 40, 20), 6: (33, 33, 33),
            7: (10, 45, 45), 8: (20, 10, 70), 9: (33, 33, 33),
            11: (10, 45, 45),
        }
        for cls, expected in weights.items():
            with self.subTest(cls=cls):
                self.assertEqual(tuple(int(self.settings[f"{SPEC_PREFIX}{cls}.{s}"]) for s in range(3)), expected)

    def test_no_standalone_bot_template_or_late_override(self):
        self.assertFalse((ROOT / "src/plugins/playerbot/aiplayerbot.conf.dist").exists())
        reader = (ROOT / "src/plugins/playerbot/PlayerbotAIConfig.cpp").read_text(encoding="utf-8")
        self.assertNotIn("LoadAdditionalFile(", reader)
        for name in ("src/plugins/CMakeLists.txt", ".github/workflows/release.yml"):
            self.assertNotIn("aiplayerbot.conf.dist", (ROOT / name).read_text(encoding="utf-8"))

    def test_build_or_install_copy_is_complete(self):
        self.assertEqual(CONFIG.read_text(encoding="utf-8"), TEMPLATE.read_text(encoding="utf-8"))


if __name__ == "__main__":
    arguments = argparse.ArgumentParser(add_help=False)
    arguments.add_argument("--config", type=Path, default=TEMPLATE)
    options, remaining = arguments.parse_known_args()
    CONFIG = options.config
    unittest.main(argv=[__file__, *remaining])
