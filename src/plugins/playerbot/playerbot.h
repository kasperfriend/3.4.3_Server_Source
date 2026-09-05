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
#include "Loot/LootMgr.h"
#include "Entities/Creature/GossipDef.h"
#include "Chat/Chat.h"
#include "../../common/Common.h"
#include "World/World.h"
#include "Spells/SpellMgr.h"
#include "Globals/ObjectMgr.h"
#include "Entities/Unit/Unit.h"
#include "Miscellaneous/SharedDefines.h"
#include "Movement/MotionMaster.h"
#include "Spells/Auras/SpellAuras.h"
#include "Guilds/Guild.h"
#include "Groups/Group.h"
#include "Accounts/AccountMgr.h"
#include "Globals/ObjectMgr.h"

#include "playerbotDefs.h"
#include "PlayerbotAIAware.h"
#include "PlayerbotMgr.h"
#include "RandomPlayerbotMgr.h"
#include "ChatHelper.h"
#include "PlayerbotAI.h"
