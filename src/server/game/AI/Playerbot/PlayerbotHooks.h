/*
 * Playerbot integration hooks (ported from ike3/mangosbot).
 *
 * The bot implementation lives in the `plugins` static library which is
 * linked *after* `game`, so the core may not reference its symbols directly.
 * Instead the plugin registers a set of callbacks at startup and the core
 * calls them through this thin, always-safe indirection layer.
 */

#ifndef TRINITY_PLAYERBOT_HOOKS_H
#define TRINITY_PLAYERBOT_HOOKS_H

#include "Define.h"
#include "ObjectGuid.h"
#include <string>

class ChatHandler;
class Player;
class WorldPacket;
class WorldSession;

namespace Playerbot
{
    struct Hooks
    {
        /// called once per world tick (drives the random bot manager)
        void (*OnWorldUpdate)(uint32 diff);
        /// called from Player::Update for every player that owns bots / is a bot
        void (*OnPlayerUpdate)(Player* player, uint32 diff);
        /// player entered / left the world
        void (*OnPlayerLogin)(Player* player);
        void (*OnPlayerLogout)(Player* player);
        /// server -> client packet destined for a bot, lets the bot react on it
        void (*OnBotPacketSent)(Player* bot, WorldPacket const* packet);
        /// chat message sent by a player, used for the "master" commands
        void (*OnPlayerChat)(Player* sender, uint32 type, uint32 lang, std::string const& msg, Player* receiver);
        /// player object is destroyed - drop the AI attached to it
        void (*OnPlayerDelete)(Player* player);
    };

    /// registry, valid for the lifetime of the process
    TC_GAME_API Hooks& GetHooks();

    /// true once the plugin registered itself and the bot system is enabled
    TC_GAME_API bool IsEnabled();
    TC_GAME_API void SetEnabled(bool enabled);

    inline void OnWorldUpdate(uint32 diff)
    {
        if (IsEnabled() && GetHooks().OnWorldUpdate)
            GetHooks().OnWorldUpdate(diff);
    }

    inline void OnPlayerUpdate(Player* player, uint32 diff)
    {
        if (IsEnabled() && GetHooks().OnPlayerUpdate)
            GetHooks().OnPlayerUpdate(player, diff);
    }

    inline void OnPlayerLogin(Player* player)
    {
        if (IsEnabled() && GetHooks().OnPlayerLogin)
            GetHooks().OnPlayerLogin(player);
    }

    inline void OnPlayerLogout(Player* player)
    {
        if (IsEnabled() && GetHooks().OnPlayerLogout)
            GetHooks().OnPlayerLogout(player);
    }

    inline void OnBotPacketSent(Player* bot, WorldPacket const* packet)
    {
        if (IsEnabled() && GetHooks().OnBotPacketSent)
            GetHooks().OnBotPacketSent(bot, packet);
    }

    inline void OnPlayerChat(Player* sender, uint32 type, uint32 lang, std::string const& msg, Player* receiver)
    {
        if (IsEnabled() && GetHooks().OnPlayerChat)
            GetHooks().OnPlayerChat(sender, type, lang, msg, receiver);
    }

    inline void OnPlayerDelete(Player* player)
    {
        if (IsEnabled() && GetHooks().OnPlayerDelete)
            GetHooks().OnPlayerDelete(player);
    }
}

#endif
