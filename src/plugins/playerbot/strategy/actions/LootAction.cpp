#include "../../../pchdef.h"
#include "../../playerbot.h"
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
        SkillType skill = SkillType(creature->GetCreatureTemplate()->GetDifficulty(DIFFICULTY_NONE)->GetRequiredLootSkill());
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

    for (uint32 spellId = 0; spellId < sSpellNameStore.GetNumRows(); spellId++)
    {
        if (spellId == MINING || spellId == HERB_GATHERING)
            continue;

        const SpellInfo* pSpellInfo = sSpellMgr->GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!pSpellInfo)
            continue;

        if (CanOpenLock(lootObject, pSpellInfo, go))
            return spellId;
    }

    return 0; //Spell 3365 = Opening?
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

    WorldPacket p(event.getPacket());

    // The event packet is the SMSG_LOOT_RESPONSE the core just built for this
    // bot (WorldPackets::Loot::LootResponse::Write(), filled by
    // Loot::BuildLootResponse in Player::SendLootResponse). This action used to
    // parse the classic 3.3.5 wire layout (loot guid, loot type, gold, item
    // count and fixed per-item index/id/count/slot fields), but the 3.4.3
    // packet is a different format: it starts with the owner and loot guids,
    // sizes are uint32, item headers are bit-packed and each item embeds an
    // ItemInstance. Reading the old layout walked off the end of the buffer and
    // ByteBufferException escaped the AI engine on a map worker thread, where
    // an uncaught exception calls std::terminate and takes the whole
    // worldserver (and every real player on it) down with the bot. Parse the
    // 3.4.3 layout instead, and treat any packet that cannot be parsed
    // (truncated, corrupt, layout drift) as "nothing to loot" rather than
    // letting it throw out of the action.
    try
    {
        p.rpos(0);
        p.ResetBitPos(); // buffer may carry writer bit state; start byte aligned

        ObjectGuid ownerGuid;   // looted object that owns the window (source guid)
        ObjectGuid lootObjGuid; // per-loot-object window guid (HighGuid::LootObject)
        uint8 lootType = 0;     // client loot type (GetLootTypeForClient)
        uint32 gold = 0;

        p >> ownerGuid;             // Owner
        p >> lootObjGuid;           // LootObj
        p.read_skip<uint8>();       // FailureReason (LootError); 0 when a window opens
        p >> lootType;              // AcquireReason
        p.read_skip<uint8>();       // LootMethod
        p.read_skip<uint8>();       // Threshold
        p >> gold;                  // Coins

        uint32 items = 0;           // Items.size()
        uint32 currencies = 0;      // Currencies.size()
        p >> items;
        p >> currencies;

        bool acquired = p.ReadBit();    // Acquired; false => error response
        p.ReadBit();                    // AELooting
        p.ReadBit();                    // PersonalLooting
        p.ResetBitPos();

        if (!acquired)
            return false;               // loot error ("didn't kill" etc.), nothing offered

        if (gold > 0)
        {
            // CMSG_LOOT_MONEY in 3.4.3 carries a single IsSoftInteract bit
            WorldPacket* const moneyPacket = new WorldPacket(CMSG_LOOT_MONEY, 1);
            moneyPacket->WriteBit(false);
            moneyPacket->FlushBits();
            bot->GetSession()->QueuePacket(moneyPacket);
        }

        for (uint32 i = 0; i < items; ++i)
        {
            // LootItemData header (operator<< for WorldPackets::Loot::LootItemData):
            // Type(2) UIType(3) CanTradeToTapList(1). UIType is the LootSlotType
            // granted to this player (ALLOW_LOOT/OWNER/MASTER/LOCKED/...).
            p.ReadBits(2);                  // Type (unused by the server)
            uint32 lootslot_type = p.ReadBits(3); // UIType
            p.ReadBit();                    // CanTradeToTapList
            p.ResetBitPos();

            // ItemInstance (operator<< for WorldPackets::Item::ItemInstance):
            // ItemID, random properties, bonus bit, ItemModList and optional
            // ItemBonuses. Only ItemID and Quantity are needed, skip the rest.
            int32 itemid = 0;
            p >> itemid;
            p.read_skip<int32>();           // RandomPropertiesSeed
            p.read_skip<int32>();           // RandomPropertiesID
            bool hasItemBonus = p.ReadBit();
            p.ResetBitPos();

            uint32 mods = p.ReadBits(6);    // ItemModList::Values size
            p.ResetBitPos();
            for (uint32 m = 0; m < mods; ++m)
            {
                p.read_skip<int32>();       // Value
                p.read_skip<uint8>();       // Type
            }

            if (hasItemBonus)
            {
                p.read_skip<uint8>();       // Context
                uint32 bonusIds = 0;
                p >> bonusIds;
                for (uint32 b = 0; b < bonusIds; ++b)
                    p.read_skip<uint32>();
            }

            uint32 itemcount = 0;
            p >> itemcount;                 // Quantity
            p.read_skip<uint8>();           // LootItemType
            uint8 lootListId = 0;
            p >> lootListId;                // 1-based slot, echoed back in CMSG_LOOT_ITEM

            if (lootslot_type != LOOT_SLOT_TYPE_ALLOW_LOOT && lootslot_type != LOOT_SLOT_TYPE_OWNER)
                continue;

            if (lootType != LOOT_SKINNING && !IsLootAllowed(uint32(itemid)))
                continue;

            if (sRandomPlayerbotMgr.IsRandomBot(bot))
            {
                ItemTemplate const *proto = sObjectMgr->GetItemTemplate(uint32(itemid));
                if (proto)
                {
                    uint32 price = itemcount * auctionbot.GetSellPrice(proto) * sRandomPlayerbotMgr.GetSellMultiplier(bot) + gold;
                    uint32 lootAmount = sRandomPlayerbotMgr.GetLootAmount(bot);
                    if (bot->GetGroup() && price)
                    {
                        sRandomPlayerbotMgr.SetLootAmount(bot, lootAmount + price);
                    }
                    else if (lootAmount)
                    {
                        sRandomPlayerbotMgr.SetLootAmount(bot, 0);
                    }

                    Group* group = bot->GetGroup();
                    if (group)
                    {
                        for (GroupReference *ref = group->GetFirstMember(); ref; ref = ref->next())
                        {
                            // group members can drop (logout/disconnect) while the bot
                            // loots; GetSource() then returns null
                            Player* member = ref->GetSource();
                            if (member && member != bot)
                                sGuildTaskMgr.CheckItemTask(uint32(itemid), itemcount, member, bot);
                        }
                    }
                }
            }

            // CMSG_LOOT_ITEM in 3.4.3 (WorldPackets::Loot::LootItem::Read):
            // uint32 request count, per request the object guid + LootListID
            // (slot + 1 as sent by the server) and one trailing IsSoftInteract
            // bit. The core handler stores item (LootListID - 1) of the player's
            // currently open loot.
            WorldPacket* const packet = new WorldPacket(CMSG_LOOT_ITEM, 14);
            *packet << uint32(1);
            *packet << ownerGuid;
            *packet << lootListId;
            packet->WriteBit(false);        // IsSoftInteract
            packet->FlushBits();
            bot->GetSession()->QueuePacket(packet);
        }

        AI_VALUE(LootObjectStack*, "available loot")->Remove(ownerGuid);

        // release loot
        WorldPacket* const packet = new WorldPacket(CMSG_LOOT_RELEASE, 8);
        *packet << ownerGuid;
        bot->GetSession()->QueuePacket(packet);
        return true;
    }
    catch (std::exception const& e)
    {
        TC_LOG_ERROR("playerbot", "Bot {} failed to store loot from a loot response: {}", bot->GetName(), e.what());
    }
    catch (...)
    {
        TC_LOG_ERROR("playerbot", "Bot {} failed to store loot from a loot response with an unknown exception", bot->GetName());
    }

    return false;
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
