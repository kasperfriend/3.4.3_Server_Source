#include "Bag.h"
#include "BattlePayPackets.h"
#include "BattlePayMgr.h"
#include "BattlePayData.h"
#include "Config.h"
#include "Containers.h"
#include "DatabaseEnv.h"
#include "ObjectMgr.h"
#include "ScriptMgr.h"
#include "Random.h"
#include <sstream>

namespace
{
    uint32 GetBagsFreeSlots(Player* player)
    {
        uint32 freeBagSlots = 0;
        for (uint8 i = INVENTORY_SLOT_BAG_START; i < INVENTORY_SLOT_BAG_END; i++)
            if (auto bag = player->GetBagByPos(i))
                freeBagSlots += bag->GetFreeSlots();

        uint8 inventoryEnd = INVENTORY_SLOT_ITEM_START + player->GetInventorySlotCount();
        for (uint8 i = INVENTORY_SLOT_ITEM_START; i < inventoryEnd; i++)
            if (!player->GetItemByPos(INVENTORY_SLOT_BAG_0, i))
                ++freeBagSlots;

        return freeBagSlots;
    }
}

void WorldSession::SendStartPurchaseResponse(WorldSession* session, Battlepay::Purchase const& purchase, Battlepay::Error const& result)
{
    WorldPackets::BattlePay::StartPurchaseResponse response;
    response.PurchaseID = purchase.PurchaseID;
    response.ClientToken = purchase.ClientToken;
    response.PurchaseResult = result;
    session->SendPacket(response.Write());
};

void WorldSession::SendPurchaseUpdate(WorldSession* session, Battlepay::Purchase const& purchase, uint32 result)
{
    WorldPackets::BattlePay::PurchaseUpdate packet;
    WorldPackets::BattlePay::Purchase data;
    data.PurchaseID = purchase.PurchaseID;
    data.UnkLong = 0;
    data.UnkLong2 = 0;
    data.Status = purchase.Status;
    data.ResultCode = result;
    data.ProductID = purchase.ProductID;
    data.UnkInt = purchase.ServerToken;
    data.WalletName = session->GetBattlePayMgr()->GetDefaultWalletName();
    packet.PurchaseData.emplace_back(data);
    session->SendPacket(packet.Write());
};

void WorldSession::HandleGetPurchaseListQuery(WorldPackets::BattlePay::GetPurchaseListQuery& /*packet*/)
{
    WorldPackets::BattlePay::PurchaseListResponse packet; // @TODO
    SendPacket(packet.Write());
}

void WorldSession::HandleUpdateVasPurchaseStates(WorldPackets::BattlePay::UpdateVasPurchaseStates& /*packet*/)
{
    WorldPackets::BattlePay::EnumVasPurchaseStatesResponse response;
    response.Result = 0;
    SendPacket(response.Write());
}

void WorldSession::HandleBattlePayDistributionAssign(WorldPackets::BattlePay::DistributionAssignToTarget& packet)
{
    if (!GetBattlePayMgr()->IsAvailable())
        return;

    GetBattlePayMgr()->AssignDistributionToCharacter(packet.TargetCharacter, packet.DistributionID, packet.ProductID, packet.SpecializationID, packet.ChoiceID);
}

void WorldSession::HandleGetProductList(WorldPackets::BattlePay::GetProductList& /*packet*/)
{
    if (!GetBattlePayMgr()->IsAvailable())
        return;

    GetBattlePayMgr()->SendProductList();
    GetBattlePayMgr()->SendAccountCredits();
}

void WorldSession::SendMakePurchase(ObjectGuid targetCharacter, uint32 clientToken, uint32 productID, WorldSession* session)
{
    if (!session || !session->GetBattlePayMgr()->IsAvailable())
        return;

    auto* mgr = session->GetBattlePayMgr();

    if (mgr == nullptr)
        return;

    auto player = session->GetPlayer();

    Battlepay::Purchase purchase;
    purchase.ProductID = productID;
    purchase.ClientToken = clientToken;
    purchase.TargetCharacter = targetCharacter;
    purchase.Status = Battlepay::UpdateStatus::Loading;
    purchase.DistributionId = mgr->GenerateNewDistributionId();

    auto getProductInfo = sBattlePayDataStore->GetProductInfoForProduct(productID);
    if (getProductInfo == nullptr)
        return;

    BattlePayData::ProductInfo productInfo = *getProductInfo;

    purchase.CurrentPrice = uint64(productInfo.CurrentPriceFixedPoint);

    mgr->RegisterStartPurchase(purchase);

    auto accountCredits = GetBattlePayMgr()->GetBattlePayCredits();
    auto purchaseData = mgr->GetPurchase();

    if (accountCredits < static_cast<uint64>(purchaseData->CurrentPrice))
    {
        SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::InsufficientBalance);
        return;
    }

    for (uint32 productId : productInfo.ProductIds)
    {
        if (sBattlePayDataStore->ProductExist(productId))
        {
            BattlePayData::Product product = *sBattlePayDataStore->GetProduct(productId);

            // if buy is disabled in product addons
            auto productAddon = sBattlePayDataStore->GetProductAddon(productInfo.Entry);
            if (productAddon)
                if (productAddon->DisableBuy > 0)
                    SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::PurchaseDenied);

            if (!product.Items.empty())
            {
                if (player)
                {
                    if (product.Items.size() > GetBagsFreeSlots(player))
                    {
                        SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::PurchaseDenied);
                        return;
                    }
                }
            }
        }
        else
        {
            SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::PurchaseDenied);
            return;
        }
    }

    purchaseData->PurchaseID = sBattlePayDataStore->GenerateNewPurchaseID();
    purchaseData->ServerToken = urand(0, 0xFFFFFFF);

    SendStartPurchaseResponse(session, *purchaseData, Battlepay::Error::Ok);
    SendPurchaseUpdate(session, *purchaseData, Battlepay::Error::Ok);

    WorldPackets::BattlePay::ConfirmPurchase confirmPurchase;
    confirmPurchase.PurchaseID = purchaseData->PurchaseID;
    confirmPurchase.ServerToken = purchaseData->ServerToken;
    session->SendPacket(confirmPurchase.Write());
};

void WorldSession::HandleBattlePayStartPurchase(WorldPackets::BattlePay::StartPurchase& packet)
{
    SendMakePurchase(packet.TargetCharacter, packet.ClientToken, packet.ProductID, this);
}

void WorldSession::HandleBattlePayConfirmPurchase(WorldPackets::BattlePay::ConfirmPurchaseResponse& packet)
{
    if (!GetBattlePayMgr()->IsAvailable())
        return;

    auto purchase = GetBattlePayMgr()->GetPurchase();
    if (!purchase)
        return;

    //const auto& productInfo = *sBattlePayDataStore->GetProductInfoForProduct(purchase->ProductID);
    //const auto& displayInfo = *sBattlePayDataStore->GetDisplayInfo(productInfo.Entry);

    if (purchase->Lock)
    {
        SendPurchaseUpdate(this, *purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    if (purchase->ServerToken != packet.ServerToken || !packet.ConfirmPurchase || purchase->CurrentPrice != packet.ClientCurrentPriceFixedPoint)
    {
        SendPurchaseUpdate(this, *purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    auto accountBalance = GetBattlePayMgr()->GetBattlePayCredits();
    if (accountBalance < static_cast<uint64>(purchase->CurrentPrice))
    {
        SendPurchaseUpdate(this, *purchase, Battlepay::Error::PurchaseDenied);
        return;
    }

    purchase->Lock = true;
    purchase->Status = Battlepay::UpdateStatus::Finish;

    SendPurchaseUpdate(this, *purchase, Battlepay::Error::Other);
    GetBattlePayMgr()->UpdateBattlePayCredits(purchase->CurrentPrice);
    GetBattlePayMgr()->ProcessDelivery(*purchase, false);
    GetBattlePayMgr()->SendProductList();
}

void WorldSession::HandleBattlePayAckFailedResponse(WorldPackets::BattlePay::BattlePayAckFailedResponse& /*packet*/)
{
}

void WorldSession::HandleBattlePayRequestPriceInfo(WorldPackets::BattlePay::BattlePayRequestPriceInfo& /*packet*/)
{
}

void WorldSession::SendDisplayPromo(int32 promotionID /*= 0*/)
{
    SendPacket(WorldPackets::BattlePay::DisplayPromotion(promotionID).Write());
}

void WorldSession::SendSyncWowEntitlements()
{
    WorldPackets::BattlePay::SyncWowEntitlements packet;
    SendPacket(packet.Write());
}
