#pragma once

#include "strategy/CustomStrategy.h"
#include "strategy/generic/NonCombatStrategy.h"
#include "strategy/generic/RacialsStrategy.h"
#include "strategy/generic/ChatCommandHandlerStrategy.h"
#include "strategy/generic/WorldPacketHandlerStrategy.h"
#include "strategy/generic/DeadStrategy.h"
#include "strategy/generic/QuestStrategies.h"
#include "strategy/generic/LootNonCombatStrategy.h"
#include "strategy/generic/DuelStrategy.h"
#include "strategy/generic/KiteStrategy.h"
#include "strategy/generic/FleeStrategy.h"
#include "strategy/generic/FollowMasterStrategy.h"
#include "strategy/generic/RunawayStrategy.h"
#include "strategy/generic/StayStrategy.h"
#include "strategy/generic/UseFoodStrategy.h"
#include "strategy/generic/ConserveManaStrategy.h"
#include "strategy/generic/EmoteStrategy.h"
#include "strategy/generic/TankAoeStrategy.h"
#include "strategy/generic/DpsAssistStrategy.h"
#include "strategy/generic/PassiveStrategy.h"
#include "strategy/generic/GrindingStrategy.h"
#include "strategy/generic/UsePotionsStrategy.h"
#include "strategy/generic/GuardStrategy.h"
#include "strategy/generic/CastTimeStrategy.h"
#include "strategy/generic/ThreatStrategy.h"
#include "strategy/generic/TellTargetStrategy.h"
#include "strategy/generic/AttackEnemyPlayersStrategy.h"
#include "strategy/generic/MoveRandomStrategy.h"

namespace ai
{
    class StrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        StrategyContext()
        {
            creators["racials"] = &StrategyContext::racials;
            creators["loot"] = &StrategyContext::loot;
            creators["gather"] = &StrategyContext::gather;
            creators["emote"] = &StrategyContext::emote;
            creators["passive"] = &StrategyContext::passive;
            creators["conserve mana"] = &StrategyContext::conserve_mana;
            creators["food"] = &StrategyContext::food;
            creators["chat"] = &StrategyContext::chat;
            creators["default"] = &StrategyContext::world_packet;
            creators["ready check"] = &StrategyContext::ready_check;
            creators["dead"] = &StrategyContext::dead;
            creators["flee"] = &StrategyContext::flee;
            creators["duel"] = &StrategyContext::duel;
            creators["kite"] = &StrategyContext::kite;
            creators["potions"] = &StrategyContext::potions;
            creators["cast time"] = &StrategyContext::cast_time;
            creators["threat"] = &StrategyContext::threat;
            creators["tell target"] = &StrategyContext::tell_target;
            creators["pvp"] = &StrategyContext::pvp;
            creators["move random"] = &StrategyContext::move_random;
            creators["lfg"] = &StrategyContext::lfg;
            creators["custom"] = &StrategyContext::custom;
        }

    private:
        static Strategy* tell_target(PlayerbotAI* ai) { return new TellTargetStrategy(ai); }
        static Strategy* threat(PlayerbotAI* ai) { return new ThreatStrategy(ai); }
        static Strategy* cast_time(PlayerbotAI* ai) { return new CastTimeStrategy(ai); }
        static Strategy* potions(PlayerbotAI* ai) { return new UsePotionsStrategy(ai); }
        static Strategy* kite(PlayerbotAI* ai) { return new KiteStrategy(ai); }
        static Strategy* duel(PlayerbotAI* ai) { return new DuelStrategy(ai); }
        static Strategy* flee(PlayerbotAI* ai) { return new FleeStrategy(ai); }
        static Strategy* dead(PlayerbotAI* ai) { return new DeadStrategy(ai); }
        static Strategy* racials(PlayerbotAI* ai) { return new RacialsStrategy(ai); }
        static Strategy* loot(PlayerbotAI* ai) { return new LootNonCombatStrategy(ai); }
        static Strategy* gather(PlayerbotAI* ai) { return new GatherStrategy(ai); }
        static Strategy* emote(PlayerbotAI* ai) { return new EmoteStrategy(ai); }
        static Strategy* passive(PlayerbotAI* ai) { return new PassiveStrategy(ai); }
        static Strategy* conserve_mana(PlayerbotAI* ai) { return new ConserveManaStrategy(ai); }
        static Strategy* food(PlayerbotAI* ai) { return new UseFoodStrategy(ai); }
        static Strategy* chat(PlayerbotAI* ai) { return new ChatCommandHandlerStrategy(ai); }
        static Strategy* world_packet(PlayerbotAI* ai) { return new WorldPacketHandlerStrategy(ai); }
        static Strategy* ready_check(PlayerbotAI* ai) { return new ReadyCheckStrategy(ai); }
        static Strategy* pvp(PlayerbotAI* ai) { return new AttackEnemyPlayersStrategy(ai); }
        static Strategy* move_random(PlayerbotAI* ai) { return new MoveRandomStrategy(ai); }
        static Strategy* lfg(PlayerbotAI* ai) { return new LfgStrategy(ai); }
        static Strategy* custom(PlayerbotAI* ai) { return new CustomStrategy(ai); }
    };

    class MovementStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        MovementStrategyContext() : NamedObjectContext<Strategy>(false, true)
        {
            creators["follow"] = &MovementStrategyContext::follow_master;
            creators["stay"] = &MovementStrategyContext::stay;
            creators["runaway"] = &MovementStrategyContext::runaway;
            creators["flee from adds"] = &MovementStrategyContext::flee_from_adds;
            creators["guard"] = &MovementStrategyContext::guard;
        }

    private:
        static Strategy* guard(PlayerbotAI* ai) { return new GuardStrategy(ai); }
        static Strategy* follow_master(PlayerbotAI* ai) { return new FollowMasterStrategy(ai); }
        static Strategy* stay(PlayerbotAI* ai) { return new StayStrategy(ai); }
        static Strategy* runaway(PlayerbotAI* ai) { return new RunawayStrategy(ai); }
        static Strategy* flee_from_adds(PlayerbotAI* ai) { return new FleeFromAddsStrategy(ai); }
    };

    class AssistStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        AssistStrategyContext() : NamedObjectContext<Strategy>(false, true)
        {
            creators["dps assist"] = &AssistStrategyContext::dps_assist;
            creators["tank aoe"] = &AssistStrategyContext::tank_aoe;
            creators["grind"] = &AssistStrategyContext::grind;
        }

    private:
        static Strategy* dps_assist(PlayerbotAI* ai) { return new DpsAssistStrategy(ai); }
        static Strategy* tank_aoe(PlayerbotAI* ai) { return new TankAoeStrategy(ai); }
        static Strategy* grind(PlayerbotAI* ai) { return new GrindingStrategy(ai); }
    };

    class QuestStrategyContext : public NamedObjectContext<Strategy>
    {
    public:
        QuestStrategyContext() : NamedObjectContext<Strategy>(false, true)
        {
            creators["quest"] = &QuestStrategyContext::quest;
            creators["accept all quests"] = &QuestStrategyContext::accept_all_quests;
        }

    private:
        static Strategy* quest(PlayerbotAI* ai) { return new DefaultQuestStrategy(ai); }
        static Strategy* accept_all_quests(PlayerbotAI* ai) { return new AcceptAllQuestsStrategy(ai); }
    };
};
