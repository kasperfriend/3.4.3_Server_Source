/*
 * Playerbot plugin <-> core glue (ported from ike3/mangosbot).
 *
 * The core exposes a small set of function pointers (see
 * src/server/game/AI/Playerbot/PlayerbotHooks.h) which are filled in here.
 * Nothing in the core links against this library directly, so the bot system
 * can always be compiled out / disabled at runtime.
 */

#include "../pchdef.h"
#include "playerbot.h"
#include "PlayerbotAI.h"
#include "PlayerbotAIConfig.h"
#include "PlayerbotMgr.h"
#include "PlayerbotCommandServer.h"
#include "RandomPlayerbotMgr.h"
#include "Playerbot/PlayerbotHooks.h"
#include "Entities/Player/Player.h"
#include "Server/WorldSession.h"

namespace
{
    void PlayerbotWorldUpdate(uint32 diff)
    {
        sRandomPlayerbotMgr.UpdateAI(diff);
        sRandomPlayerbotMgr.UpdateSessions(diff);
    }

    void PlayerbotPlayerUpdate(Player* player, uint32 diff)
    {
        if (PlayerbotAI* ai = player->GetPlayerbotAI())
            ai->UpdateAI(diff);

        if (PlayerbotMgr* mgr = player->GetPlayerbotMgr())
        {
            mgr->UpdateAI(diff);
            mgr->UpdateSessions(diff);
        }
    }

    void PlayerbotPlayerLogin(Player* player)
    {
        if (!player)
            return;

        // real players get a manager so they can control their own bots
        if (!player->GetPlayerbotAI() && !player->GetPlayerbotMgr())
            player->SetPlayerbotMgr(new PlayerbotMgr(player));

        sRandomPlayerbotMgr.OnPlayerLogin(player);
    }

    void PlayerbotPlayerLogout(Player* player)
    {
        if (!player)
            return;

        if (PlayerbotMgr* mgr = player->GetPlayerbotMgr())
            mgr->LogoutAllBots();

        sRandomPlayerbotMgr.OnPlayerLogout(player);
    }

    void PlayerbotPlayerDelete(Player* player)
    {
        if (!player)
            return;

        if (PlayerbotAI* ai = player->GetPlayerbotAI())
        {
            player->SetPlayerbotAI(nullptr);
            delete ai;
        }

        if (PlayerbotMgr* mgr = player->GetPlayerbotMgr())
        {
            player->SetPlayerbotMgr(nullptr);
            delete mgr;
        }
    }

    void PlayerbotBotPacketSent(Player* bot, WorldPacket const* packet)
    {
        if (!bot || !packet)
            return;

        if (PlayerbotAI* ai = bot->GetPlayerbotAI())
            ai->HandleBotOutgoingPacket(*packet);
    }

    void PlayerbotChat(Player* sender, uint32 type, uint32 /*lang*/, std::string const& msg, Player* receiver)
    {
        if (!sender)
            return;

        // a whisper to one of our bots
        if (receiver)
        {
            if (PlayerbotAI* ai = receiver->GetPlayerbotAI())
            {
                ai->HandleCommand(type, msg, *sender);
                return;
            }
        }

        // party/raid chat is broadcast to every bot the sender owns
        if (PlayerbotMgr* mgr = sender->GetPlayerbotMgr())
            mgr->HandleCommand(type, msg);

        sRandomPlayerbotMgr.HandleCommand(type, msg, *sender);
    }
}

namespace Playerbot
{
    /// Called once from the worldserver right after the world was loaded.
    void InitializePlayerbots()
    {
        if (!sPlayerbotAIConfig.Initialize())
        {
            TC_LOG_INFO("playerbot", "Playerbots are disabled");
            return;
        }

        Hooks& hooks = GetHooks();
        hooks.OnWorldUpdate = &PlayerbotWorldUpdate;
        hooks.OnPlayerUpdate = &PlayerbotPlayerUpdate;
        hooks.OnPlayerLogin = &PlayerbotPlayerLogin;
        hooks.OnPlayerLogout = &PlayerbotPlayerLogout;
        hooks.OnBotPacketSent = &PlayerbotBotPacketSent;
        hooks.OnPlayerChat = &PlayerbotChat;
        hooks.OnPlayerDelete = &PlayerbotPlayerDelete;
        SetEnabled(true);

        // touch the random bot manager so its bots are scheduled from now on
        sRandomPlayerbotMgr.UpdateAI(0);

        if (sPlayerbotAIConfig.commandServerPort)
            PlayerbotCommandServer::instance().Start();

        TC_LOG_INFO("playerbot", "Playerbots initialized");
    }

    void ShutdownPlayerbots()
    {
        if (!IsEnabled())
            return;

        SetEnabled(false);
        sRandomPlayerbotMgr.LogoutAllBots();
    }
}
