#include "../pchdef.h"
#include "playerbot.h"
#include "ChatHelper.h"

using namespace ai;
using namespace std;

map<string, uint32> ChatHelper::consumableSubClasses;
map<string, uint32> ChatHelper::tradeSubClasses;
map<string, uint32> ChatHelper::itemQualities;
map<string, uint32> ChatHelper::slots;
map<string, ChatMsg> ChatHelper::chats;
map<uint8, string> ChatHelper::classes;
map<uint8, string> ChatHelper::races;
map<uint8, map<uint8, string> > ChatHelper::specs;

template<class T>
static bool substrContainsInMap(string const& searchTerm, map<string, T> const& searchIn)
{
    for (typename map<string, T>::const_iterator i = searchIn.begin(); i != searchIn.end(); ++i)
    {
		string const& term = i->first;
		if (term.size() > 1 && searchTerm.find(term) != string::npos)
            return true;
    }

    return false;
}

ChatHelper::ChatHelper(PlayerbotAI* ai) : PlayerbotAIAware(ai)
{
    itemQualities["poor"] = ITEM_QUALITY_POOR;
    itemQualities["gray"] = ITEM_QUALITY_POOR;
    itemQualities["normal"] = ITEM_QUALITY_NORMAL;
    itemQualities["white"] = ITEM_QUALITY_NORMAL;
    itemQualities["uncommon"] = ITEM_QUALITY_UNCOMMON;
    itemQualities["green"] = ITEM_QUALITY_UNCOMMON;
    itemQualities["rare"] = ITEM_QUALITY_RARE;
    itemQualities["blue"] = ITEM_QUALITY_RARE;
    itemQualities["epic"] = ITEM_QUALITY_EPIC;
    itemQualities["violet"] = ITEM_QUALITY_EPIC;

    consumableSubClasses["potion"] = ITEM_SUBCLASS_POTION;
    consumableSubClasses["elixir"] = ITEM_SUBCLASS_ELIXIR;
    consumableSubClasses["flask"] = ITEM_SUBCLASS_FLASK;
    consumableSubClasses["scroll"] = ITEM_SUBCLASS_SCROLL;
    consumableSubClasses["food"] = ITEM_SUBCLASS_FOOD_DRINK;
    consumableSubClasses["bandage"] = ITEM_SUBCLASS_BANDAGE;
    consumableSubClasses["enchant"] = ITEM_SUBCLASS_CONSUMABLE_OTHER;

    tradeSubClasses["cloth"] = ITEM_SUBCLASS_CLOTH;
    tradeSubClasses["leather"] = ITEM_SUBCLASS_LEATHER;
    tradeSubClasses["metal"] = ITEM_SUBCLASS_METAL_STONE;
    tradeSubClasses["stone"] = ITEM_SUBCLASS_METAL_STONE;
    tradeSubClasses["ore"] = ITEM_SUBCLASS_METAL_STONE;
    tradeSubClasses["meat"] = ITEM_SUBCLASS_MEAT;
    tradeSubClasses["herb"] = ITEM_SUBCLASS_HERB;
    tradeSubClasses["elemental"] = ITEM_SUBCLASS_ELEMENTAL;
    tradeSubClasses["disenchants"] = ITEM_SUBCLASS_ENCHANTING;
    tradeSubClasses["enchanting"] = ITEM_SUBCLASS_ENCHANTING;
    tradeSubClasses["gems"] = ITEM_SUBCLASS_JEWELCRAFTING;
    tradeSubClasses["jewels"] = ITEM_SUBCLASS_JEWELCRAFTING;
    tradeSubClasses["jewelcrafting"] = ITEM_SUBCLASS_JEWELCRAFTING;

    slots["head"] = EQUIPMENT_SLOT_HEAD;
    slots["neck"] = EQUIPMENT_SLOT_NECK;
    slots["shoulder"] = EQUIPMENT_SLOT_SHOULDERS;
    slots["shirt"] = EQUIPMENT_SLOT_BODY;
    slots["chest"] = EQUIPMENT_SLOT_CHEST;
    slots["waist"] = EQUIPMENT_SLOT_WAIST;
    slots["legs"] = EQUIPMENT_SLOT_LEGS;
    slots["feet"] = EQUIPMENT_SLOT_FEET;
    slots["wrist"] = EQUIPMENT_SLOT_WRISTS;
    slots["hands"] = EQUIPMENT_SLOT_HANDS;
    slots["finger 1"] = EQUIPMENT_SLOT_FINGER1;
    slots["finger 2"] = EQUIPMENT_SLOT_FINGER2;
    slots["trinket 1"] = EQUIPMENT_SLOT_TRINKET1;
    slots["trinket 2"] = EQUIPMENT_SLOT_TRINKET2;
    slots["back"] = EQUIPMENT_SLOT_BACK;
    slots["main hand"] = EQUIPMENT_SLOT_MAINHAND;
    slots["off hand"] = EQUIPMENT_SLOT_OFFHAND;
    slots["ranged"] = EQUIPMENT_SLOT_RANGED;
    slots["tabard"] = EQUIPMENT_SLOT_TABARD;

    chats["party"] = CHAT_MSG_PARTY;
    chats["p"] = CHAT_MSG_PARTY;
    chats["guild"] = CHAT_MSG_GUILD;
    chats["g"] = CHAT_MSG_GUILD;
    chats["raid"] = CHAT_MSG_RAID;
    chats["r"] = CHAT_MSG_RAID;
    chats["whisper"] = CHAT_MSG_WHISPER;
    chats["w"] = CHAT_MSG_WHISPER;

    classes[CLASS_DRUID] = "druid";
    specs[CLASS_DRUID][0] = "balance";
    specs[CLASS_DRUID][1] = "feral combat";
    specs[CLASS_DRUID][2] = "restoration";

    classes[CLASS_HUNTER] = "hunter";
    specs[CLASS_HUNTER][0] = "beast mastery";
    specs[CLASS_HUNTER][1] = "marksmanship";
    specs[CLASS_HUNTER][2] = "survival";

    classes[CLASS_MAGE] = "mage";
    specs[CLASS_MAGE][0] = "arcane";
    specs[CLASS_MAGE][1] = "fire";
    specs[CLASS_MAGE][2] = "frost";

    classes[CLASS_PALADIN] = "paladin";
    specs[CLASS_PALADIN][0] = "holy";
    specs[CLASS_PALADIN][1] = "protection";
    specs[CLASS_PALADIN][2] = "retribution";

    classes[CLASS_PRIEST] = "priest";
    specs[CLASS_PRIEST][0] = "discipline";
    specs[CLASS_PRIEST][1] = "holy";
    specs[CLASS_PRIEST][2] = "shadow";

    classes[CLASS_ROGUE] = "rogue";
    specs[CLASS_ROGUE][0] = "assassination";
    specs[CLASS_ROGUE][1] = "combat";
    specs[CLASS_ROGUE][2] = "subtlety";

    classes[CLASS_SHAMAN] = "shaman";
    specs[CLASS_SHAMAN][0] = "elemental";
    specs[CLASS_SHAMAN][1] = "enhancement";
    specs[CLASS_SHAMAN][2] = "restoration";

    classes[CLASS_WARLOCK] = "warlock";
    specs[CLASS_WARLOCK][0] = "affliction";
    specs[CLASS_WARLOCK][1] = "demonology";
    specs[CLASS_WARLOCK][2] = "destruction";

    classes[CLASS_WARRIOR] = "warrior";
    specs[CLASS_WARRIOR][0] = "arms";
    specs[CLASS_WARRIOR][1] = "fury";
    specs[CLASS_WARRIOR][2] = "protection";

    classes[CLASS_DEATH_KNIGHT] = "death knight";
    specs[CLASS_DEATH_KNIGHT][0] = "blood";
    specs[CLASS_DEATH_KNIGHT][1] = "frost";
    specs[CLASS_DEATH_KNIGHT][2] = "unholy";

    races[RACE_BLOODELF] = "Blood Elf";
    races[RACE_DRAENEI] = "Draenei";
    races[RACE_DWARF] = "Dwarf";
    races[RACE_GNOME] = "Gnome";
    races[RACE_HUMAN] = "Human";
    races[RACE_NIGHTELF] = "Night Elf";
    races[RACE_ORC] = "Orc";
    races[RACE_TAUREN] = "Tauren";
    races[RACE_TROLL] = "Troll";
    races[RACE_UNDEAD_PLAYER] = "Undead";
}

string ChatHelper::formatMoney(uint64 copper)
{
    ostringstream out;
	if (!copper)
	{
		out << "0|TInterface\\AddOns\\AtlasLoot\\Images\\bronze:0|t";
		return out.str();
	}

    uint64 gold = copper / 10000;
    copper -= (gold * 10000);
    uint64 silver = copper / 100;
    copper -= (silver * 100);
    out << " ";
    if (gold > 0)
        out << gold <<  "|TInterface\\AddOns\\AtlasLoot\\Images\\gold:0|t ";
    if (silver > 0 && gold < 50)
        out << silver <<  "|TInterface\\AddOns\\AtlasLoot\\Images\\silver:0|t ";
	if (copper > 0 && gold < 10)
		out << copper <<  "|TInterface\\AddOns\\AtlasLoot\\Images\\bronze:0|t";

    return out.str();
}

uint32 ChatHelper::parseMoney(string& text)
{
    // if user specified money in ##g##s##c format
    string acum = "";
    uint32 copper = 0;
    for (uint8 i = 0; i < text.length(); i++)
    {
        if (text[i] == 'g')
        {
            copper += (atol(acum.c_str()) * 100 * 100);
            acum = "";
        }
        else if (text[i] == 'c')
        {
            copper += atol(acum.c_str());
            acum = "";
        }
        else if (text[i] == 's')
        {
            copper += (atol(acum.c_str()) * 100);
            acum = "";
        }
        else if (text[i] == ' ')
            break;
        else if (text[i] >= 48 && text[i] <= 57)
            acum += text[i];
        else
        {
            copper = 0;
            break;
        }
    }
    return copper;
}

ItemIds ChatHelper::parseItems(string& text)
{
    ItemIds itemIds;

    uint8 pos = 0;
    while (true)
    {
        int i = text.find("Hitem:", pos);
        if (i == -1)
            break;
        pos = i + 6;
        int endPos = text.find(':', pos);
        if (endPos == -1)
            break;
        string idC = text.substr(pos, endPos - pos);
        uint32 id = atol(idC.c_str());
        pos = endPos;
        if (id)
            itemIds.insert(id);
    }

    return itemIds;
}

string ChatHelper::formatQuest(Quest const* quest)
{
    if (!quest)
        return "[unknown quest]";

    ostringstream out;
    out << "|cFFFFFF00|Hquest:" << quest->GetQuestId() << ':' << quest->GetQuestLevel() << "|h[" << quest->GetLogTitle() << "]|h|r";
    return out.str();
}

string ChatHelper::formatGameobject(GameObject* go)
{
    ostringstream out;
    out << "|cFFFFFF00|Hfound:" << go->GetGUID().GetCounter() << ":" << go->GetEntry() << ":" << go->GetMapId() << ":" << "|h[" << go->GetGOInfo()->name << "]|h|r";
    return out.str();
}

string ChatHelper::formatSpell(SpellInfo const *sInfo)
{
    // missing spell info must not crash chat formatting
    if (!sInfo || !sInfo->SpellName)
        return "[unknown spell]";

    ostringstream out;
    out << "|cffffffff|Hspell:" << sInfo->Id << "|h[" << sInfo->SpellName->Str[LOCALE_enUS] << "]|h|r";
    return out.str();
}

string ChatHelper::formatItem(ItemTemplate const * proto, int count)
{
    // item templates can vanish from the DB while bots (and players) still hold
    // the item; dozens of chat sites call this with an unguarded GetTemplate()
    if (!proto)
        return "[unknown item]";

    char color[32];
    sprintf(color, "%x", ItemQualityColors[proto->GetQuality()]);

    ostringstream out;
    out << "|c" << color << "|Hitem:" << proto->GetId()
        << ":0:0:0:0:0:0:0" << "|h[" << proto->GetDefaultLocaleName()
        << "]|h|r";

    if (count > 1)
        out << "x" << count;

    return out.str();
}

ChatMsg ChatHelper::parseChat(string& text)
{
    if (chats.find(text) != chats.end())
        return chats[text];

    return CHAT_MSG_SYSTEM;
}

string ChatHelper::formatChat(ChatMsg chat)
{
    switch (chat)
    {
    case CHAT_MSG_GUILD:
        return "guild";
    case CHAT_MSG_PARTY:
        return "party";
    case CHAT_MSG_WHISPER:
        return "whisper";
    case CHAT_MSG_RAID:
        return "raid";
    }

    return "unknown";
}


uint32 ChatHelper::parseSpell(string& text)
{
    PlayerbotChatHandler handler(ai->GetBot());
    return handler.extractSpellId(text);
}

list<ObjectGuid> ChatHelper::parseGameobjects(string& text)
{
    list<ObjectGuid> gos;
    //    Link format
    //    |cFFFFFF00|Hfound:<counter>:<entry>:<mapId>:|h[Copper Vein]|h|r

    size_t pos = 0;
    while (true)
    {
        size_t i = text.find("Hfound:", pos);
        if (i == string::npos)
            break;

        pos = i + 7;
        size_t endPos = text.find(':', pos);
        if (endPos == string::npos)
            break;

        uint64 counter = strtoull(text.substr(pos, endPos - pos).c_str(), nullptr, 10);

        // extract GO entry
        pos = endPos + 1;
        endPos = text.find(':', pos);
        if (endPos == string::npos)
            break;

        uint32 entry = atol(text.substr(pos, endPos - pos).c_str());

        // extract map id
        pos = endPos + 1;
        endPos = text.find(':', pos);
        if (endPos == string::npos)
            break;

        uint32 mapId = atol(text.substr(pos, endPos - pos).c_str());
        pos = endPos + 1;

        if (counter && entry)
            gos.push_back(ObjectGuid::Create<HighGuid::GameObject>(mapId, entry, counter));
    }

    return gos;
}

string ChatHelper::formatQuestObjective(string name, int available, int required)
{
    ostringstream out;
    out << "|cFFFFFFFF" << name << (available >= required ? "|c0000FF00: " : "|c00FF0000: ")
        << available << "/" << required << "|r";

    return out.str();
}


uint32 ChatHelper::parseItemQuality(string text)
{
    if (itemQualities.find(text) == itemQualities.end())
        return MAX_ITEM_QUALITY;

    return itemQualities[text];
}

bool ChatHelper::parseItemClass(string text, uint32 *itemClass, uint32 *itemSubClass)
{
    if (text == "questitem")
    {
        *itemClass = ITEM_CLASS_QUEST;
        *itemSubClass = ITEM_SUBCLASS_QUEST;
        return true;
    }

    if (consumableSubClasses.find(text) != consumableSubClasses.end())
    {
        *itemClass = ITEM_CLASS_CONSUMABLE;
        *itemSubClass = consumableSubClasses[text];
        return true;
    }

    if (tradeSubClasses.find(text) != tradeSubClasses.end())
    {
        *itemClass = ITEM_CLASS_TRADE_GOODS;
        *itemSubClass = tradeSubClasses[text];
        return true;
    }

    return false;
}

uint32 ChatHelper::parseSlot(string text)
{
    if (slots.find(text) != slots.end())
        return slots[text];

    return EQUIPMENT_SLOT_END;
}

bool ChatHelper::parseable(string text)
{
    return text.find("|H") != string::npos ||
            text == "questitem" ||
            substrContainsInMap<uint32>(text, consumableSubClasses) ||
            substrContainsInMap<uint32>(text, tradeSubClasses) ||
            substrContainsInMap<uint32>(text, itemQualities) ||
            substrContainsInMap<uint32>(text, slots) ||
            substrContainsInMap<ChatMsg>(text, chats) ||
            parseMoney(text) > 0;
}

string ChatHelper::formatClass(Player* player, int spec)
{
    uint8 cls = player->GetClass();

    ostringstream out;
    out << specs[cls][spec] << " (";

    int c0 = 0, c1 = 0, c2 = 0;
    PlayerTalentMap const& talentMap = player->GetPlayerTalentMap(player->GetActiveTalentGroup());
    for (PlayerTalentMap::const_iterator i = talentMap.begin(); i != talentMap.end(); ++i)
    {
        if (i->second.State == PLAYERSPELL_REMOVED)
            continue;

        TalentEntry const* talentInfo = sTalentStore.LookupEntry(i->first);
        if (!talentInfo)
            continue;

        uint32 const* talentTabIds = sDB2Manager.GetTalentTabPages(player->GetClass());
        if (talentInfo->TabID == talentTabIds[0]) c0++;
        if (talentInfo->TabID == talentTabIds[1]) c1++;
        if (talentInfo->TabID == talentTabIds[2]) c2++;
    }

    out << (c0 ? "|h|cff00ff00" : "") << c0 << "|h|cffffffff/";
    out << (c1 ? "|h|cff00ff00" : "") << c1 << "|h|cffffffff/";
    out << (c2 ? "|h|cff00ff00" : "") << c2 << "|h|cffffffff";

    out <<  ") " << classes[cls];
    return out.str();
}

string ChatHelper::formatClass(uint8 cls)
{
    return classes[cls];
}

string ChatHelper::formatRace(uint8 race)
{
    return races[race];
}
