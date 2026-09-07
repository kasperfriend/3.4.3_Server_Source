#include "../pchdef.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "RandomItemMgr.h"

#include "../../plugins/ahbot/AhBot.h"
#include "../../server/database/Database/DatabaseEnv.h"
#include "PlayerbotAI.h"

#include "../../plugins/ahbot/AhBotConfig.h"

char * strstri (const char* str1, const char* str2);

class RandomItemGuildTaskPredicate : public RandomItemPredicate
{
public:
    virtual bool Apply(ItemTemplate const* proto)
    {
        if (proto->GetBonding() == BIND_ON_ACQUIRE ||
                proto->GetBonding() == BIND_QUEST ||
                proto->GetBonding() == BIND_ON_USE)
            return false;

        if (proto->GetQuality() < ITEM_QUALITY_UNCOMMON)
            return false;

        if ((proto->GetClass() == ITEM_CLASS_ARMOR || proto->GetClass() == ITEM_CLASS_WEAPON) && proto->GetQuality() >= ITEM_QUALITY_RARE)
            return true;

        if (proto->GetClass() == ITEM_CLASS_TRADE_GOODS || proto->GetClass() == ITEM_CLASS_CONSUMABLE)
            return true;

        return false;
    }
};

class RandomItemGuildTaskRewardPredicate : public RandomItemPredicate
{
public:
    RandomItemGuildTaskRewardPredicate(bool equip) { this->equip = equip; }

    virtual bool Apply(ItemTemplate const* proto)
    {
        if (proto->GetBonding() == BIND_ON_ACQUIRE ||
                proto->GetBonding() == BIND_QUEST ||
                proto->GetBonding() == BIND_ON_USE)
            return false;

        if (proto->GetQuality() < ITEM_QUALITY_RARE)
            return false;

        if (proto->GetClass() == ITEM_CLASS_QUEST)
            return false;

        if (equip && (proto->GetClass() == ITEM_CLASS_ARMOR || proto->GetClass() == ITEM_CLASS_WEAPON))
            return true;

        if (!equip && (proto->GetClass() == ITEM_CLASS_TRADE_GOODS || proto->GetClass() == ITEM_CLASS_CONSUMABLE))
            return true;

        return false;
    }

private:
    bool equip;
};

RandomItemMgr::RandomItemMgr()
{
    predicates[RANDOM_ITEM_GUILD_TASK] = new RandomItemGuildTaskPredicate();
    predicates[RANDOM_ITEM_GUILD_TASK_REWARD_EQUIP] = new RandomItemGuildTaskRewardPredicate(true);
    predicates[RANDOM_ITEM_GUILD_TASK_REWARD_TRADE] = new RandomItemGuildTaskRewardPredicate(false);
}

RandomItemMgr::~RandomItemMgr()
{
    for (map<RandomItemType, RandomItemPredicate*>::iterator i = predicates.begin(); i != predicates.end(); ++i)
        delete i->second;

    predicates.clear();
}

bool RandomItemMgr::HandleConsoleCommand(ChatHandler* handler, char const* args)
{
    if (!args || !*args)
    {
        TC_LOG_ERROR("gtask",  "Usage: rnditem");
        return false;
    }

    return false;
}

RandomItemList RandomItemMgr::Query(RandomItemType type, RandomItemPredicate* predicate)
{
    RandomItemList &list = cache[type];
    if (list.empty())
        list = cache[type] = Query(type);

    RandomItemList result;
    for (RandomItemList::iterator i = list.begin(); i != list.end(); ++i)
    {
        uint32 itemId = *i;
        ItemTemplate const* proto = sObjectMgr->GetItemTemplate(itemId);
        if (!proto)
            continue;

        if (predicate && !predicate->Apply(proto))
            continue;

        result.push_back(itemId);
    }

    return result;
}

RandomItemList RandomItemMgr::Query(RandomItemType type)
{
    RandomItemList items;

    ItemTemplateContainer const& itemTemplates = sObjectMgr->GetItemTemplateStore();
    for (ItemTemplateContainer::const_iterator i = itemTemplates.begin(); i != itemTemplates.end(); ++i)
    {
        uint32 itemId = i->first;
        ItemTemplate const* proto = &i->second;
        if (!proto)
            continue;

        if (proto->GetDuration() & 0x80000000)
            continue;

        if (sAhBotConfig.ignoreItemIds.find(proto->GetId()) != sAhBotConfig.ignoreItemIds.end())
            continue;

        if (strstri(proto->GetDefaultLocaleName(), "qa") || strstri(proto->GetDefaultLocaleName(), "test") || strstri(proto->GetDefaultLocaleName(), "deprecated"))
            continue;

        // Zero is the default/unlimited setting, not a ban on all nonzero levels.
        if ((sAhBotConfig.maxRequiredLevel && proto->GetBaseRequiredLevel() > sAhBotConfig.maxRequiredLevel) ||
            (sAhBotConfig.maxItemLevel && proto->GetItemLevel() > sAhBotConfig.maxItemLevel))
            continue;

        if (predicates[type] && !predicates[type]->Apply(proto))
            continue;

        if (!auctionbot.GetSellPrice(proto))
            continue;

        items.push_back(itemId);
    }

    if (items.empty())
        TC_LOG_DEBUG("gtask",  "no items available for random item query {}", type);

    return items;
}

uint32 RandomItemMgr::GetRandomItem(RandomItemType type, RandomItemPredicate* predicate)
{
    RandomItemList const& list = Query(type, predicate);
    if (list.empty())
        return 0;

    uint32 index = urand(0, list.size() - 1);
    uint32 itemId = list[index];

    return itemId;
}
