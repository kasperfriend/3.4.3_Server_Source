#include "../../../pchdef.h"
#include "../../playerbot.h"
#include "strategy/actions/GuildAcceptAction.h"

using namespace std;
using namespace ai;

bool GuildAcceptAction::Execute(Event event)
{
    Player* master = GetMaster();
    if (!master)
        return false;

    bool accept = true;
    uint32 guildId = master->GetGuildId();
    if (!guildId)
    {
        ai->TellMaster("You are not in a guild");
        accept = false;
    }
    else if (bot->GetGuildId())
    {
        ai->TellMaster("Sorry, I am in a guild already");
        accept = false;
    }
    else if (!ai->GetSecurity()->CheckLevelFor(PLAYERBOT_SECURITY_INVITE, false, master, true))
    {
        accept = false;
    }

    if (accept)
    {
        bot->SetGuildIdInvited(guildId);
        WorldPackets::Guild::AcceptGuildInvite acceptInvite{WorldPacket(CMSG_ACCEPT_GUILD_INVITE)};
        bot->GetSession()->HandleGuildAcceptInvite(acceptInvite);
    }
    else
    {
        WorldPackets::Guild::GuildDeclineInvitation decline{WorldPacket(CMSG_GUILD_DECLINE_INVITATION)};
        bot->GetSession()->HandleGuildDeclineInvitation(decline);
    }
    return true;
}
