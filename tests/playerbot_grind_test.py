"""Guard the autonomous-grind behavior and the party-chat command path.

Grinding bots are meant to roam on their own ("move random" movement) and kill
everything they see unless too strong, instead of standing where they are and
looking stuck:

- "move random" is registered in MovementStrategyContext, which is
  sibling-exclusive: "+move random" replaces follow/stay/guard/etc. instead of
  stacking a second movement on top of them.
- The "grind" chat shortcut switches the bot to "+grind,+move random".
- The grind strategy leashes roaming bots back with "follow" when the
  "out of react range" trigger fires, so bots stay in the master's area.
- The trigger itself must be registered in TriggerContext (it was missing, so
  the leash - and follow mode's "Wait for me!" - never fired).

Also guards the party-chat command path ("summon" typed in party chat must
reach the bots): the core ChatHandler forwards party/raid chat to
Playerbot::OnPlayerChat, which routes it to the sender's PlayerbotMgr.

Run: python -m unittest discover -s tests -p "playerbot_*_test.py" -v
"""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
STRATEGY_CTX = ROOT / "src" / "plugins" / "playerbot" / "strategy" / "StrategyContext.h"
TRIGGER_CTX = ROOT / "src" / "plugins" / "playerbot" / "strategy" / "triggers" / "TriggerContext.h"
GRIND_SHORTCUT = ROOT / "src" / "plugins" / "playerbot" / "strategy" / "actions" / "ChatShortcutActions.cpp"
GRIND_STRATEGY = ROOT / "src" / "plugins" / "playerbot" / "strategy" / "generic" / "GrindingStrategy.cpp"
GRIND_TARGET = ROOT / "src" / "plugins" / "playerbot" / "strategy" / "values" / "GrindTargetValue.cpp"
CHAT_HANDLER = ROOT / "src" / "server" / "game" / "Handlers" / "ChatHandler.cpp"


class MoveRandomExclusivityTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = STRATEGY_CTX.read_text(encoding="utf-8", errors="replace")

    def block_between(self, start_marker, end_marker):
        start = self.text.index(start_marker)
        end = self.text.index(end_marker, start)
        return self.text[start:end]

    def test_move_random_is_movement_exclusive(self):
        """'move random' must live in MovementStrategyContext (exclusive)."""
        movement = self.block_between("class MovementStrategyContext",
                                      "class AssistStrategyContext")
        self.assertIn('creators["move random"]', movement,
                      "'move random' is not registered in MovementStrategyContext")

    def test_move_random_not_in_main_context(self):
        """'move random' must not also be in the non-exclusive main context."""
        main = self.block_between("class StrategyContext",
                                  "class MovementStrategyContext")
        self.assertNotIn('creators["move random"]', main,
                         "'move random' is registered in the non-exclusive "
                         "StrategyContext and would stack on other movement")


class GrindShortcutTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.text = GRIND_SHORTCUT.read_text(encoding="utf-8", errors="replace")

    def test_grind_shortcut_roams(self):
        """The 'grind' shortcut must switch the bot to roaming movement."""
        start = self.text.index("GrindChatShortcutAction::Execute")
        body = self.text[start:start + 2000]
        self.assertIn("+grind,+move random", body,
                      "grind shortcut does not enable roaming movement")


class GrindLeashTest(unittest.TestCase):
    def test_grind_strategy_has_leash(self):
        text = GRIND_STRATEGY.read_text(encoding="utf-8", errors="replace")
        self.assertIn("out of react range", text,
                      "grind strategy has no out-of-react-range leash")
        self.assertIn('"follow"', text,
                      "grind leash does not walk back with follow")

    def test_out_of_react_range_trigger_registered(self):
        text = TRIGGER_CTX.read_text(encoding="utf-8", errors="replace")
        self.assertIn('creators["out of react range"]', text,
                      "'out of react range' trigger is not registered, "
                      "so the grind leash never fires")
        self.assertIn("OutOfReactRangeTrigger", text,
                      "OutOfReactRangeTrigger is not referenced by TriggerContext")


class GrindTargetFilterTest(unittest.TestCase):
    def test_no_master_distance_gate(self):
        """Grind targets must not be gated on distance to the master."""
        text = GRIND_TARGET.read_text(encoding="utf-8", errors="replace")
        start = text.index("GrindTargetValue::FindTargetForGrinding")
        body = text[start:]
        self.assertNotIn("master->GetDistance", body,
                         "grind target selection still ignores mobs far from "
                         "the master instead of killing everything the bot sees")

    def test_too_strong_filter_kept(self):
        """The level/elite 'too strong' filters must stay in place."""
        text = GRIND_TARGET.read_text(encoding="utf-8", errors="replace")
        self.assertIn("GetLevel() - (int)bot->GetLevel() > 4", text,
                      "over-level filter missing from grind targeting")
        self.assertIn("Classification > CreatureClassifications::Normal", text,
                      "elite filter missing from grind targeting")


class PartyChatCommandPathTest(unittest.TestCase):
    def assert_case_forwards_to_bots(self, label):
        text = CHAT_HANDLER.read_text(encoding="utf-8", errors="replace")
        found = False
        start = 0
        while True:
            idx = text.find(label, start)
            if idx == -1:
                break
            if "Playerbot::OnPlayerChat" in text[idx:idx + 2000]:
                found = True
                break
            start = idx + 1
        self.assertTrue(found, "%s is no longer forwarded to "
                               "Playerbot::OnPlayerChat" % label)

    def test_party_chat_reaches_bots(self):
        self.assert_case_forwards_to_bots("case CHAT_MSG_PARTY:")

    def test_raid_chat_reaches_bots(self):
        self.assert_case_forwards_to_bots("case CHAT_MSG_RAID:")


if __name__ == "__main__":
    unittest.main()
