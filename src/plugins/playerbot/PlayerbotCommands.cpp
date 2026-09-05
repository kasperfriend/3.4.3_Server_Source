/*
 * Playerbot chat commands (ported from ike3/mangosbot).
 *
 * Registered from Playerbot::RegisterPlayerbotScripts() which the worldserver
 * hands to ScriptMgr together with the regular script loader.
 */

#include "../pchdef.h"
#include "playerbot.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "Chat/Chat.h"
#include "Chat/ChatCommand.h"
#include "Scripting/ScriptMgr.h"

using namespace Trinity::ChatCommands;

class playerbot_commandscript : public CommandScript
{
public:
    playerbot_commandscript() : CommandScript("playerbot_commandscript") { }

    ChatCommandTable GetCommands() const override
    {
        static ChatCommandTable playerbotCommandTable =
        {
            { "bot",       HandleBotCommand,       rbac::RBAC_PERM_COMMAND_GM,      Console::No  },
            { "playerbot", HandleBotCommand,       rbac::RBAC_PERM_COMMAND_GM,      Console::No  },
            { "rndbot",    HandleRandomBotCommand, rbac::RBAC_PERM_COMMAND_GM,      Console::Yes },
        };

        return playerbotCommandTable;
    }

    static bool HandleBotCommand(ChatHandler* handler, Tail args)
    {
        return PlayerbotMgr::HandlePlayerbotMgrCommand(handler, std::string(args).c_str());
    }

    static bool HandleRandomBotCommand(ChatHandler* handler, Tail args)
    {
        return RandomPlayerbotMgr::HandlePlayerbotConsoleCommand(handler, std::string(args).c_str());
    }
};

void AddSC_playerbot_commandscript()
{
    new playerbot_commandscript();
}

namespace Playerbot
{
    /// hooked into the worldserver script loader
    void RegisterPlayerbotScripts()
    {
        AddSC_playerbot_commandscript();
    }
}
