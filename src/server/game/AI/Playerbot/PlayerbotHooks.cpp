/*
 * Playerbot integration hooks (ported from ike3/mangosbot) - core side.
 */

#include "PlayerbotHooks.h"

namespace Playerbot
{
    namespace
    {
        Hooks _hooks = { };
        bool _enabled = false;
    }

    Hooks& GetHooks()
    {
        return _hooks;
    }

    bool IsEnabled()
    {
        return _enabled;
    }

    void SetEnabled(bool enabled)
    {
        _enabled = enabled;
    }
}
