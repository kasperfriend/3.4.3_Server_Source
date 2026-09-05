#pragma once

#include <map>
#include <set>
#include <string>
#include <sstream>
#include <vector>
#include <list>

#include "Log.h"
#include "Cache/CharacterCache.h"

class Player;
class Creature;
class Item;
class WorldObject;
class Unit;
class ObjectGuid;

std::vector<std::string> split(const std::string &s, char delim);
#ifndef WIN32
int strcmpi(std::string s1, std::string s2);
#endif

#include "Spells/Spell.h"
#include "Server/WorldPacket.h"
#include "Server/WorldSession.h"
#include "Loot/LootMgr.h"
#include "Loot/Loot.h"
#include "Entities/Creature/GossipDef.h"
#include "Chat/Chat.h"
#include "../../common/Common.h"
#include "World/World.h"
#include "Spells/SpellMgr.h"
#include "Spells/SpellInfo.h"
#include "Spells/SpellHistory.h"
#include "Globals/ObjectMgr.h"
#include "Globals/ObjectAccessor.h"
#include "Entities/Unit/Unit.h"
#include "Entities/Item/Item.h"
#include "Entities/Item/ItemTemplate.h"
#include "Entities/Item/Container/Bag.h"
#include "Entities/Creature/Creature.h"
#include "Entities/Creature/Trainer.h"
#include "Entities/GameObject/GameObject.h"
#include "Entities/Pet/Pet.h"
#include "Entities/Corpse/Corpse.h"
#include "Entities/Player/TradeData.h"
#include "Maps/Map.h"
#include "Maps/MapManager.h"
#include "Mails/Mail.h"
#include "Miscellaneous/SharedDefines.h"
#include "Movement/MotionMaster.h"
#include "Spells/Auras/SpellAuras.h"
#include "Spells/Auras/SpellAuraEffects.h"
#include "Guilds/Guild.h"
#include "Guilds/GuildMgr.h"
#include "Groups/Group.h"
#include "Accounts/AccountMgr.h"
#include "Globals/ObjectMgr.h"
#include "Grids/Notifiers/GridNotifiers.h"
#include "Grids/Notifiers/GridNotifiersImpl.h"
#include "Grids/Cells/CellImpl.h"
#include "DataStores/DB2Stores.h"
#include "AI/CreatureAI.h"
#include "Entities/Unit/CharmInfo.h"

// client packet classes the bots feed into the session handlers
#include "Server/Packets/AreaTriggerPackets.h"
#include "Server/Packets/CharacterPackets.h"
#include "Server/Packets/ChatPackets.h"
#include "Server/Packets/DuelPackets.h"
#include "Server/Packets/GuildPackets.h"
#include "Server/Packets/EquipmentSetPackets.h"
#include "Server/Packets/ItemPackets.h"
#include "Server/Packets/LootPackets.h"
#include "Server/Packets/MailPackets.h"
#include "Server/Packets/MiscPackets.h"
#include "Server/Packets/MovementPackets.h"
#include "Server/Packets/NPCPackets.h"
#include "Server/Packets/PartyPackets.h"
#include "Server/Packets/QuestPackets.h"
#include "Server/Packets/SpellPackets.h"
#include "Server/Packets/TradePackets.h"


// --- playerbot compatibility helpers for the 3.4.3 item template layout ---
#define MAX_ITEM_PROTO_EFFECTS 5

inline ItemEffectEntry const* ItemEffectAt(ItemTemplate const* proto, uint32 index)
{
    return (proto && index < proto->Effects.size()) ? proto->Effects[index] : nullptr;
}

inline uint32 ItemSpellId(ItemTemplate const* proto, uint32 index)
{
    ItemEffectEntry const* effect = ItemEffectAt(proto, index);
    return effect ? uint32(effect->SpellID) : 0u;
}

inline uint32 ItemSpellCategory(ItemTemplate const* proto, uint32 index)
{
    ItemEffectEntry const* effect = ItemEffectAt(proto, index);
    return effect ? uint32(effect->SpellCategoryID) : 0u;
}

inline uint32 ItemSpellTrigger(ItemTemplate const* proto, uint32 index)
{
    ItemEffectEntry const* effect = ItemEffectAt(proto, index);
    return effect ? uint32(effect->TriggerType) : 0u;
}

inline int32 ItemSpellCharges(ItemTemplate const* proto, uint32 index)
{
    ItemEffectEntry const* effect = ItemEffectAt(proto, index);
    return effect ? int32(effect->Charges) : 0;
}

#include "playerbotDefs.h"
#include "PlayerbotAIAware.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "ChatHelper.h"
#include "PlayerbotAI.h"
