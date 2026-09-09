#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "../../PlayerbotPackets.h"
#include "strategy/actions/LootAction.h"

#include "../../LootObjectStack.h"
#include "../../PlayerbotAIConfig.h"
#include "../../../ahbot/AhBot.h"
#include "../../RandomPlayerbotMgr.h"
#include "../values/ItemUsageValue.h"
#include "../../GuildTaskMgr.h"

using namespace ai;

bool LootAction::Execute(Event event)
{
    if (!AI_VALUE(bool, "has available loot"))
        return false;

    LootObject const& lootObject = AI_VALUE(LootObjectStack*, "available loot")->GetLoot(sPlayerbotAIConfig.lootDistance);
    context->GetValue<LootObject>("loot target")->Set(lootObject);
    return true;
}

enum ProfessionSpells
{
    ALCHEMY                      = 2259,
    BLACKSMITHING                = 2018,
    COOKING                      = 2550,
    ENCHANTING                   = 7411,
    ENGINEERING                  = 49383,
    FIRST_AID                    = 3273,
    FISHING                      = 7620,
    HERB_GATHERING               = 2366,
    INSCRIPTION                  = 45357,
    JEWELCRAFTING                = 25229,
    MINING                       = 2575,
    SKINNING                     = 8613,
    TAILORING                    = 3908
};

bool OpenLootAction::Execute(Event event)
{
    LootObject lootObject = AI_VALUE(LootObject, "loot target");
    bool result = DoLoot(lootObject);
    if (result)
    {
        AI_VALUE(LootObjectStack*, "available loot")->Remove(lootObject.guid);
        context->GetValue<LootObject>("loot target")->Set(LootObject());
    }
    return result;
}

bool OpenLootAction::DoLoot(LootObject& lootObject)
{
    if (lootObject.IsEmpty())
        return false;

    Creature* creature = ai->GetCreature(lootObject.guid);
    if (creature && bot->GetDistance(creature) > INTERACTION_DISTANCE)
        return false;

    if (creature && creature->HasDynamicFlag(UNIT_DYNFLAG_LOOTABLE))
    {
        bot->GetMotionMaster()->Clear();
        WorldPacket* const packet = new WorldPacket(CMSG_LOOT_UNIT, 8);
        *packet << lootObject.guid;
        bot->GetSession()->QueuePacket(packet);
        return true;
    }

    if (creature)
    {
        // the creature template (and its difficulty row) can vanish on a reload
        // while the bot still sees this lootable unit - bail out instead of
        // dereferencing the missing entry
        CreatureTemplate const* creatureTemplate = creature->GetCreatureTemplate();
        if (!creatureTemplate)
            return false;

        CreatureDifficulty const* difficulty = creatureTemplate->GetDifficulty(DIFFICULTY_NONE);
        if (!difficulty)
            return false;

        SkillType skill = SkillType(difficulty->GetRequiredLootSkill());
        if (!CanOpenLock(skill, lootObject.reqSkillValue))
            return false;

        bot->GetMotionMaster()->Clear();
        switch (skill)
        {
        case SKILL_ENGINEERING:
            return bot->HasSkill(SKILL_ENGINEERING) ? ai->CastSpell(ENGINEERING, creature) : false;
        case SKILL_HERBALISM:
            return bot->HasSkill(SKILL_HERBALISM) ? ai->CastSpell(32605, creature) : false;
        case SKILL_MINING:
            return bot->HasSkill(SKILL_MINING) ? ai->CastSpell(32606, creature) : false;
        default:
            return bot->HasSkill(SKILL_SKINNING) ? ai->CastSpell(SKINNING, creature) : false;
        }
    }

    GameObject* go = ai->GetGameObject(lootObject.guid);
    if (go && bot->GetDistance(go) > INTERACTION_DISTANCE)
        return false;

    bot->GetMotionMaster()->Clear();
    if (lootObject.skillId == SKILL_MINING)
        return bot->HasSkill(SKILL_MINING) ? ai->CastSpell(MINING, bot) : false;

    if (lootObject.skillId == SKILL_HERBALISM)
        return bot->HasSkill(SKILL_HERBALISM) ? ai->CastSpell(HERB_GATHERING, bot) : false;

    uint32 spellId = GetOpeningSpell(lootObject);
    if (!spellId)
        return false;

    return ai->CastSpell(spellId, bot);
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject)
{
    GameObject* go = ai->GetGameObject(lootObject.guid);
    if (go && go->isSpawned())
        return GetOpeningSpell(lootObject, go);

    return 0;
}

uint32 OpenLootAction::GetOpeningSpell(LootObject& lootObject, GameObject* go)
{
    for (PlayerSpellMap::iterator itr = bot->GetSpellMap().begin(); itr != bot->GetSpellMap().end(); ++itr)
    {
        uint32 spellId = itr->first;

        const SpellInfo* pSpellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!pSpellInfo)
            continue;

        if (itr->second.state == PLAYERSPELL_REMOVED || itr->second.disabled || pSpellInfo->IsPassive())
            continue;

        if (spellId == MINING || spellId == HERB_GATHERING)
            continue;

        if (CanOpenLock(lootObject, pSpellInfo, go))
            return spellId;
    }

    return 0;
}

bool OpenLootAction::CanOpenLock(LootObject& lootObject, const SpellInfo* pSpellInfo, GameObject* go)
{
    for (int effIndex = 0; effIndex <= EFFECT_2; effIndex++)
    {
        if (pSpellInfo->GetEffect(SpellEffIndex(effIndex)).Effect != SPELL_EFFECT_OPEN_LOCK && pSpellInfo->GetEffect(SpellEffIndex(effIndex)).Effect != SPELL_EFFECT_SKINNING)
            return false;

        uint32 lockId = go->GetGOInfo()->GetLockId();
        if (!lockId)
            return false;

        LockEntry const *lockInfo = sLockStore.LookupEntry(lockId);
        if (!lockInfo)
            return false;

        bool reqKey = false;                                    // some locks not have reqs

        for(int j = 0; j < 8; ++j)
        {
            switch(lockInfo->Type[j])
            {
            /*
            case LOCK_KEY_ITEM:
                return true;
            */
            case LOCK_KEY_SKILL:
                {
                    if(uint32(pSpellInfo->GetEffect(SpellEffIndex(effIndex)).MiscValue) != lockInfo->Index[j])
                        continue;

                    uint32 skillId = SkillByLockType(LockType(lockInfo->Index[j]));
                    if (skillId == SKILL_NONE)
                        return true;

                    if (CanOpenLock(skillId, lockInfo->Skill[j]))
                        return true;
                }
            }
        }
    }

    return false;
}

bool OpenLootAction::CanOpenLock(uint32 skillId, uint32 reqSkillValue)
{
    uint32 skillValue = bot->GetSkillValue(skillId);
    return skillValue >= reqSkillValue || !reqSkillValue;
}

bool StoreLootAction::Execute(Event event)
{
    if (!bot || !bot->GetSession())
        return false;

    // Decode the ENTIRE response before queuing money/items or changing bot
    // state. A truncated optional ItemInstance/currency tail must do nothing.
    packets::LootResponse loot;
    if (!packets::ReadLootResponse(event.getPacket(), loot))
        return false;
    if (!loot.Acquired)
    {
        AI_VALUE(LootObjectStack*, "available loot")->Remove(loot.Owner);
        return false;
    }
    if (loot.Owner.IsEmpty() || loot.Loot.IsEmpty() || bot->GetLootGUID() != loot.Owner)
        return false; // stale response from a previously opened corpse/container

    if (loot.Gold)
        bot->GetSession()->QueuePacket(new WorldPacket(packets::LootMoneyRequest()));

    for (packets::LootItem const& item : loot.Items)
    {
        if (item.UIType != LOOT_SLOT_TYPE_ALLOW_LOOT && item.UIType != LOOT_SLOT_TYPE_OWNER)
            continue;
        if (!item.Item.ItemID || !item.Quantity || (loot.Type != LOOT_SKINNING && !IsLootAllowed(item.Item.ItemID)))
            continue;
        // The wire request uses LootObj and the server's one-based list ID.
        bot->GetSession()->QueuePacket(new WorldPacket(packets::LootItemRequest(loot.Loot, item.ListID)));
    }

    // Guild/discount credit is granted by the subsequent confirmed item-push
    // notification, not by a preview that can still fail with a full inventory.
    AI_VALUE(LootObjectStack*, "available loot")->Remove(loot.Owner);
    bot->GetSession()->QueuePacket(new WorldPacket(packets::LootReleaseRequest(loot.Owner)));
    return true;
}

bool StoreLootAction::IsLootAllowed(uint32 itemid)
{
    LootStrategy lootStrategy = AI_VALUE(LootStrategy, "loot strategy");

    if (lootStrategy == LOOTSTRATEGY_ALL)
        return true;

    set<uint32>& lootItems = AI_VALUE(set<uint32>&, "always loot list");
    if (lootItems.find(itemid) != lootItems.end())
        return true;

    ItemTemplate const *proto = sObjectMgr->GetItemTemplate(itemid);
    if (!proto)
        return false;

    uint32 max = proto->GetMaxCount();
    if (max > 0 && bot->HasItemCount(itemid, max, true))
        return false;

    if (proto->GetStartQuest() ||
        proto->GetBonding() == BIND_QUEST ||
        proto->GetClass() == ITEM_CLASS_QUEST)
        return true;

    if (lootStrategy == LOOTSTRATEGY_QUEST)
        return false;

    ostringstream out; out << itemid;
    ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
    if (usage == ITEM_USAGE_SKILL || usage == ITEM_USAGE_USE || usage == ITEM_USAGE_GUILD_TASK)
        return true;

    if (lootStrategy == LOOTSTRATEGY_SKILL)
        return false;

    if (proto->GetClass() == ITEM_CLASS_MONEY || proto->GetQuality() == ITEM_QUALITY_POOR)
        return true;

    if (lootStrategy == LOOTSTRATEGY_GRAY)
        return true;

    if (proto->GetBonding() == BIND_ON_ACQUIRE)
        return false;

    return true;
}
