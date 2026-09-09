#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/UseMeetingStoneAction.h"
#include "../../PlayerbotAIConfig.h"
#include "Phasing/PhasingHandler.h"

bool UseMeetingStoneAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    WorldPacket p(event.getPacket());
    p.rpos(0);
    ObjectGuid guid;
    p >> guid;

    if (master->GetSelectedPlayer() && master->GetSelectedPlayer() != bot)
        return false;

    if (!master->GetSelectedPlayer() && master->GetGroup() != bot->GetGroup())
        return false;

    if (master->IsBeingTeleported())
        return false;

    if (bot->IsInCombat())
    {
        ai->TellMasterNoFacing("I am in combat");
        return false;
    }

    Map* map = master->GetMap();
    if (!map)
        return NULL;

    GameObject *gameObject = map->GetGameObject(guid);
    if (!gameObject)
        return false;

    const GameObjectTemplate* goInfo = gameObject->GetGOInfo();
    if (!goInfo || goInfo->type != GAMEOBJECT_TYPE_RITUAL)
        return false;

    return Teleport();
}


bool SummonAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    if (bot->IsInCombat())
    {
        ai->TellMasterNoFacing("I am in combat");
        return false;
    }

    if (!master->IsInWorld() || master->IsBeingTeleported() || bot->IsBeingTeleported())
    {
        ai->TellMasterNoFacing("Someone is teleporting, try again in a moment");
        return false;
    }

    if (!CanBeSummonedBy(master))
    {
        ai->TellMasterNoFacing("You cannot summon me");
        return false;
    }

    return Teleport();
}

bool SummonAction::CanBeSummonedBy(Player* master)
{
    // Game masters can always summon (historical behavior).
    if (master->GetSession() && master->GetSession()->GetSecurity() >= SEC_GAMEMASTER)
        return true;

    // A player-owned bot belongs to its master: summoning it to the master is
    // the whole point of the command and must not require GM rights.
    uint32 botAccount = sCharacterCache->GetCharacterAccountIdByGuid(bot->GetGUID());
    if (!sPlayerbotAIConfig.IsInRandomAccountList(botAccount))
        return true;

    // Shared random bots: only within the same group, mirroring the meeting
    // stone rules in UseMeetingStoneAction, so strangers cannot yank them away.
    Group* group = bot->GetGroup();
    return group && group == master->GetGroup();
}

bool SummonAction::Teleport()
{
    Player* master = GetMaster();
    if (!master || !master->IsInWorld() || master->IsBeingTeleported() || bot->IsBeingTeleported())
    {
        ai->TellMasterNoFacing("Someone is teleporting, try again in a moment");
        return false;
    }

    uint32 mapId = master->GetMapId();

    // Preferred placement: a free spot next to the master with line of sight.
    float followAngle = GetFollowAngle();
    for (float angle = followAngle - M_PI; angle <= followAngle + M_PI + 0.01f; angle += M_PI / 4)
    {
        float x = master->GetPositionX() + cos(angle) * sPlayerbotAIConfig.followDistance;
        float y = master->GetPositionY() + sin(angle) * sPlayerbotAIConfig.followDistance;
        float z = master->GetPositionZ();
        if (master->IsWithinLOS(x, y, z))
            return TeleportBot(mapId, x, y, z);
    }

    // Fallback: the core GM .summon command does no LOS check at all - it uses
    // GetClosePoint and teleports. Dense gameobjects, custom collision or
    // slightly off vmaps can fail every LOS ray above even though the spot is
    // perfectly fine, which used to end here as "There is not enough place to
    // summon me". Mirror the core behavior instead of failing.
    float x, y, z;
    master->GetClosePoint(x, y, z, bot->GetCombatReach());
    if (TeleportBot(mapId, x, y, z))
        return true;

    // Last resort: the master's exact position.
    return TeleportBot(mapId, master->GetPositionX(), master->GetPositionY(), master->GetPositionZ());
}

bool SummonAction::TeleportBot(uint32 mapId, float x, float y, float z)
{
    Player* master = GetMaster();
    if (!master || !master->IsInWorld())
        return false;

    bot->AttackStop();
    ai->InterruptSpell();
    bot->GetMotionMaster()->Clear();

    // Same-map summons must land in the master's instance (dungeons/raids);
    // cross-map TeleportTo ignores the instance hint. This mirrors the core GM
    // .summon command, which also inherits the master's phase so the bot ends
    // up seeing and being seen by the same world.
    Optional<uint32> instanceId;
    if (mapId == bot->GetMapId() && master->GetMap())
        instanceId = master->GetMap()->GetInstanceId();

    if (!bot->TeleportTo(mapId, x, y, z, bot->GetOrientation(), TELE_TO_NONE, instanceId))
    {
        ai->TellMasterNoFacing("There is not enough place to summon me");
        return false;
    }

    PhasingHandler::InheritPhaseShift(bot, master);
    return true;
}
