#pragma once

#include "strategy/values/NearestGameObjects.h"
#include "strategy/values/LogLevelValue.h"
#include "strategy/values/NearestNpcsValue.h"
#include "strategy/values/PossibleTargetsValue.h"
#include "strategy/values/NearestAdsValue.h"
#include "strategy/values/NearestCorpsesValue.h"
#include "strategy/values/PartyMemberWithoutAuraValue.h"
#include "strategy/values/PartyMemberToHeal.h"
#include "strategy/values/PartyMemberToResurrect.h"
#include "strategy/values/CurrentTargetValue.h"
#include "strategy/values/SelfTargetValue.h"
#include "strategy/values/MasterTargetValue.h"
#include "strategy/values/LineTargetValue.h"
#include "strategy/values/TankTargetValue.h"
#include "strategy/values/DpsTargetValue.h"
#include "strategy/values/CcTargetValue.h"
#include "strategy/values/CurrentCcTargetValue.h"
#include "strategy/values/PetTargetValue.h"
#include "strategy/values/GrindTargetValue.h"
#include "strategy/values/RtiTargetValue.h"
#include "strategy/values/PartyMemberToDispel.h"
#include "strategy/values/StatsValues.h"
#include "strategy/values/AttackerCountValues.h"
#include "strategy/values/AttackersValue.h"
#include "strategy/values/AvailableLootValue.h"
#include "strategy/values/AlwaysLootListValue.h"
#include "strategy/values/LootStrategyValue.h"
#include "strategy/values/HasAvailableLootValue.h"
#include "strategy/values/LastMovementValue.h"
#include "strategy/values/DistanceValue.h"
#include "strategy/values/IsMovingValue.h"
#include "strategy/values/IsBehindValue.h"
#include "strategy/values/IsFacingValue.h"
#include "strategy/values/ItemCountValue.h"
#include "strategy/values/SpellIdValue.h"
#include "strategy/values/ItemForSpellValue.h"
#include "strategy/values/SpellCastUsefulValue.h"
#include "strategy/values/LastSpellCastValue.h"
#include "strategy/values/ChatValue.h"
#include "strategy/values/HasTotemValue.h"
#include "strategy/values/LeastHpTargetValue.h"
#include "strategy/values/AoeHealValues.h"
#include "strategy/values/RtiValue.h"
#include "strategy/values/PositionValue.h"
#include "strategy/values/ThreatValues.h"
#include "strategy/values/DuelTargetValue.h"
#include "strategy/values/InvalidTargetValue.h"
#include "strategy/values/EnemyPlayerValue.h"
#include "strategy/values/AttackerWithoutAuraTargetValue.h"
#include "strategy/values/LastSpellCastTimeValue.h"
#include "strategy/values/ManaSaveLevelValue.h"
#include "strategy/values/LfgValues.h"
#include "strategy/values/EnemyHealerTargetValue.h"
#include "strategy/values/Formations.h"
#include "strategy/values/ItemUsageValue.h"
#include "strategy/values/LastSaidValue.h"

namespace ai
{
    class ValueContext : public NamedObjectContext<UntypedValue>
    {
    public:
        ValueContext()
        {
            creators["nearest game objects"] = &ValueContext::nearest_game_objects;
            creators["nearest npcs"] = &ValueContext::nearest_npcs;
            creators["possible targets"] = &ValueContext::possible_targets;
            creators["nearest adds"] = &ValueContext::nearest_adds;
            creators["nearest corpses"] = &ValueContext::nearest_corpses;
            creators["log level"] = &ValueContext::log_level;
            creators["party member without aura"] = &ValueContext::party_member_without_aura;
            creators["attacker without aura"] = &ValueContext::attacker_without_aura;
            creators["party member to heal"] = &ValueContext::party_member_to_heal;
            creators["party member to resurrect"] = &ValueContext::party_member_to_resurrect;
            creators["current target"] = &ValueContext::current_target;
            creators["self target"] = &ValueContext::self_target;
            creators["master target"] = &ValueContext::master;
            creators["line target"] = &ValueContext::line_target;
            creators["tank target"] = &ValueContext::tank_target;
            creators["dps target"] = &ValueContext::dps_target;
            creators["least hp target"] = &ValueContext::least_hp_target;
            creators["enemy player target"] = &ValueContext::enemy_player_target;
            creators["cc target"] = &ValueContext::cc_target;
            creators["current cc target"] = &ValueContext::current_cc_target;
            creators["pet target"] = &ValueContext::pet_target;
            creators["old target"] = &ValueContext::old_target;
            creators["grind target"] = &ValueContext::grind_target;
            creators["rti target"] = &ValueContext::rti_target;
            creators["duel target"] = &ValueContext::duel_target;
            creators["party member to dispel"] = &ValueContext::party_member_to_dispel;
            creators["health"] = &ValueContext::health;
            creators["rage"] = &ValueContext::rage;
            creators["energy"] = &ValueContext::energy;
            creators["mana"] = &ValueContext::mana;
            creators["combo"] = &ValueContext::combo;
            creators["dead"] = &ValueContext::dead;
            creators["has mana"] = &ValueContext::has_mana;
            creators["attacker count"] = &ValueContext::attacker_count;
            creators["my attacker count"] = &ValueContext::my_attacker_count;
            creators["has aggro"] = &ValueContext::has_aggro;
            creators["mounted"] = &ValueContext::mounted;

            creators["can loot"] = &ValueContext::can_loot;
            creators["loot target"] = &ValueContext::loot_target;
            creators["available loot"] = &ValueContext::available_loot;
            creators["has available loot"] = &ValueContext::has_available_loot;
            creators["always loot list"] = &ValueContext::always_loot_list;
            creators["loot strategy"] = &ValueContext::loot_strategy;
            creators["last movement"] = &ValueContext::last_movement;
            creators["distance"] = &ValueContext::distance;
            creators["moving"] = &ValueContext::moving;
            creators["swimming"] = &ValueContext::swimming;
            creators["behind"] = &ValueContext::behind;
            creators["facing"] = &ValueContext::facing;

            creators["item count"] = &ValueContext::item_count;
            creators["inventory items"] = &ValueContext::inventory_item;

            creators["spell id"] = &ValueContext::spell_id;
            creators["item for spell"] = &ValueContext::item_for_spell;
            creators["spell cast useful"] = &ValueContext::spell_cast_useful;
            creators["last spell cast"] = &ValueContext::last_spell_cast;
            creators["last spell cast time"] = &ValueContext::last_spell_cast_time;
            creators["chat"] = &ValueContext::chat;
            creators["has totem"] = &ValueContext::has_totem;

            creators["aoe heal"] = &ValueContext::aoe_heal;

            creators["rti"] = &ValueContext::rti;
            creators["position"] = &ValueContext::position;
            creators["threat"] = &ValueContext::threat;

            creators["balance"] = &ValueContext::balance;
            creators["attackers"] = &ValueContext::attackers;
            creators["invalid target"] = &ValueContext::invalid_target;
            creators["mana save level"] = &ValueContext::mana_save_level;
            creators["combat"] = &ValueContext::combat;
            creators["lfg proposal"] = &ValueContext::lfg_proposal;
            creators["bag space"] = &ValueContext::bag_space;
            creators["enemy healer target"] = &ValueContext::enemy_healer_target;
            creators["formation"] = &ValueContext::formation;
            creators["item usage"] = &ValueContext::item_usage;
            creators["speed"] = &ValueContext::speed;
            creators["last said"] = &ValueContext::last_said;
            creators["last emote"] = &ValueContext::last_emote;
        }

    private:
        static UntypedValue* item_usage(PlayerbotAI* ai) { return new ItemUsageValue(ai); }
        static UntypedValue* formation(PlayerbotAI* ai) { return new FormationValue(ai); }
        static UntypedValue* mana_save_level(PlayerbotAI* ai) { return new ManaSaveLevelValue(ai); }
        static UntypedValue* invalid_target(PlayerbotAI* ai) { return new InvalidTargetValue(ai); }
        static UntypedValue* balance(PlayerbotAI* ai) { return new BalancePercentValue(ai); }
        static UntypedValue* attackers(PlayerbotAI* ai) { return new AttackersValue(ai); }

        static UntypedValue* position(PlayerbotAI* ai) { return new PositionValue(ai); }
        static UntypedValue* rti(PlayerbotAI* ai) { return new RtiValue(ai); }

        static UntypedValue* aoe_heal(PlayerbotAI* ai) { return new AoeHealValue(ai); }

        static UntypedValue* chat(PlayerbotAI* ai) { return new ChatValue(ai); }
        static UntypedValue* last_spell_cast(PlayerbotAI* ai) { return new LastSpellCastValue(ai); }
        static UntypedValue* last_spell_cast_time(PlayerbotAI* ai) { return new LastSpellCastTimeValue(ai); }
        static UntypedValue* spell_cast_useful(PlayerbotAI* ai) { return new SpellCastUsefulValue(ai); }
        static UntypedValue* item_for_spell(PlayerbotAI* ai) { return new ItemForSpellValue(ai); }
        static UntypedValue* spell_id(PlayerbotAI* ai) { return new SpellIdValue(ai); }
        static UntypedValue* inventory_item(PlayerbotAI* ai) { return new InventoryItemValue(ai); }
        static UntypedValue* item_count(PlayerbotAI* ai) { return new ItemCountValue(ai); }
        static UntypedValue* behind(PlayerbotAI* ai) { return new IsBehindValue(ai); }
        static UntypedValue* facing(PlayerbotAI* ai) { return new IsFacingValue(ai); }
        static UntypedValue* moving(PlayerbotAI* ai) { return new IsMovingValue(ai); }
        static UntypedValue* swimming(PlayerbotAI* ai) { return new IsSwimmingValue(ai); }
        static UntypedValue* distance(PlayerbotAI* ai) { return new DistanceValue(ai); }
        static UntypedValue* last_movement(PlayerbotAI* ai) { return new LastMovementValue(ai); }

        static UntypedValue* can_loot(PlayerbotAI* ai) { return new CanLootValue(ai); }
        static UntypedValue* available_loot(PlayerbotAI* ai) { return new AvailableLootValue(ai); }
        static UntypedValue* loot_target(PlayerbotAI* ai) { return new LootTargetValue(ai); }
        static UntypedValue* has_available_loot(PlayerbotAI* ai) { return new HasAvailableLootValue(ai); }
        static UntypedValue* always_loot_list(PlayerbotAI* ai) { return new AlwaysLootListValue(ai); }
        static UntypedValue* loot_strategy(PlayerbotAI* ai) { return new LootStrategyValue(ai); }

        static UntypedValue* attacker_count(PlayerbotAI* ai) { return new AttackerCountValue(ai); }
        static UntypedValue* my_attacker_count(PlayerbotAI* ai) { return new MyAttackerCountValue(ai); }
        static UntypedValue* has_aggro(PlayerbotAI* ai) { return new HasAggroValue(ai); }
        static UntypedValue* mounted(PlayerbotAI* ai) { return new IsMountedValue(ai); }
        static UntypedValue* health(PlayerbotAI* ai) { return new HealthValue(ai); }
        static UntypedValue* rage(PlayerbotAI* ai) { return new RageValue(ai); }
        static UntypedValue* energy(PlayerbotAI* ai) { return new EnergyValue(ai); }
        static UntypedValue* mana(PlayerbotAI* ai) { return new ManaValue(ai); }
        static UntypedValue* combo(PlayerbotAI* ai) { return new ComboPointsValue(ai); }
        static UntypedValue* dead(PlayerbotAI* ai) { return new IsDeadValue(ai); }
        static UntypedValue* has_mana(PlayerbotAI* ai) { return new HasManaValue(ai); }
        static UntypedValue* nearest_game_objects(PlayerbotAI* ai) { return new NearestGameObjects(ai); }
        static UntypedValue* log_level(PlayerbotAI* ai) { return new LogLevelValue(ai); }
        static UntypedValue* nearest_npcs(PlayerbotAI* ai) { return new NearestNpcsValue(ai); }
        static UntypedValue* nearest_corpses(PlayerbotAI* ai) { return new NearestCorpsesValue(ai); }
        static UntypedValue* possible_targets(PlayerbotAI* ai) { return new PossibleTargetsValue(ai); }
        static UntypedValue* nearest_adds(PlayerbotAI* ai) { return new NearestAdsValue(ai); }
        static UntypedValue* party_member_without_aura(PlayerbotAI* ai) { return new PartyMemberWithoutAuraValue(ai); }
        static UntypedValue* attacker_without_aura(PlayerbotAI* ai) { return new AttackerWithoutAuraTargetValue(ai); }
        static UntypedValue* party_member_to_heal(PlayerbotAI* ai) { return new PartyMemberToHeal(ai); }
        static UntypedValue* party_member_to_resurrect(PlayerbotAI* ai) { return new PartyMemberToResurrect(ai); }
        static UntypedValue* party_member_to_dispel(PlayerbotAI* ai) { return new PartyMemberToDispel(ai); }
        static UntypedValue* current_target(PlayerbotAI* ai) { return new CurrentTargetValue(ai); }
        static UntypedValue* old_target(PlayerbotAI* ai) { return new CurrentTargetValue(ai); }
        static UntypedValue* self_target(PlayerbotAI* ai) { return new SelfTargetValue(ai); }
        static UntypedValue* master(PlayerbotAI* ai) { return new MasterTargetValue(ai); }
        static UntypedValue* line_target(PlayerbotAI* ai) { return new LineTargetValue(ai); }
        static UntypedValue* tank_target(PlayerbotAI* ai) { return new TankTargetValue(ai); }
        static UntypedValue* dps_target(PlayerbotAI* ai) { return new DpsTargetValue(ai); }
        static UntypedValue* least_hp_target(PlayerbotAI* ai) { return new LeastHpTargetValue(ai); }
        static UntypedValue* enemy_player_target(PlayerbotAI* ai) { return new EnemyPlayerValue(ai); }
        static UntypedValue* cc_target(PlayerbotAI* ai) { return new CcTargetValue(ai); }
        static UntypedValue* current_cc_target(PlayerbotAI* ai) { return new CurrentCcTargetValue(ai); }
        static UntypedValue* pet_target(PlayerbotAI* ai) { return new PetTargetValue(ai); }
        static UntypedValue* grind_target(PlayerbotAI* ai) { return new GrindTargetValue(ai); }
        static UntypedValue* rti_target(PlayerbotAI* ai) { return new RtiTargetValue(ai); }
        static UntypedValue* duel_target(PlayerbotAI* ai) { return new DuelTargetValue(ai); }
        static UntypedValue* has_totem(PlayerbotAI* ai) { return new HasTotemValue(ai); }
        static UntypedValue* threat(PlayerbotAI* ai) { return new ThreatValue(ai); }
        static UntypedValue* combat(PlayerbotAI* ai) { return new IsInCombatValue(ai); }
        static UntypedValue* lfg_proposal(PlayerbotAI* ai) { return new LfgProposalValue(ai); }
        static UntypedValue* bag_space(PlayerbotAI* ai) { return new BagSpaceValue(ai); }
        static UntypedValue* enemy_healer_target(PlayerbotAI* ai) { return new EnemyHealerTargetValue(ai); }
        static UntypedValue* speed(PlayerbotAI* ai) { return new SpeedValue(ai); }
        static UntypedValue* last_said(PlayerbotAI* ai) { return new LastSaidValue(ai); }
        static UntypedValue* last_emote(PlayerbotAI* ai) { return new LastEmoteValue(ai); }
    };
};
