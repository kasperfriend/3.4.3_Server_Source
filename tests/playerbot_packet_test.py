"""Byte-level round trips using the real ByteBuffer, GUID and packet codecs.

Only logging/assert sinks and unrelated Util declarations are shimmed. Large
packet translation units contribute their production types and codec functions
verbatim; no hand-reimplemented GUID/bit-buffer codec is used in the tests.
"""

from pathlib import Path
import re
import unittest

from playerbot_logic_test import ROOT, run_cpp, source_slice
from playerbot_safety_test import function


PACKETS = "src/server/game/Server/Packets/"
INCLUDES = ["src/common", "src/common/Utilities", "src/server/shared/Packets", "src/server/game/Server",
            "src/server/game/Server/Protocol", "src/server/game/Server/Packets", "src/server/game/Entities/Object",
            "src/server/game/Entities/Item", "src/plugins/playerbot", "dep/fmt/include", "dep/utf8cpp", "dep/short_alloc"]
HEADERS = {
    "Errors.h": '''#pragma once
#include <stdexcept>
#define ASSERT(condition, ...) do { if (!(condition)) throw std::runtime_error("core assertion: " #condition); } while(false)
''',
    "Util.h": '#pragma once\n#include "Errors.h"\n',
    "Log.h": '''#pragma once
#include "StringFormat.h"
#include <fmt/format.h>
#include <vector>
struct FakeLog { bool ShouldLog(char const*, int) const { return false; } };
inline FakeLog logStorage;
inline auto sLog = &logStorage;
constexpr int LOG_LEVEL_TRACE = 0;
namespace TestLog { inline std::vector<std::string> messages; }
#define TC_LOG_TRACE(category, ...) TestLog::messages.push_back(Trinity::StringFormat(__VA_ARGS__))
#define TC_LOG_WARN(category, ...) TestLog::messages.push_back(Trinity::StringFormat(__VA_ARGS__))
#define TC_LOG_ERROR(category, ...) TestLog::messages.push_back(Trinity::StringFormat(__VA_ARGS__))
''',
}


def type_definition(path, name):
    source = (ROOT / path).read_text()
    match = re.search(r"\b(?:class|struct)\s+(?:TC_GAME_API\s+)?" + re.escape(name) + r"\b[^;{]*\{", source)
    assert match, name
    return function(path, source[match.start():match.end() - 1]) + ";\n"


def codecs():
    code = source_slice("src/server/game/Entities/Object/ObjectGuid.cpp",
                        "ByteBuffer& operator<<(ByteBuffer& buf, ObjectGuid const& guid)",
                        "ObjectGuid::LowType ObjectGuidGenerator::Generate()")
    code += "\nObjectGuid const ObjectGuid::Empty{};\n"
    # Constructor initializer contains format-string braces before the body.
    text = (ROOT / (PACKETS + "PacketUtilities.cpp")).read_text()
    code += text[text.index("WorldPackets::PacketArrayMaxCapacityException::"):]
    code += "\nnamespace WorldPackets::Item {\n"
    for kind in ("ItemBonuses", "ItemMod", "ItemModList", "ItemInstance"):
        for op, arg in (("<<", kind + " const&"), (">>", kind + "&")):
            code += function(PACKETS + "ItemPacketsCommon.cpp", f"ByteBuffer& operator{op}(ByteBuffer& data, {arg}") + "\n"
    code += "}\nnamespace WorldPackets::Spells {\n"
    for op, arg in (("<<", "SpellCastVisual const&"), (">>", "SpellCastVisual&")):
        code += function(PACKETS + "CombatLogPacketsCommon.cpp", f"ByteBuffer& operator{op}(ByteBuffer& data, {arg}") + "\n"
    code += "}\n"
    for op, arg in (("<<", "WorldPackets::LFG::RideTicket const&"), (">>", "WorldPackets::LFG::RideTicket&")):
        code += function(PACKETS + "LFGPacketsCommon.cpp", f"ByteBuffer& operator{op}(ByteBuffer& data, {arg}") + "\n"
    return code


def packet_types():
    code = ""
    for namespace, stem, names in [
        ("Spells", "SpellPackets", ["CastFailed", "SpellFailure", "SpellFailedOther"]),
        ("Item", "ItemPackets", ["ItemPushResult", "BuyFailed"]),
        ("Party", "PartyPackets", ["ReadyCheckStarted", "ReadyCheckCompleted", "ReadyCheckResponseClient"]),
        ("Movement", "MovementPackets", ["MoveTeleportAck"]),
        ("Quest", "QuestPackets", ["PushQuestToParty", "QuestPushResult"]),
    ]:
        code += f"\nnamespace WorldPackets::{namespace} {{\n"
        for name in names:
            code += type_definition(PACKETS + stem + ".h", name)
        code += "}\n"
        for name in names:
            path = PACKETS + stem + ".cpp"
            source = (ROOT / path).read_text()
            op = "Read" if name in ("ReadyCheckResponseClient", "MoveTeleportAck", "PushQuestToParty", "QuestPushResult") else "Write"
            # These files mix qualified definitions with namespace-level definitions.
            signature = re.search(r"(?:void|WorldPacket const\*) (?:WorldPackets::\w+::)?" + name + "::" + op + r"\(", source).group()
            body = function(path, signature)
            if "WorldPackets::" not in signature:
                body = f"namespace WorldPackets::{namespace} {{\n" + body + "\n}\n"
            code += body + "\n"
    code += "namespace WorldPackets::LFG {\n"
    code += type_definition(PACKETS + "LFGPackets.h", "LFGProposalUpdatePlayer")
    code += type_definition(PACKETS + "LFGPackets.h", "LFGProposalUpdate")
    code += "}\n"
    code += function(PACKETS + "LFGPackets.cpp", "ByteBuffer& operator<<(ByteBuffer& data, WorldPackets::LFG::LFGProposalUpdatePlayer const&") + "\n"
    code += function(PACKETS + "LFGPackets.cpp", "WorldPacket const* WorldPackets::LFG::LFGProposalUpdate::Write()") + "\n"
    return code


class PlayerbotPacketTest(unittest.TestCase):
    def test_real_packet_round_trips_and_truncation(self):
        text = Path(__file__).with_name("packet_wire_test.cpp.in").read_text()
        text = text.replace("@CODECS@", codecs()).replace("@PACKET_TYPES@", packet_types())
        loot_enums = function("src/server/game/Loot/Loot.h", "enum LootSlotType") + ";\n"
        loot_enums += function("src/server/game/Loot/Loot.h", "enum LootType : uint8") + ";\n"
        text = text.replace("@LOOT_ENUMS@", loot_enums)
        text = text.replace("@BUY_FAILED@", function("src/plugins/playerbot/strategy/actions/InventoryChangeFailureAction.cpp", "bool BuyFailedAction::Execute("))
        text = text.replace("@STORE_LOOT@", function("src/plugins/playerbot/strategy/actions/LootAction.cpp", "bool StoreLootAction::Execute("))
        text = text.replace("@READY_CHECK@", function("src/plugins/playerbot/strategy/actions/ReadyCheckAction.cpp", "bool ReadyCheckAction::Execute("))
        text = text.replace("@READY_FINISH@", function("src/plugins/playerbot/strategy/actions/ReadyCheckAction.cpp", "bool FinishReadyCheckAction::Execute("))
        run_cpp(self, text, sources=["src/server/shared/Packets/ByteBuffer.cpp", "src/server/game/Server/Packet.cpp",
                                    PACKETS + "LootPackets.cpp", "src/plugins/playerbot/PlayerbotPackets.cpp"],
                includes=INCLUDES, headers=HEADERS, defines=["FMT_HEADER_ONLY"], standard=20,
                # ObjectGuid is explicitly packed in this core's ABI.
                gcc_flags=["-fno-sanitize=alignment"])

    def test_packet_queue_and_interrupt_state(self):
        text = Path(__file__).with_name("packet_state_test.cpp.in").read_text()
        replacements = {
            "CODECS": codecs(), "PACKET_TYPES": packet_types(),
            "TRADE_COST": function("src/plugins/playerbot/strategy/actions/TradeStatusAction.cpp", "int32 TradeStatusAction::CalculateCost("),
            "EVENT": type_definition("src/plugins/playerbot/strategy/Event.h", "Event"),
            "PACKET_TRIGGER": type_definition("src/plugins/playerbot/strategy/triggers/WorldPacketTrigger.h", "WorldPacketTrigger"),
            "LAST_SPELL": type_definition("src/plugins/playerbot/strategy/values/LastSpellCastValue.h", "LastSpellCast"),
            "PACKET_HELPER": type_definition("src/plugins/playerbot/PlayerbotAI.h", "PacketHandlingHelper"),
        }
        for marker, signature in [
            ("HELPER_HANDLE", "void PacketHandlingHelper::Handle("),
            ("HELPER_ADD_HANDLER", "void PacketHandlingHelper::AddHandler("),
            ("HELPER_ADD_PACKET", "void PacketHandlingHelper::AddPacket("),
            ("SPELL_INTERRUPTED", "void PlayerbotAI::SpellInterrupted("),
            ("INTERRUPT_SPELL", "void PlayerbotAI::InterruptSpell()"),
            ("TELEPORT_ACK", "void PlayerbotAI::HandleTeleportAck()"),
        ]:
            replacements[marker] = function("src/plugins/playerbot/PlayerbotAI.cpp", signature)
        for marker, value in replacements.items():
            text = text.replace(f"@{marker}@", value)
        run_cpp(self, text, sources=["src/server/shared/Packets/ByteBuffer.cpp", "src/server/game/Server/Packet.cpp"],
                includes=INCLUDES, headers=HEADERS, defines=["FMT_HEADER_ONLY"], standard=20,
                gcc_flags=["-fno-sanitize=alignment", "-pthread"])

    def test_packet_routing_and_type_guards(self):
        ai = (ROOT / "src/plugins/playerbot/PlayerbotAI.cpp").read_text()
        self.assertIn("MoveTeleportAck p{WorldPacket(CMSG_MOVE_TELEPORT_ACK)}", ai)
        self.assertNotIn("MoveTeleportAck(WorldPacket())", ai)
        self.assertIn('AddHandler(SMSG_READY_CHECK_STARTED, "ready check")', ai)
        self.assertIn('AddHandler(SMSG_READY_CHECK_COMPLETED, "ready check finished")', ai)
        self.assertNotIn("AddHandler(BUY_ERR_", ai)
        self.assertIn('AddHandler(SMSG_BUY_FAILED, "buy failed")', ai)
        interrupted = function("src/plugins/playerbot/PlayerbotAI.cpp", "void PlayerbotAI::InterruptSpell()")
        self.assertNotIn("WorldPacket", interrupted)
        self.assertNotIn("SendMessageToSet", interrupted)
        share = function("src/plugins/playerbot/strategy/actions/AcceptQuestAction.cpp", "bool AcceptQuestShareAction::Execute(")
        self.assertIn("Quest::PushQuestToParty", share)
        self.assertNotIn("Quest::QuestPushResult", share)
        trade = function("src/plugins/playerbot/strategy/actions/TradeStatusAction.cpp", "bool TradeStatusAction::Execute(")
        self.assertLess(trade.index("acceptTrade.StateIndex ="), trade.index("HandleAcceptTradeOpcode(acceptTrade)"))
        loot = function("src/plugins/playerbot/strategy/actions/LootAction.cpp", "bool StoreLootAction::Execute(")
        self.assertNotIn("SetLootAmount", loot)
        self.assertNotIn("CheckItemTask", loot)


if __name__ == "__main__":
    unittest.main()
