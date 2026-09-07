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
#include <exception>
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
        // A bot AI tick must never be able to kill the whole worldserver: the
        // bot code parses packets and walks containers without full exception
        // safety, and these hooks run from World::Update / Player::Update where
        // the core does not expect exceptions. Contain and log instead.
        try
        {
            sRandomPlayerbotMgr.UpdateAI(diff);
        }
        catch (std::exception const& e)
        {
            TC_LOG_ERROR("playerbot", "Random bot manager update failed: {}", e.what());
        }
        catch (...)
        {
            TC_LOG_ERROR("playerbot", "Random bot manager update failed with an unknown exception");
        }

        try
        {
            sRandomPlayerbotMgr.UpdateSessions(diff);
            sRandomPlayerbotMgr.UpdatePlayerbotSessions(diff);
        }
        catch (std::exception const& e)
        {
            TC_LOG_ERROR("playerbot", "Random bot session update failed: {}", e.what());
        }
        catch (...)
        {
            TC_LOG_ERROR("playerbot", "Random bot session update failed with an unknown exception");
        }
    }

    void PlayerbotPlayerUpdate(Player* player, uint32 diff)
    {
        try
        {
            if (PlayerbotAI* ai = player->GetPlayerbotAI())
                ai->UpdateAI(diff);
        }
        catch (std::exception const& e)
        {
            TC_LOG_ERROR("playerbot", "Player update for {} failed: {}", player->GetName(), e.what());
        }
        catch (...)
        {
            TC_LOG_ERROR("playerbot", "Player update for {} failed with an unknown exception", player->GetName());
        }
    }

    void PlayerbotPlayerLogin(Player* player)
    {
        if (!player || !player->GetSession() || player->GetSession()->IsBotSession())
            return;

        try
        {
            // real players get a manager so they can control their own bots
            if (!player->GetPlayerbotAI() && !player->GetPlayerbotMgr())
                player->SetPlayerbotMgr(new PlayerbotMgr(player));

            sRandomPlayerbotMgr.OnPlayerLogin(player);
        }
        catch (std::exception const& e)
        {
            TC_LOG_ERROR("playerbot", "Player login hook for {} failed: {}", player->GetName(), e.what());
        }
        catch (...)
        {
            TC_LOG_ERROR("playerbot", "Player login hook for {} failed with an unknown exception", player->GetName());
        }
    }

    void PlayerbotPlayerLogout(Player* player)
    {
        if (!player)
            return;

        try
        {
            if (PlayerbotMgr* mgr = player->GetPlayerbotMgr())
                mgr->LogoutAllBots();

            sRandomPlayerbotMgr.OnPlayerLogout(player);
        }
        catch (std::exception const& e)
        {
            TC_LOG_ERROR("playerbot", "Player logout hook for {} failed: {}", player->GetName(), e.what());
        }
        catch (...)
        {
            TC_LOG_ERROR("playerbot", "Player logout hook for {} failed with an unknown exception", player->GetName());
        }
    }

    void PlayerbotPlayerDelete(Player* player)
    {
        if (!player)
            return;

        try
        {
            // A Player can also be removed directly by core map/error cleanup.
            // Do not leave its owning session with a pointer to the destroyed Player.
            if (WorldSession* session = player->GetSession())
                if (session->IsBotSession() && session->GetPlayer() == player)
                    session->SetPlayer(nullptr);

            sRandomPlayerbotMgr.OnPlayerLogout(player); // also covers deletion without a normal logout hook
            PlayerbotAI* ai = player->GetPlayerbotAI();
            Player* master = ai ? ai->GetMaster() : nullptr;
            sRandomPlayerbotMgr.RemovePlayerBotEntry(player->GetGUID());
            if (master && master != player)
                if (PlayerbotMgr* masterMgr = master->GetPlayerbotMgr())
                    masterMgr->RemovePlayerBotEntry(player->GetGUID());

            if (ai)
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
        catch (std::exception const& e)
        {
            TC_LOG_ERROR("playerbot", "Player delete hook for {} failed: {}", player->GetName(), e.what());
        }
        catch (...)
        {
            TC_LOG_ERROR("playerbot", "Player delete hook for {} failed with an unknown exception", player->GetName());
        }
    }

    void PlayerbotBotPacketSent(Player* bot, WorldPacket const* packet)
    {
        if (!bot || !packet)
            return;

        if (PlayerbotAI* ai = bot->GetPlayerbotAI())
        {
            try
            {
                ai->HandleBotOutgoingPacket(*packet);
            }
            catch (std::exception const& e)
            {
                // server packet handlers parse wire layouts by hand; a layout
                // mismatch throws and must not take down the worldserver
                TC_LOG_ERROR("playerbot", "Bot {} failed to handle outgoing packet {}: {}",
                        bot->GetName(), packet->GetOpcode(), e.what());
            }
            catch (...)
            {
                TC_LOG_ERROR("playerbot", "Bot {} failed to handle outgoing packet {} with an unknown exception",
                        bot->GetName(), packet->GetOpcode());
            }
        }
    }

    void PlayerbotChat(Player* sender, uint32 type, uint32 /*lang*/, std::string const& msg, Player* receiver)
    {
        if (!sender)
            return;

        try
        {
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
        catch (std::exception const& e)
        {
            TC_LOG_ERROR("playerbot", "Chat handling from {} failed: {}", sender->GetName(), e.what());
        }
        catch (...)
        {
            TC_LOG_ERROR("playerbot", "Chat handling from {} failed with an unknown exception", sender->GetName());
        }
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

        // Keep teardown hooks active until every owned/pending bot is gone.
        sRandomPlayerbotMgr.ShutdownPlayerbotSessions();
        sRandomPlayerbotMgr.LogoutAllBots();
        SetEnabled(false);
    }
}
