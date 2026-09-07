#pragma once

#include "WorldPacket.h"
#include "ObjectGuid.h"
#include "ItemPacketsCommon.h"
#include "CombatLogPacketsCommon.h"
#include "LFGPacketsCommon.h"

// Decoders for server packets consumed by bots. Layouts come from this core's
// Write() functions; GUIDs and embedded structures use their shared codecs.
namespace ai::packets
{
    struct CastFailure
    {
        ObjectGuid Caster, CastID;
        int32 SpellID = 0, Reason = 0, FailedArg1 = 0, FailedArg2 = 0;
        WorldPackets::Spells::SpellCastVisual Visual;
    };
    struct ReadyCheck
    {
        int8 PartyIndex = 0;
        ObjectGuid Party, Initiator;
        WorldPackets::Duration<Milliseconds> Duration;
    };
    struct LootItem
    {
        WorldPackets::Item::ItemInstance Item;
        uint32 Quantity = 0;
        uint8 UIType = 0, ItemType = 0, ListID = 0;
    };
    struct LootCurrency
    {
        uint32 CurrencyID = 0, Quantity = 0;
        uint8 ListID = 0, UIType = 0;
    };
    struct LootResponse
    {
        ObjectGuid Owner, Loot;
        uint8 Failure = 0, Type = 0;
        uint32 Gold = 0;
        bool Acquired = false;
        std::vector<LootItem> Items;
        std::vector<LootCurrency> Currencies;
    };
    struct ItemPush
    {
        ObjectGuid Player, ItemGUID;
        int32 QuestLogItemID = 0, Quantity = 0;
        bool Pushed = false, Created = false;
        WorldPackets::Item::ItemInstance Item;
    };
    struct LfgProposal
    {
        WorldPackets::LFG::RideTicket Ticket;
        uint64 InstanceID = 0;
        uint32 ProposalID = 0;
    };

    bool ReadCastFailure(WorldPacket const& source, CastFailure& result);
    bool ReadReadyCheck(WorldPacket const& source, ReadyCheck& result);
    bool ReadLootResponse(WorldPacket const& source, LootResponse& result);
    bool ReadItemPush(WorldPacket const& source, ItemPush& result);
    bool ReadLfgProposal(WorldPacket const& source, LfgProposal& result);
    WorldPacket LootMoneyRequest();
    WorldPacket LootItemRequest(ObjectGuid const& lootObject, uint8 listId);
    WorldPacket LootReleaseRequest(ObjectGuid const& owner);
}
