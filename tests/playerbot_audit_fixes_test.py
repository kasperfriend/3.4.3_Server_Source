"""Tests for the systematic playerbot optimization & bugfix audit."""

import unittest
from pathlib import Path
from playerbot_logic_test import ROOT, run_cpp, source_slice


def function(path, signature):
    text = (ROOT / path).read_text(encoding="utf-8")
    start = text.index(signature)
    brace = text.index("{", start)
    depth = 1
    end = brace + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}")
        end += 1
    return text[start:end]


class PlayerbotAuditFixesTest(unittest.TestCase):
    def test_bank_and_guild_bank_actions(self):
        bank_exec = function("src/plugins/playerbot/strategy/actions/BankAction.cpp", "bool BankAction::Execute(string text, Unit* bank)")
        self.assertIn("result = true;", bank_exec)
        self.assertNotIn("bool result = false;\n    if (text[0] == '-')\n    {\n        ItemIds found = chat->parseItems(text);\n        for (ItemIds::iterator i = found.begin(); i != found.end(); i++)\n        {\n            uint32 itemId = *i;\n            result &= Withdraw(itemId);", bank_exec)

        bank_list = function("src/plugins/playerbot/strategy/actions/BankAction.cpp", "void BankAction::ListItems()")
        self.assertIn("BANK_SLOT_ITEM_START", bank_list)
        self.assertIn("BANK_SLOT_BAG_START", bank_list)

        guild_bank_move = function("src/plugins/playerbot/strategy/actions/GuildBankAction.cpp", "bool GuildBankAction::MoveFromCharToBank(Item* item, GameObject* bank)")
        self.assertIn("SwapItemsWithInventory", guild_bank_move)
        self.assertNotIn("SwapItems(bot, 0, playerSlot, 0, INVENTORY_SLOT_BAG_0, 0)", guild_bank_move)

    def test_reward_action_single_choice_fix(self):
        reward_fn = function("src/plugins/playerbot/strategy/actions/RewardAction.cpp", "bool RewardAction::Reward(uint32 itemId, Object* questGiver)")
        self.assertIn("pQuest->GetRewChoiceItemsCount() > 0", reward_fn)
        self.assertNotIn("pQuest->GetRewChoiceItemsCount() > 1", reward_fn)

    def test_stay_action_usefulness(self):
        stay_fn = function("src/plugins/playerbot/strategy/actions/StayActions.cpp", "bool StayAction::isUseful()")
        self.assertIn('return AI_VALUE2(bool, "moving", "self target");', stay_fn)
        self.assertNotIn('return !AI_VALUE2(bool, "moving", "self target");', stay_fn)

    def test_party_healing_pet_target_fix(self):
        heal_fn = function("src/plugins/playerbot/strategy/values/PartyMemberToHeal.cpp", "Unit* PartyMemberToHeal::Calculate()")
        self.assertIn("calc.probe(health, pet)", heal_fn)
        self.assertIn("!IsTargetOfSpellCast(pet, predicate)", heal_fn)

    def test_attackers_value_fixes(self):
        add_attackers = function("src/plugins/playerbot/strategy/values/AttackersValue.cpp", "void AttackersValue::AddAttackersOf(Group* group, set<Unit*>& targets)")
        self.assertIn("if (member->IsBeingTeleported())\n            continue;", add_attackers)

        has_threat = function("src/plugins/playerbot/strategy/values/AttackersValue.cpp", "bool AttackersValue::hasRealThreat(Unit *attacker)")
        self.assertNotIn("UNIT_STATE_ROOT", has_threat)

    def test_stats_and_threat_values(self):
        has_mana = function("src/plugins/playerbot/strategy/values/StatsValues.cpp", "bool HasManaValue::Calculate()")
        self.assertIn("GetMaxPower(POWER_MANA) > 0", has_mana)

        bag_space = function("src/plugins/playerbot/strategy/values/StatsValues.cpp", "uint8 BagSpaceValue::Calculate()")
        self.assertIn("totalused = total >= totalfree ? total - totalfree : 0;", bag_space)

        threat_calc = function("src/plugins/playerbot/strategy/values/ThreatValues.cpp", "uint8 ThreatValue::Calculate(Unit* target)")
        self.assertIn("percent >= 255.0f ? 255 : (uint8)percent", threat_calc)

    def test_shaman_context_and_actions(self):
        shaman_ctx = (ROOT / "src/plugins/playerbot/strategy/shaman/ShamanAiObjectContext.cpp").read_text(encoding="utf-8")
        self.assertIn('creators["chain heal on party"] = &AiObjectContextInternal::chain_heal_on_party;', shaman_ctx)
        self.assertIn('static Action* chain_heal_on_party(PlayerbotAI* ai) { return new CastChainHealOnPartyAction(ai); }', shaman_ctx)

        shaman_actions = (ROOT / "src/plugins/playerbot/strategy/shaman/ShamanActions.h").read_text(encoding="utf-8")
        self.assertIn("class CastChainHealOnPartyAction", shaman_actions)

    def test_queue_memory_safety(self):
        queue_cpp = (ROOT / "src/plugins/playerbot/strategy/Queue.cpp").read_text(encoding="utf-8")
        self.assertIn("delete action->getAction();\n                delete action;", queue_cpp)
        self.assertIn("for (int i=0; actions[i]; i++)", queue_cpp)
        self.assertNotIn("sizeof(actions)/sizeof(ActionBasket*)", queue_cpp)
        self.assertIn("Queue::~Queue(void)", queue_cpp)

    def test_warrior_bloodrage_trigger(self):
        warrior_triggers = (ROOT / "src/plugins/playerbot/strategy/warrior/WarriorTriggers.h").read_text(encoding="utf-8")
        self.assertIn("class BloodrageBuffTrigger : public BuffTrigger", warrior_triggers)
        self.assertNotIn("class BloodrageDebuffTrigger : public DebuffTrigger", warrior_triggers)

    def test_paladin_lay_on_hands_alternative(self):
        paladin_factory = (ROOT / "src/plugins/playerbot/strategy/paladin/GenericPaladinStrategyActionNodeFactory.h").read_text(encoding="utf-8")
        self.assertIn('new NextAction("flash of light on party")', paladin_factory)

    def test_druid_caster_dedup(self):
        druid_strategy = (ROOT / "src/plugins/playerbot/strategy/druid/CasterDruidStrategy.cpp").read_text(encoding="utf-8")
        moonfire_count = druid_strategy.count('"moonfire"')
        self.assertEqual(moonfire_count, 4) # 2 in action factory + 2 in single TriggerNode in InitTriggers

    def test_death_knight_roster_and_chat(self):
        mgr = (ROOT / "src/plugins/playerbot/PlayerbotMgr.cpp").read_text(encoding="utf-8")
        self.assertIn('classNames[CLASS_DEATH_KNIGHT] = "Death Knight";', mgr)

        chat_helper = (ROOT / "src/plugins/playerbot/ChatHelper.cpp").read_text(encoding="utf-8")
        self.assertIn('classes[CLASS_DEATH_KNIGHT] = "death knight";', chat_helper)
        self.assertIn('specs[CLASS_DEATH_KNIGHT][0] = "blood";', chat_helper)
        self.assertIn('specs[CLASS_ROGUE][0] = "assassination";', chat_helper)

    def test_random_bot_mgr_unified_free_bots(self):
        mgr_h = (ROOT / "src/plugins/playerbot/RandomPlayerbotMgr.h").read_text(encoding="utf-8")
        self.assertIn("void GetAllFreeBots(vector<uint32>& freeAllianceBots, vector<uint32>& freeHordeBots);", mgr_h)


if __name__ == "__main__":
    unittest.main()
