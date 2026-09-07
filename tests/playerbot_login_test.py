"""Fresh connection/login/lifetime regressions.

Production methods and validation blocks run against small deterministic socket,
DB-result and game-state fakes. This does not replace a Windows/live-realm test.
"""

from pathlib import Path
import unittest

from playerbot_logic_test import ROOT, run_cpp, source_slice
from playerbot_safety_test import function
from playerbot_packet_test import type_definition


SOCKET = "src/server/game/Server/WorldSocket.cpp"
PLAYER = "src/server/game/Entities/Player/Player.cpp"
HOLDER = "src/plugins/playerbot/PlayerbotMgr.cpp"


def fill(name, blocks):
    text = Path(__file__).with_name(name).read_text()
    for marker, code in blocks.items():
        text = text.replace(f"@{marker}@", code)
    return text


class PlayerbotLoginTest(unittest.TestCase):
    def test_world_connection_framing_auth_and_disconnect(self):
        blocks = {
            "HEADER": type_definition("src/server/game/Server/WorldSocket.h", "PacketHeader"),
            "INCOMING_HEADER": type_definition("src/server/game/Server/WorldSocket.h", "IncomingPacketHeader"),
            "COMPRESSED_HEADER": type_definition(SOCKET, "CompressedWorldPacket"),
            "AUTH_TIMEOUT": function("src/server/game/Server/WorldSocket.h", "bool IsAuthenticationTimedOut(TimePoint now) const"),
            "AUTH_PHASE": function("src/server/game/Server/WorldSocket.h", "enum class AuthPhase") + ";",
            "WORLD_READ_HEADER": function(SOCKET, "bool WorldSocket::ReadHeaderHandler()"),
            "WORLD_READ": function(SOCKET, "void WorldSocket::ReadHandler()"),
            "ENCRYPTED_ACK": function(SOCKET, "bool WorldSocket::HandleEnterEncryptedModeAck()"),
            "PERMISSIONS_CALLBACK": function(SOCKET, "void WorldSocket::LoadSessionPermissionsCallback("),
            "ON_CLOSE": function(SOCKET, "void WorldSocket::OnClose()"),
            "WORLD_WRITE": function(SOCKET, "void WorldSocket::WritePacketToBuffer("),
            "COMPRESSED_SIZE": source_slice(SOCKET, "        uint32 packetSize = queued->size()", "        // Flush current buffer"),
        }
        run_cpp(self, fill("login_network_test.cpp.in", blocks), includes=["src/common", "src/common/Utilities"], gcc_flags=["-pthread"])

    def test_saved_character_state_bounds(self):
        blocks = {
            "STATE_LEVEL": source_slice(PLAYER, "    uint32 loadedLevel =", "    SetXP(fields.xp);"),
            "STATE_BAGS": source_slice(PLAYER, "    uint8 inventorySlots =", "    SetNativeGender(fields.gender);"),
            "STATE_MODE": source_slice(PLAYER, "    m_createMode = fields.createMode;", "    m_cinematic ="),
            "STATE_GROUPS": source_slice(PLAYER, "    uint8 bonusTalentGroups =", "    uint32 lootSpecId ="),
            "ADD_TALENT": function(PLAYER, "bool Player::AddTalent("),
            "LOAD_TALENTS": function(PLAYER, "void Player::_LoadTalents("),
            "LOAD_GLYPHS": function(PLAYER, "void Player::_LoadGlyphs("),
            "BONUS_GROUPS": function(PLAYER, "void Player::SetBonusTalentGroupCount("),
            "CORPSE": function(PLAYER, "void Player::LoadCorpse("),
            "LOGIN_ENTRY": source_slice("src/server/game/Handlers/CharacterHandler.cpp", "void WorldSession::HandlePlayerLogin(LoginQueryHolder const& holder)", "    Player* pCurrChar = new Player(this);"),
            "RAW_GUID": function("src/server/game/Entities/Object/ObjectGuid.cpp", "bool ObjectGuid::TrySetRawValue("),
        }
        run_cpp(self, fill("login_player_data_test.cpp.in", blocks), standard=20,
                includes=["src/common", "src/common/Utilities", "src/server/game/Entities/Object"],
                gcc_flags=["-fno-sanitize=alignment"])

    def test_bot_login_ownership_and_owner_lifecycle(self):
        blocks = {
            "UPDATE_SESSIONS": function(HOLDER, "void PlayerbotHolder::UpdateSessions("),
            "ADD_BOT": function(HOLDER, "void PlayerbotHolder::AddPlayerBot("),
            "LOGOUT_ALL": function(HOLDER, "void PlayerbotHolder::LogoutAllBots()"),
            "LOGOUT_BOT": function(HOLDER, "void PlayerbotHolder::LogoutPlayerBot("),
            "GET_BOT": function(HOLDER, "Player* PlayerbotHolder::GetPlayerBot("),
            "PLAYER_SESSIONS": function("src/plugins/playerbot/RandomPlayerbotMgr.cpp", "void RandomPlayerbotMgr::UpdatePlayerbotSessions("),
            "PLAYER_SHUTDOWN": function("src/plugins/playerbot/RandomPlayerbotMgr.cpp", "void RandomPlayerbotMgr::ShutdownPlayerbotSessions()"),
            "RANDOM_PLAYER": function("src/plugins/playerbot/RandomPlayerbotMgr.cpp", "Player* RandomPlayerbotMgr::GetRandomPlayer()"),
            "COMMAND_PARSE": source_slice(HOLDER, "    std::istringstream input(args ? args", "    set<string> bots;"),
        }
        run_cpp(self, fill("login_bot_lifetime_test.cpp.in", blocks))

    def test_bnet_fragmentation_and_rpc_size_limits(self):
        path = "src/server/bnetserver/Server/Session.cpp"
        blocks = {
            "PARTIAL": source_slice(path, "template<bool(Battlenet::Session::*processMethod)()", "void Battlenet::Session::ReadHandler()"),
            "READ": function(path, "void Battlenet::Session::ReadHandler()"),
            "LENGTH": function(path, "bool Battlenet::Session::ReadHeaderLengthHandler()"),
            "HEADER": function(path, "bool Battlenet::Session::ReadHeaderHandler()"),
            "BODY": function(path, "bool Battlenet::Session::ReadDataHandler()"),
        }
        run_cpp(self, fill("login_bnet_test.cpp.in", blocks), includes=["src/common", "src/common/Utilities"])

    def test_login_spell_validation_graphs(self):
        blocks = { "VALIDATION": function("src/server/game/Spells/SpellMgr.cpp", "bool SpellMgr::IsSpellValid(") }
        run_cpp(self, fill("login_spell_validation_test.cpp.in", blocks), includes=["src/common", "src/common/Utilities", "dep/fmt/include"], defines=["FMT_HEADER_ONLY"])

    def test_login_lifecycle_guards_are_wired_before_side_effects(self):
        login = function("src/server/game/Handlers/CharacterHandler.cpp", "void WorldSession::HandlePlayerLogin(")
        self.assertLess(login.index("m_playerLoading != playerGuid"), login.index("new Player(this)"))
        self.assertLess(login.index("FindConnectedPlayer(playerGuid)"), login.index("new Player(this)"))
        self.assertLess(login.index("CleanupsBeforeDelete"), login.index("delete pCurrChar"))
        destructor = function(PLAYER, "Player::~Player()")
        self.assertLess(destructor.index("Playerbot::OnPlayerDelete"), destructor.index("delete m_items"))
        hook = function("src/plugins/playerbot/PlayerbotHookImpl.cpp", "    void PlayerbotPlayerUpdate(")
        self.assertNotIn("UpdateSessions", hook)
        update = function("src/plugins/playerbot/PlayerbotHookImpl.cpp", "    void PlayerbotWorldUpdate(")
        self.assertIn("UpdatePlayerbotSessions(diff)", update)
        shutdown = function("src/plugins/playerbot/PlayerbotHookImpl.cpp", "    void ShutdownPlayerbots()")
        self.assertLess(shutdown.index("ShutdownPlayerbotSessions"), shutdown.index("SetEnabled(false)"))
        main = (ROOT / "src/server/worldserver/Main.cpp").read_text(encoding="utf-8")
        # Reverse shared_ptr destruction: KickAll, then bots, then UnloadAll.
        self.assertLess(main.index("std::shared_ptr<void> mapManagementHandle"),
                        main.index("std::shared_ptr<void> sPlayerbotHandle"))
        kick = function("src/server/worldserver/Main.cpp", "    std::shared_ptr<void> sWorldSocketMgrHandle")
        self.assertLess(kick.index("KickAll"), kick.index("ShutdownPlayerbots"))
        self.assertLess(kick.index("ShutdownPlayerbots"), kick.index("StopNetwork"))
        load = function("src/server/worldserver/Main.cpp", "static void LoadAllScripts()")
        self.assertLess(load.index('GetBoolDefault("AiPlayerbot.Enabled"'), load.index("RegisterPlayerbotScripts"))
        initialize = function("src/plugins/playerbot/PlayerbotHookImpl.cpp", "    void InitializePlayerbots()")
        self.assertLess(initialize.index("if (!sPlayerbotAIConfig.Initialize())"), initialize.index("SetEnabled(true)"))
        load_skills = function(PLAYER, "void Player::_LoadSkills(")
        self.assertLess(load_skills.index("IsBotSession"), load_skills.index("SKILL_DELETED"))
        more = load_skills.index("has more than")
        self.assertLess(load_skills.rfind("IsBotSession", 0, more), more)
        extra = source_slice(PLAYER, "        if (!skillSlot)", "        if (skillEntry->ParentSkillLineID)")
        self.assertIn("IsBotSession", extra)
        self.assertIn("cannot have additional skills", extra)
        explore = function(PLAYER, "void Player::CheckAreaExplore()")
        self.assertIn("IsBotSession", explore)
        self.assertIn("discovered unknown area", explore)
        self.assertLess(explore.index("IsBotSession"), explore.index('TC_LOG_ERROR("entities.player", "Player'))
        for signature in ("void WorldSocket::HandleAuthSessionCallback(", "void WorldSocket::HandleAuthContinuedSessionCallback("):
            auth = function(SOCKET, signature)
            self.assertIn("!IsOpen()", auth)
            self.assertIn("GetBinary().size()", auth)
        self.assertNotIn("strtok", function(HOLDER, "list<string> PlayerbotHolder::HandlePlayerbotCommand("))
        pet = function("src/server/game/Entities/Pet/Pet.cpp", "bool Pet::LoadPetFromDB(")
        self.assertLess(pet.index("std::clamp<uint8>(petInfo->Level"), pet.index("InitStatsForLevel(petlevel)"))
        reputation = function("src/server/game/Reputation/ReputationMgr.cpp", "bool ReputationMgr::SetOneFactionReputation(")
        self.assertLess(reputation.index("if (!factionEntry)"), reputation.index("factionEntry->ReputationIndex"))


if __name__ == "__main__":
    unittest.main()
