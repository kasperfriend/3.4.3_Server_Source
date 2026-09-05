#pragma once

// Compatibility layer: the original playerbot plugin used the old DBC
// (s*Store) API which no longer exists in the modern DB2-based core.
// We expose the modern DB2 stores plus a spell-store shim that iterates
// the same spell-id range through SpellMgr.

#include "DataStores/DB2Stores.h"
#include "DataStores/DB2Structure.h"
#include "Spells/SpellMgr.h"

namespace pbot
{
    struct SpellStoreCompat
    {
        // Upper bound covers all WotLK-era spell ids (spell ids below 100000).
        uint32 GetNumRows() const { return 100000; }
        SpellInfo const* LookupEntry(uint32 id) const { return sSpellMgr->GetSpellInfo(id, DIFFICULTY_NONE); }
    };
}

static pbot::SpellStoreCompat sSpellStore;
