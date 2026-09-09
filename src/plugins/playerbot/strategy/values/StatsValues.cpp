#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/values/StatsValues.h"

using namespace ai;

uint8 HealthValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 100;
    if (!target->GetMaxHealth())
        return 0;
    return (static_cast<float> (target->GetHealth()) / target->GetMaxHealth()) * 100;
}

bool IsDeadValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;
    return target->getDeathState() != ALIVE;
}


uint8 RageValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 0;
    return (static_cast<float> (target->GetPower(POWER_RAGE)));
}

uint8 EnergyValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 0;
    return (static_cast<float> (target->GetPower(POWER_ENERGY)));
}

uint8 ManaValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 100;
    // Classes without mana (warrior, rogue, death knight, ...) have zero max
    // mana: 0/0 is NaN and converting it to uint8 is undefined behavior. They
    // count as having no mana, which is what every mana gate ("!mana || ...")
    // already expects.
    if (!target->GetMaxPower(POWER_MANA))
        return 0;
    return (static_cast<float> (target->GetPower(POWER_MANA)) / target->GetMaxPower(POWER_MANA)) * 100;
}

bool HasManaValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;
    return target->GetPower(POWER_MANA);
}


uint8 ComboPointsValue::Calculate()
{
    Unit *target = GetTarget();
    if (!target || target->GetGUID() != bot->GetComboTargetGUID())
        return 0;

    return bot->GetComboPoints();
}

bool IsMountedValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    return target->IsMounted();
}


bool IsInCombatValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return false;

    return target->IsInCombat();
}

uint8 BagSpaceValue::Calculate()
{
    uint32 totalused = 0, total = 16;
    for (uint8 slot = INVENTORY_SLOT_ITEM_START; slot < INVENTORY_SLOT_ITEM_END; slot++)
    {
        if (bot->GetItemByPos(INVENTORY_SLOT_BAG_0, slot))
            totalused++;
    }

    uint32 totalfree = 16 - totalused;
    for (uint8 bag = INVENTORY_SLOT_BAG_START; bag < INVENTORY_SLOT_BAG_END; ++bag)
    {
        const Bag* const pBag = (Bag*) bot->GetItemByPos(INVENTORY_SLOT_BAG_0, bag);
        if (pBag)
        {
            ItemTemplate const* pBagProto = pBag->GetTemplate();
            if (pBagProto->GetClass() == ITEM_CLASS_CONTAINER && pBagProto->GetSubClass() == ITEM_SUBCLASS_CONTAINER)
            {
                total += pBag->GetBagSize();
                totalfree += pBag->GetFreeSlots();
            }
        }

    }

    return (static_cast<float> (totalused) / total) * 100;
}

uint8 SpeedValue::Calculate()
{
    Unit* target = GetTarget();
    if (!target)
        return 100;

    return (uint8) (100.0f * target->GetSpeedRate(MOVE_RUN));
}
