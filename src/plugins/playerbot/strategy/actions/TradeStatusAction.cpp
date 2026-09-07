#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/TradeStatusAction.h"

#include "../ItemVisitors.h"
#include "../../PlayerbotAIConfig.h"
#include "../../../ahbot/AhBot.h"
#include "../../RandomPlayerbotMgr.h"
#include "../../GuildTaskMgr.h"
#include "../values/ItemUsageValue.h"
#include <limits>

using namespace ai;



bool TradeStatusAction::Execute(Event event)
{
    Player* trader = bot->GetTrader();
    Player* master = GetMaster();
    if (!trader || !master || !bot->GetTradeData() || !trader->GetTradeData() || event.getPacket().GetOpcode() != SMSG_TRADE_STATUS)
        return false;

    if (trader != master)
    {
		bot->Whisper("I'm kind of busy now", LANG_UNIVERSAL, trader);
    }

    if (trader != master || !ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_ALLOW_ALL, true, master))
    {
        WorldPackets::Trade::CancelTrade cancelTrade{WorldPacket(CMSG_CANCEL_TRADE)};
        bot->GetSession()->HandleCancelTradeOpcode(cancelTrade);
        return false;
    }

    WorldPacket p(event.getPacket());
    p.rpos(0);
    p.ResetBitPos();
    p.ReadBit();                                    // PartnerIsSameBnetAccount
    uint32 status = p.ReadBits(5);

    if (status == TRADE_STATUS_ACCEPTED)
    {
        WorldPackets::Trade::AcceptTrade acceptTrade{WorldPacket(CMSG_ACCEPT_TRADE)};
        // The core rejects a default/old index once either side changes an item.
        acceptTrade.StateIndex = trader->GetTradeData()->GetServerStateIndex();

        if (CheckTrade())
        {
            int32 botMoney = CalculateCost(bot->GetTradeData(), true);

            map<uint32, uint32> itemIds;
            for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
            {
                Item* item = master->GetTradeData()->GetItem((TradeSlots)slot);
                if (item)
                    itemIds[item->GetTemplate()->GetId()] += item->GetCount();
            }

            bot->GetSession()->HandleAcceptTradeOpcode(acceptTrade);
            if (bot->GetTradeData())
                return false;

            for (map<uint32, uint32>::iterator i = itemIds.begin(); i != itemIds.end(); ++i)
                sGuildTaskMgr.CheckItemTask(i->first, i->second, master, bot);

            if (sRandomPlayerbotMgr.IsRandomBot(bot))
            {
                uint32 lootAmount = sRandomPlayerbotMgr.GetLootAmount(bot);
                uint64 spent = uint64(botMoney) * 10;
                sRandomPlayerbotMgr.SetLootAmount(bot, spent >= lootAmount ? 0 : uint32(lootAmount - spent));
            }
            return true;
        }
    }
    else if (status == TRADE_STATUS_PROPOSED)
    {
        if (!bot->isInFront(trader, M_PI / 2))
            bot->SetFacingToObject(trader);
        BeginTrade();
        return true;
    }

    return false;
}


void TradeStatusAction::BeginTrade()
{
    WorldPackets::Trade::BeginTrade beginTrade{WorldPacket(CMSG_BEGIN_TRADE)};
    bot->GetSession()->HandleBeginTradeOpcode(beginTrade);

    ListItemsVisitor visitor;
    IterateItems(&visitor);

    ai->TellMaster("=== Trade ===");
    TellItems(visitor.items);

    if (sRandomPlayerbotMgr.IsRandomBot(bot))
    {
        uint32 discount = sRandomPlayerbotMgr.GetTradeDiscount(bot);
        if (discount)
        {
            ostringstream out; out << "Discount up to: " << chat->formatMoney(discount);
            ai->TellMaster(out);
        }
    }
}

bool TradeStatusAction::CheckTrade()
{
    if (!sRandomPlayerbotMgr.IsRandomBot(bot))
        return true;

    Player* master = GetMaster();
    if (!master || !bot->GetTradeData() || !master->GetTradeData())
        return false;

    for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
    {
        Item* item = bot->GetTradeData()->GetItem((TradeSlots)slot);
        if (item)
        {
            // a template deleted while the trade window is open must not crash
            // the bot tick; treat the item as not sellable
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                return false;
            if (!auctionbot.GetSellPrice(proto))
            {
                ostringstream out;
                out << chat->formatItem(proto) << " - This is not for sale";
                ai->TellMaster(out);
                return false;
            }
        }

        item = master->GetTradeData()->GetItem((TradeSlots)slot);
        if (item)
        {
            ItemTemplate const* proto = item->GetTemplate();
            if (!proto)
                continue;

            ostringstream out; out << proto->GetId();
            ItemUsage usage = AI_VALUE2(ItemUsage, "item usage", out.str());
            if (!auctionbot.GetBuyPrice(proto) || usage == ITEM_USAGE_NONE)
            {
                ostringstream out;
                out << chat->formatItem(proto) << " - I don't need this";
                ai->TellMaster(out);
                return false;
            }
        }
    }

    int32 botItemsMoney = CalculateCost(bot->GetTradeData(), true);
    int64 botMoney = int64(bot->GetTradeData()->GetMoney()) + botItemsMoney;
    int32 playerItemsMoney = CalculateCost(master->GetTradeData(), false);
    int64 playerMoney = int64(master->GetTradeData()->GetMoney()) + playerItemsMoney;

    if (!botMoney && !playerMoney)
        return true;

    if (!botItemsMoney && !playerItemsMoney)
    {
        ai->TellMaster("There are no items to trade");
        return false;
    }

    int64 discount = std::min<int64>(botItemsMoney, sRandomPlayerbotMgr.GetTradeDiscount(bot));
    botMoney = std::max<int64>(0, botMoney - discount);

    if (playerMoney >= botMoney)
    {
        switch (urand(0, 4)) {
        case 0:
            ai->TellMaster("A pleasure doing business with you");
            break;
        case 1:
            ai->TellMaster("Fair trade");
            break;
        case 2:
            ai->TellMaster("Thanks");
            break;
        case 3:
            ai->TellMaster("Off with you");
            break;
        }
        return true;
    }

    ostringstream out;
    out << "I want " << chat->formatMoney(botMoney - playerMoney) << " for this";
    ai->TellMaster(out);
    return false;
}

int32 TradeStatusAction::CalculateCost(TradeData* data, bool sell)
{
    if (!data)
        return 0;

    double sum = 0.0;
    for (uint32 slot = 0; slot < TRADE_SLOT_TRADED_COUNT; ++slot)
    {
        Item* item = data->GetItem((TradeSlots)slot);
        if (!item)
            continue;

        ItemTemplate const* proto = item->GetTemplate();
        if (!proto)
            continue;

        if (proto->GetQuality() < ITEM_QUALITY_NORMAL)
            continue; // one grey item must not zero the entire basket's price

        if (sell)
        {
            sum += double(item->GetCount()) * auctionbot.GetSellPrice(proto) * sRandomPlayerbotMgr.GetSellMultiplier(bot);
        }
        else
        {
            sum += double(item->GetCount()) * auctionbot.GetBuyPrice(proto) * sRandomPlayerbotMgr.GetBuyMultiplier(bot);
        }
    }

    return std::isfinite(sum) && sum > 0.0 ? int32(std::min(sum, double(std::numeric_limits<int32>::max()))) : 0;
}
