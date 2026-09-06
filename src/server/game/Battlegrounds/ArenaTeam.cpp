/*
 * This file is part of the TrinityCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "ArenaTeam.h"
#include "ArenaTeamMgr.h"
#include "CharacterCache.h"
#include "DatabaseEnv.h"
#include "Group.h"
#include "Log.h"
#include "Map.h"
#include "ObjectAccessor.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "World.h"
#include "WorldPacket.h"
#include "WorldSession.h"

ArenaTeam::ArenaTeam(Group* group, uint8 type)
{
    Type = type;
    TeamId = sArenaTeamMgr->GenerateArenaTeamId();

    // alistar since they removed the team names I guess we can use the leader name
    TeamName = "Team: ";
    TeamName += group->GetLeaderName();

    for (GroupReference* itr = group->GetFirstMember(); itr != nullptr; itr = itr->next())
    {
        Player* player = itr->GetSource();
        if (player == nullptr)
            continue;

        CreateTeam(player);
    }
}

ArenaTeam::ArenaTeam(Player* player, uint8 type)
{
    Type = type;
    TeamId = sArenaTeamMgr->GenerateArenaTeamId();

    CreateTeam(player);
}

ArenaTeam::~ArenaTeam()
{
}

void ArenaTeam::SendStats(WorldSession* /*session*/)
{
    // alistar: TODO need packets for this. It's most likely not need due to new arena system?
#if 0
    WorldPacket data(SMSG_ARENA_TEAM_STATS, 4*7);
    data << uint32(GetId());                                // team id
    data << uint32(Stats.Rating);                           // rating
    data << uint32(Stats.WeekGames);                        // games this week
    data << uint32(Stats.WeekWins);                         // wins this week
    data << uint32(Stats.SeasonGames);                      // played this season
    data << uint32(Stats.SeasonWins);                       // wins this season
    data << uint32(Stats.Rank);                             // rank
    session->SendPacket(&data);
#endif
}

void ArenaTeam::NotifyStatsChanged()
{
    // This is called after a rated match ended
    // Updates arena team stats for every member of the team (not only the ones who participated!)
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (Player* player = ObjectAccessor::FindConnectedPlayer(itr->Guid))
            SendStats(player->GetSession());
}

void ArenaTeamMember::ModifyPersonalRating(int32 mod, uint32 type)
{
    auto it = sArenaTeamMgr->GetPersonalArenaTeams().find(Guid);
    if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
        return;

    uint16& PersonalRating = it->second[ArenaTeam::GetSlotByType(type)].PersonalRating;

    if (int32(PersonalRating) + mod < 0)
        PersonalRating = 0;
    else
        PersonalRating += mod;

    if (Player* player = ObjectAccessor::FindPlayer(Guid))
    {
        player->UpdateCriteria(CriteriaType::EarnTeamArenaRating, PersonalRating, type);
        player->UpdateCriteria(CriteriaType::EarnPersonalArenaRating, PersonalRating, type);
    }
}

void ArenaTeamMember::ModifyMatchmakerRating(int32 mod, uint32 type)
{
    auto it = sArenaTeamMgr->GetPersonalArenaTeams().find(Guid);
    if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
        return;

    uint16& MatchMakerRating = it->second[ArenaTeam::GetSlotByType(type)].MatchMakerRating;

    if (int32(MatchMakerRating) + mod < 0)
        MatchMakerRating = 0;
    else
        MatchMakerRating += mod;
}

void ArenaTeam::BroadcastPacket(WorldPacket* packet)
{
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (Player* player = ObjectAccessor::FindConnectedPlayer(itr->Guid))
            player->SendDirectMessage(packet);
}

uint8 ArenaTeam::GetSlotByType(uint32 type)
{
    switch (type)
    {
        case ARENA_TEAM_2v2: return 0;
        case ARENA_TEAM_3v3: return 1;
        case ARENA_TEAM_5v5: return 2;
        default:
            break;
    }
    TC_LOG_ERROR("bg.arena", "FATAL: Unknown arena team type {} for some arena team", type);
    return 0xFF;
}

uint8 ArenaTeam::GetTypeBySlot(uint8 slot)
{
    switch (slot)
    {
        case 0: return ARENA_TEAM_2v2;
        case 1: return ARENA_TEAM_3v3;
        case 2: return ARENA_TEAM_5v5;
        default:
            break;
    }
    TC_LOG_ERROR("bg.arena", "FATAL: Unknown arena team slot {} for some arena team", slot);
    return 0xFF;
}

uint32 ArenaTeam::GetRating() const
{
    uint32 averageRating = 0;
    uint32 divider = 0;
    const uint8 slot = GetSlot();

    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        auto it = sArenaTeamMgr->GetPersonalArenaTeams().find(itr->Guid);
        if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
            continue;

        averageRating += it->second[slot].PersonalRating;
        ++divider;
    }

    if (divider == 0)
        divider = 1;

    averageRating /= divider;

    return averageRating;
}

bool ArenaTeam::IsMember(ObjectGuid guid) const
{
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (itr->Guid == guid)
            return true;

    return false;
}

uint32 ArenaTeam::GetAverageMMR(Group* group) const
{
    if (!group)
        return 0;

    uint32 matchMakerRating = 0;
    uint32 playerDivider = 0;
    const uint8 slot = GetSlot();

    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        auto it = sArenaTeamMgr->GetPersonalArenaTeams().find(itr->Guid);
        if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
            continue;

        // Skip if player is not a member of group
        if (!group->IsMember(itr->Guid))
            continue;

        matchMakerRating += it->second[slot].MatchMakerRating;
        ++playerDivider;
    }

    // x/0 = crash
    if (playerDivider == 0)
        playerDivider = 1;

    matchMakerRating /= playerDivider;

    return matchMakerRating;
}

float ArenaTeam::GetChanceAgainst(uint32 ownRating, uint32 opponentRating)
{
    // Returns the chance to win against a team with the given rating, used in the rating adjustment calculation
    // ELO system
    return 1.0f / (1.0f + std::exp(std::log(10.0f) * (float(opponentRating) - float(ownRating)) / 650.0f));
}

int32 ArenaTeam::GetMatchmakerRatingMod(uint32 ownRating, uint32 opponentRating, bool won)
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);
    float won_mod = (won) ? 1.0f : 0.0f;
    float mod = won_mod - chance;

    // Work in progress:
    /*
    // This is a simulation, as there is not much info on how it really works
    float confidence_mod = min(1.0f - fabs(mod), 0.5f);

    // Apply confidence factor to the mod:
    mod *= confidence_factor

    // And only after that update the new confidence factor
    confidence_factor -= ((confidence_factor - 1.0f) * confidence_mod) / confidence_factor;
    */

    // Real rating modification
    mod *= sWorld->getFloatConfig(CONFIG_ARENA_MATCHMAKER_RATING_MODIFIER);

    return (int32)ceil(mod);
}

int32 ArenaTeam::GetRatingMod(uint32 ownRating, uint32 opponentRating, bool won /*, float confidence_factor*/)
{
    // 'Chance' calculation - to beat the opponent
    // This is a simulation. Not much info on how it really works
    float chance = GetChanceAgainst(ownRating, opponentRating);

    // Calculate the rating modification
    float mod;

    /// @todo Replace this hack with using the confidence factor (limiting the factor to 2.0f)
    if (won)
    {
        if (ownRating < 1300)
        {
            float win_rating_modifier1 = sWorld->getFloatConfig(CONFIG_ARENA_WIN_RATING_MODIFIER_1);

            if (ownRating < 1000)
                mod =  win_rating_modifier1 * (1.0f - chance);
            else
                mod = ((win_rating_modifier1 / 2.0f) + ((win_rating_modifier1 / 2.0f) * (1300.0f - float(ownRating)) / 300.0f)) * (1.0f - chance);
        }
        else
            mod = sWorld->getFloatConfig(CONFIG_ARENA_WIN_RATING_MODIFIER_2) * (1.0f - chance);
    }
    else
        mod = sWorld->getFloatConfig(CONFIG_ARENA_LOSE_RATING_MODIFIER) * (-chance);

    return (int32)ceil(mod);
}

int32 ArenaTeam::WonAgainst(uint32 ownMMRating, uint32 opponentMMRating, int32& ratingChange)
{
    // Called when the team has won
    ratingChange = GetMatchmakerRatingMod(ownMMRating, opponentMMRating, true);

    // Return the rating change, used to display it on the results screen
    return ratingChange;
}

int32 ArenaTeam::LostAgainst(uint32 ownMMRating, uint32 opponentMMRating, int32& ratingChange)
{
    // Called when the team has lost
    ratingChange = GetMatchmakerRatingMod(ownMMRating, opponentMMRating, false);

    // Return the rating change, used to display it on the results screen
    return ratingChange;
}

void ArenaTeam::MemberLost(Player* player, uint32 againstMatchmakerRating, int32 matchmakerRatingChange)
{
    const uint8 slot = GetSlot();

    // Called for each participant of a match after losing
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        if (itr->Guid == player->GetGUID())
        {
            auto it = sArenaTeamMgr->GetPersonalArenaTeams().find(itr->Guid);
            if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
                continue;

            // Update personal rating
            int32 mod = GetRatingMod(it->second[slot].PersonalRating, againstMatchmakerRating, false);
            itr->ModifyPersonalRating(mod, GetType());

            // Update matchmaker rating
            itr->ModifyMatchmakerRating(matchmakerRatingChange, Type);

            // Update personal played stats
            it->second[slot].WeekGames   += 1;
            it->second[slot].SeasonGames += 1;
            return;
        }
    }
}

void ArenaTeam::OfflineMemberLost(ObjectGuid guid, uint32 againstMatchmakerRating, int32 MatchmakerRatingChange)
{
    const uint8 slot = GetSlot();

    // Called for offline player after ending rated arena match!
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        if (itr->Guid == guid)
        {
            auto it = sArenaTeamMgr->GetPersonalArenaTeams().find(itr->Guid);
            if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
                continue;

            // update personal rating
            int32 mod = GetRatingMod(it->second[slot].PersonalRating, againstMatchmakerRating, false);
            itr->ModifyPersonalRating(mod, GetType());

            // update matchmaker rating
            itr->ModifyMatchmakerRating(MatchmakerRatingChange, Type);

            // update personal played stats
            it->second[slot].WeekGames += 1;
            it->second[slot].SeasonGames += 1;
            return;
        }
    }
}

void ArenaTeam::MemberWon(Player* player, uint32 againstMatchmakerRating, int32 matchmakerRatingChange)
{
    const uint8 slot = GetSlot();

    // called for each participant after winning a match
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        if (itr->Guid == player->GetGUID())
        {
            auto it = sArenaTeamMgr->GetPersonalArenaTeams().find(itr->Guid);
            if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
                continue;

            // update personal rating
            int32 mod = GetRatingMod(it->second[slot].PersonalRating, againstMatchmakerRating, true);
            itr->ModifyPersonalRating(mod, GetType());

            // update matchmaker rating
            itr->ModifyMatchmakerRating(matchmakerRatingChange, Type);

            // update personal stats
            it->second[slot].WeekGames   += 1;
            it->second[slot].SeasonGames += 1;
            it->second[slot].SeasonWins  += 1;
            it->second[slot].WeekWins    += 1;
            return;
        }
    }
}

void ArenaTeam::SaveToDB(bool forceMemberSave)
{
    // Save member stats to db
    // Called after a match has ended or when calculating arena_points

    CharacterDatabaseTransaction trans = CharacterDatabase.BeginTransaction();

    const uint8 slot = GetSlot();

    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        auto it = sArenaTeamMgr->GetPersonalArenaTeams().find(itr->Guid);
        if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
            continue;

        if (it->second[slot].WeekGames == 0 && !forceMemberSave)
            continue;

        // Update team's rank, start with rank 1 and increase until no team with more rating was found
        it->second[slot].Rank = 1;
        for (const auto& [_, teamStats] : sArenaTeamMgr->GetPersonalArenaTeams())
            if (teamStats.at(slot).PersonalRating > it->second[slot].PersonalRating)
                ++it->second[slot].Rank;

        CharacterDatabasePreparedStatement* stmt = CharacterDatabase.GetPreparedStatement(CHAR_REP_CHARACTER_ARENA_STATS);
        stmt->setUInt64(0, itr->Guid.GetCounter());
        stmt->setUInt8(1, slot);
        stmt->setUInt16(2, it->second[slot].WeekGames);
        stmt->setUInt16(3, it->second[slot].WeekWins);
        stmt->setUInt16(4, it->second[slot].SeasonGames);
        stmt->setUInt16(5, it->second[slot].SeasonWins);
        stmt->setUInt16(6, it->second[slot].PersonalRating);
        stmt->setUInt16(7, it->second[slot].MatchMakerRating);
        stmt->setUInt16(8, it->second[slot].Rank);

        trans->Append(stmt);
    }

    CharacterDatabase.CommitTransaction(trans);
}

void ArenaTeam::FinishWeek()
{
    const uint8 slot = GetSlot();

    // Reset member stats
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
    {
        auto it = sArenaTeamMgr->GetPersonalArenaTeams().find(itr->Guid);
        if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
            continue;

        it->second[slot].WeekGames = 0;
        it->second[slot].WeekWins  = 0;
    }
}

void ArenaTeam::CreateTeam(Player* player)
{
    ArenaTeamMember member;
    member.Guid       = player->GetGUID();
    member.Name       = player->GetName();
    member.Class      = player->GetClass();

    // if this is the first time a player is queueing create their team
    const auto& it = sArenaTeamMgr->GetPersonalArenaTeams().find(player->GetGUID());
    if (it == sArenaTeamMgr->GetPersonalArenaTeams().end())
    {
        PersonalArenaStats stats;
        stats.PersonalRating   = sWorld->getIntConfig(CONFIG_ARENA_START_PERSONAL_RATING);
        stats.MatchMakerRating = sWorld->getIntConfig(CONFIG_ARENA_START_MATCHMAKER_RATING);

        sArenaTeamMgr->AddPersonalArenaTeam(player, GetSlotByType(Type), stats);
    }

    Members.push_back(std::move(member));
}

bool ArenaTeam::IsFighting() const
{
    for (MemberList::const_iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (Player* player = ObjectAccessor::FindPlayer(itr->Guid))
            if (player->GetMap()->IsBattleArena())
                return true;

    return false;
}

ArenaTeamMember* ArenaTeam::GetMember(ObjectGuid guid)
{
    for (MemberList::iterator itr = Members.begin(); itr != Members.end(); ++itr)
        if (itr->Guid == guid)
            return &(*itr);

    return nullptr;
}
