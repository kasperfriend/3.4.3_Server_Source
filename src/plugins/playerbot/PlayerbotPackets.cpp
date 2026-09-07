#include "PlayerbotPackets.h"
#include "Log.h"

namespace ai::packets
{
    namespace
    {
        template<class Value, class Reader>
        bool Decode(WorldPacket const& source, Value& value, Reader&& reader)
        {
            WorldPacket data(source);
            data.rpos(0);
            data.ResetBitPos();
            try
            {
                Value decoded;
                reader(data, decoded);
                if (data.rpos() != data.size())
                    throw ByteBufferInvalidValueException("packet", "unexpected trailing bytes");
                value = decoded; // no partially decoded result escapes on failure
                return true;
            }
            catch (ByteBufferException const& error)
            {
                TC_LOG_WARN("playerbot", "Ignoring malformed bot packet opcode {} ({} bytes): {}", source.GetOpcode(), source.size(), error.what());
                return false;
            }
        }

        void RequireEntries(ByteBuffer const& data, uint32 count, uint32 minimumBytes)
        {
            if (data.rpos() > data.size() || count > (data.size() - data.rpos()) / minimumBytes)
                throw ByteBufferPositionException(data.rpos(), count, data.size());
        }
    }

    bool ReadCastFailure(WorldPacket const& source, CastFailure& result)
    {
        uint32 opcode = source.GetOpcode();
        if (opcode != SMSG_CAST_FAILED && opcode != SMSG_SPELL_FAILURE && opcode != SMSG_SPELL_FAILED_OTHER)
            return false;
        return Decode(source, result, [opcode](WorldPacket& data, CastFailure& value)
        {
            if (opcode != SMSG_CAST_FAILED)
                data >> value.Caster;
            data >> value.CastID >> value.SpellID >> value.Visual;
            if (opcode == SMSG_CAST_FAILED)
                data >> value.Reason >> value.FailedArg1 >> value.FailedArg2;
            else if (opcode == SMSG_SPELL_FAILURE)
                value.Reason = data.read<uint16>();
            else
                value.Reason = data.read<uint8>();
        });
    }

    bool ReadReadyCheck(WorldPacket const& source, ReadyCheck& result)
    {
        if (source.GetOpcode() != SMSG_READY_CHECK_STARTED && source.GetOpcode() != SMSG_READY_CHECK_COMPLETED)
            return false;
        return Decode(source, result, [&source](WorldPacket& data, ReadyCheck& value)
        {
            data >> value.PartyIndex >> value.Party;
            if (source.GetOpcode() == SMSG_READY_CHECK_STARTED)
                data >> value.Initiator >> value.Duration;
        });
    }

    bool ReadLootResponse(WorldPacket const& source, LootResponse& result)
    {
        if (source.GetOpcode() != SMSG_LOOT_RESPONSE)
            return false;
        return Decode(source, result, [](WorldPacket& data, LootResponse& value)
        {
            data >> value.Owner >> value.Loot >> value.Failure >> value.Type;
            data.read_skip<uint8>(); // LootMethod
            data.read_skip<uint8>(); // Threshold
            data >> value.Gold;
            uint32 items = data.read<uint32>(), currencies = data.read<uint32>();
            value.Acquired = data.ReadBit();
            data.ReadBit(); // AELooting
            data.ReadBit(); // PersonalLooting
            data.ResetBitPos();
            if (items > 255 || currencies > 255)
                throw ByteBufferInvalidValueException("loot count", "exceeds one-byte slot IDs");
            RequireEntries(data, items, 21); // minimum item including empty bonus/modifier lists
            value.Items.resize(items);
            for (LootItem& item : value.Items)
            {
                data.ReadBits(2); // Type
                item.UIType = uint8(data.ReadBits(3));
                data.ReadBit(); // CanTradeToTapList
                data.ResetBitPos();
                data >> item.Item >> item.Quantity >> item.ItemType >> item.ListID;
                if (!item.ListID)
                    throw ByteBufferInvalidValueException("loot slot", "zero is not a one-based slot");
            }
            RequireEntries(data, currencies, 10);
            value.Currencies.resize(currencies);
            for (LootCurrency& currency : value.Currencies)
            {
                data >> currency.CurrencyID >> currency.Quantity >> currency.ListID;
                currency.UIType = uint8(data.ReadBits(3));
                data.ResetBitPos();
            }
        });
    }

    bool ReadItemPush(WorldPacket const& source, ItemPush& result)
    {
        if (source.GetOpcode() != SMSG_ITEM_PUSH_RESULT)
            return false;
        return Decode(source, result, [](WorldPacket& data, ItemPush& value)
        {
            data >> value.Player;
            data.read_skip<uint8>(); // Slot
            data.read_skip<int32>(); // SlotInBag
            data >> value.QuestLogItemID >> value.Quantity;
            data.read_skip(6 * sizeof(int32)); // inventory/encounter and four battle-pet fields
            data >> value.ItemGUID;
            value.Pushed = data.ReadBit();
            value.Created = data.ReadBit();
            data.ReadBit(); // Unused_1017
            data.ReadBits(3); // DisplayText
            data.ReadBit(); // IsBonusRoll
            data.ReadBit(); // IsEncounterLoot
            data.ResetBitPos();
            data >> value.Item; // shared ItemInstance decoder, including all optional data
        });
    }

    bool ReadLfgProposal(WorldPacket const& source, LfgProposal& result)
    {
        if (source.GetOpcode() != SMSG_LFG_PROPOSAL_UPDATE)
            return false;
        return Decode(source, result, [](WorldPacket& data, LfgProposal& value)
        {
            data >> value.Ticket >> value.InstanceID >> value.ProposalID;
            data.read_skip<uint32>(); // Slot
            data.read_skip<int8>(); // State
            data.read_skip<uint32>(); // CompletedMask
            data.read_skip<uint32>(); // EncounterMask
            uint32 players = data.read<uint32>();
            data.read_skip<uint8>(); // Unused
            data.ReadBit(); // ValidCompletedMask
            data.ReadBit(); // ProposalSilent
            data.ReadBit(); // IsRequeue
            data.ResetBitPos();
            RequireEntries(data, players, 2); // uint8 roles + one flags byte per player
            data.read_skip(size_t(players) * 2);
        });
    }

    WorldPacket LootMoneyRequest()
    {
        WorldPacket packet(CMSG_LOOT_MONEY, 1);
        packet.WriteBit(false); // IsSoftInteract
        packet.FlushBits();
        return packet;
    }

    WorldPacket LootItemRequest(ObjectGuid const& lootObject, uint8 listId)
    {
        WorldPacket packet(CMSG_LOOT_ITEM, 24);
        packet << uint32(1) << lootObject << listId;
        packet.WriteBit(false); // IsSoftInteract
        packet.FlushBits();
        return packet;
    }

    WorldPacket LootReleaseRequest(ObjectGuid const& owner)
    {
        WorldPacket packet(CMSG_LOOT_RELEASE, 18);
        packet << owner;
        return packet;
    }
}
