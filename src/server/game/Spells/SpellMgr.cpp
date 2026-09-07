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

#include "SpellMgr.h"
#include "BattlefieldMgr.h"
#include "BattlegroundMgr.h"
#include "BattlePetMgr.h"
#include "Chat.h"
#include "Containers.h"
#include "DB2Stores.h"
#include "DatabaseEnv.h"
#include "LanguageMgr.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "Player.h"
#include "SharedDefines.h"
#include "Spell.h"
#include "SpellAuraDefines.h"
#include "SpellInfo.h"
#include <G3D/g3dmath.h>
#include <unordered_set>
#include <boost/multi_index_container.hpp>
#include <boost/multi_index/composite_key.hpp>
#include <boost/multi_index/hashed_index.hpp>
#include <boost/multi_index/member.hpp>

namespace
{
    struct SpellIdDifficultyIndex;
    struct SpellIdIndex;

    boost::multi_index::multi_index_container<
        SpellInfo,
        boost::multi_index::indexed_by<
            boost::multi_index::hashed_unique<
                boost::multi_index::tag<SpellIdDifficultyIndex>,
                boost::multi_index::composite_key<
                    SpellInfo,
                    boost::multi_index::member<SpellInfo, uint32 const, &SpellInfo::Id>,
                    boost::multi_index::member<SpellInfo, Difficulty const, &SpellInfo::Difficulty>
                >
            >,
            boost::multi_index::hashed_non_unique<
                boost::multi_index::tag<SpellIdIndex>,
                boost::multi_index::member<SpellInfo, uint32 const, &SpellInfo::Id>
            >
        >
    > mSpellInfoMap;

    class ServersideSpellName
    {
    public:
        explicit ServersideSpellName(uint32 id, std::string name) : _nameStorage(std::move(name))
        {
            Name.ID = id;
            InitPointers();
        }

        ServersideSpellName(ServersideSpellName const& right) : _nameStorage(right._nameStorage)
        {
            Name.ID = right.Name.ID;
            InitPointers();
        }

        ServersideSpellName(ServersideSpellName&& right) noexcept : _nameStorage(std::move(right._nameStorage))
        {
            Name.ID = right.Name.ID;
            InitPointers();
            right.InitPointers();
        }

        SpellNameEntry Name;

    private:
        void InitPointers()
        {
            std::fill(std::begin(Name.Name.Str), std::end(Name.Name.Str), _nameStorage.c_str());
        }

        std::string _nameStorage;
    };

    std::vector<ServersideSpellName> mServersideSpellNames;

    std::unordered_map<std::pair<uint32, Difficulty>, SpellProcEntry> mSpellProcMap;
}

PetFamilySpellsStore sPetFamilySpellsStore;

bool IsPrimaryProfessionSkill(uint32 skill)
{
    SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(skill);
    return pSkill && pSkill->CategoryID == SKILL_CATEGORY_PROFESSION && !pSkill->ParentSkillLineID;
}

bool IsWeaponSkill(uint32 skill)
{
    SkillLineEntry const* pSkill = sSkillLineStore.LookupEntry(skill);
    return pSkill && pSkill->CategoryID == SKILL_CATEGORY_WEAPON;
}

bool IsPartOfSkillLine(uint32 skillId, uint32 spellId)
{
    SkillLineAbilityMapBounds skillBounds = sSpellMgr->GetSkillLineAbilityMapBounds(spellId);
    for (SkillLineAbilityMap::const_iterator itr = skillBounds.first; itr != skillBounds.second; ++itr)
        if (itr->second->SkillLine == int32(skillId))
            return true;

    return false;
}

SpellMgr::SpellMgr() { }

SpellMgr::~SpellMgr()
{
    UnloadSpellInfoStore();
}

SpellMgr* SpellMgr::instance()
{
    static SpellMgr instance;
    return &instance;
}

/// Some checks for spells, to prevent adding deprecated/broken spells for trainers, spell book, etc
bool SpellMgr::IsSpellValid(SpellInfo const* spellInfo, Player* player, bool msg)
{
    if (!spellInfo)
        return false;

    auto report = [player, msg](std::string const& text)
    {
        if (!msg)
            return;
        if (player)
            ChatHandler(player->GetSession()).SendSysMessage(text);
        else
            TC_LOG_ERROR("sql.sql", "{}", text);
    };

    struct ValidationFrame
    {
        SpellInfo const* Spell;
        size_t NextEffect = 0;
        bool CheckReagents = false;
    };
    // Player login, default skills and quest rewards all validate learned
    // spells here. Cyclic or very deep learn-spell data must not recurse on
    // the C++ stack. A completed shared dependency only needs checking once.
    std::vector<ValidationFrame> pending{ { spellInfo } };
    std::unordered_map<SpellInfo const*, bool> checked{ { spellInfo, false } };
    while (!pending.empty())
    {
        ValidationFrame& frame = pending.back();
        SpellInfo const* current = frame.Spell;
        if (frame.NextEffect == current->GetEffects().size())
        {
            if (frame.CheckReagents)
                for (int32 reagent : current->Reagent)
                    if (reagent > 0 && !sObjectMgr->GetItemTemplate(reagent))
                    {
                        report(Trinity::StringFormat("Craft spell {} refers to missing reagent {}", current->Id, reagent));
                        return false;
                    }
            checked[current] = true;
            pending.pop_back();
            continue;
        }

        SpellEffectInfo const& effect = current->GetEffects()[frame.NextEffect++];
        switch (effect.Effect)
        {
            case SPELL_EFFECT_CREATE_ITEM:
            case SPELL_EFFECT_CREATE_LOOT:
                if ((!effect.ItemType && !current->IsLootCrafting()) ||
                    (effect.ItemType && !sObjectMgr->GetItemTemplate(effect.ItemType)))
                {
                    report(Trinity::StringFormat("Craft spell {} has invalid created item {}", current->Id, effect.ItemType));
                    return false;
                }
                frame.CheckReagents = true;
                break;
            case SPELL_EFFECT_LEARN_SPELL:
            {
                SpellInfo const* learned = sSpellMgr->GetSpellInfo(effect.TriggerSpell, DIFFICULTY_NONE);
                if (!learned)
                {
                    report(Trinity::StringFormat("Spell {} learns missing spell {}", current->Id, effect.TriggerSpell));
                    return false;
                }
                auto [state, inserted] = checked.emplace(learned, false);
                if (inserted)
                    pending.push_back({ learned });
                else if (!state->second)
                {
                    report(Trinity::StringFormat("Spell {} learns spell {}, forming a dependency cycle", current->Id, effect.TriggerSpell));
                    return false;
                }
                break;
            }
            default:
                break;
        }
    }
    return true;
}

SpellChainNode const* SpellMgr::GetSpellChainNode(uint32 spell_id) const
{
    SpellChainMap::const_iterator itr = mSpellChains.find(spell_id);
    if (itr == mSpellChains.end())
        return nullptr;

    return &itr->second;
}

uint32 SpellMgr::GetFirstSpellInChain(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        return node->first->Id;

    return spell_id;
}

uint32 SpellMgr::GetLastSpellInChain(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        return node->last->Id;

    return spell_id;
}

uint32 SpellMgr::GetNextSpellInChain(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        if (node->next)
            return node->next->Id;

    return 0;
}

uint32 SpellMgr::GetPrevSpellInChain(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        if (node->prev)
            return node->prev->Id;

    return 0;
}

uint8 SpellMgr::GetSpellRank(uint32 spell_id) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
        return node->rank;

    return 0;
}

uint32 SpellMgr::GetSpellWithRank(uint32 spell_id, uint32 rank, bool strict) const
{
    if (SpellChainNode const* node = GetSpellChainNode(spell_id))
    {
        if (rank != node->rank)
            return GetSpellWithRank(node->rank < rank ? node->next->Id : node->prev->Id, rank, strict);
    }
    else if (strict && rank > 1)
        return 0;
    return spell_id;
}

Trinity::IteratorPair<SpellRequiredMap::const_iterator> SpellMgr::GetSpellsRequiredForSpellBounds(uint32 spell_id) const
{
    return Trinity::Containers::MapEqualRange(mSpellReq, spell_id);
}

SpellsRequiringSpellMapBounds SpellMgr::GetSpellsRequiringSpellBounds(uint32 spell_id) const
{
    return mSpellsReqSpell.equal_range(spell_id);
}

bool SpellMgr::IsSpellRequiringSpell(uint32 spellid, uint32 req_spellid) const
{
    SpellsRequiringSpellMapBounds spellsRequiringSpell = GetSpellsRequiringSpellBounds(req_spellid);
    for (SpellsRequiringSpellMap::const_iterator itr = spellsRequiringSpell.first; itr != spellsRequiringSpell.second; ++itr)
    {
        if (itr->second == spellid)
            return true;
    }
    return false;
}

SpellLearnSkillNode const* SpellMgr::GetSpellLearnSkill(uint32 spell_id) const
{
    SpellLearnSkillMap::const_iterator itr = mSpellLearnSkills.find(spell_id);
    if (itr != mSpellLearnSkills.end())
        return &itr->second;
    else
        return nullptr;
}

SpellLearnSpellMapBounds SpellMgr::GetSpellLearnSpellMapBounds(uint32 spell_id) const
{
    return mSpellLearnSpells.equal_range(spell_id);
}

bool SpellMgr::IsSpellLearnSpell(uint32 spell_id) const
{
    return mSpellLearnSpells.find(spell_id) != mSpellLearnSpells.end();
}

bool SpellMgr::IsSpellLearnToSpell(uint32 spell_id1, uint32 spell_id2) const
{
    SpellLearnSpellMapBounds bounds = GetSpellLearnSpellMapBounds(spell_id1);
    for (SpellLearnSpellMap::const_iterator i = bounds.first; i != bounds.second; ++i)
        if (i->second.Spell == spell_id2)
            return true;
    return false;
}

SpellTargetPosition const* SpellMgr::GetSpellTargetPosition(uint32 spell_id, SpellEffIndex effIndex) const
{
    SpellTargetPositionMap::const_iterator itr = mSpellTargetPositions.find(std::make_pair(spell_id, effIndex));
    if (itr != mSpellTargetPositions.end())
        return &itr->second;
    return nullptr;
}

SpellSpellGroupMapBounds SpellMgr::GetSpellSpellGroupMapBounds(uint32 spell_id) const
{
    spell_id = GetFirstSpellInChain(spell_id);
    return mSpellSpellGroup.equal_range(spell_id);
}

bool SpellMgr::IsSpellMemberOfSpellGroup(uint32 spellid, SpellGroup groupid) const
{
    SpellSpellGroupMapBounds spellGroup = GetSpellSpellGroupMapBounds(spellid);
    for (SpellSpellGroupMap::const_iterator itr = spellGroup.first; itr != spellGroup.second; ++itr)
    {
        if (itr->second == groupid)
            return true;
    }
    return false;
}

SpellGroupSpellMapBounds SpellMgr::GetSpellGroupSpellMapBounds(SpellGroup group_id) const
{
    return mSpellGroupSpell.equal_range(group_id);
}

void SpellMgr::GetSetOfSpellsInSpellGroup(SpellGroup group_id, std::set<uint32>& foundSpells) const
{
    std::set<SpellGroup> usedGroups;
    GetSetOfSpellsInSpellGroup(group_id, foundSpells, usedGroups);
}

void SpellMgr::GetSetOfSpellsInSpellGroup(SpellGroup group_id, std::set<uint32>& foundSpells, std::set<SpellGroup>& usedGroups) const
{
    if (usedGroups.find(group_id) != usedGroups.end())
        return;
    usedGroups.insert(group_id);

    SpellGroupSpellMapBounds groupSpell = GetSpellGroupSpellMapBounds(group_id);
    for (SpellGroupSpellMap::const_iterator itr = groupSpell.first; itr != groupSpell.second; ++itr)
    {
        if (itr->second < 0)
        {
            SpellGroup currGroup = (SpellGroup)abs(itr->second);
            GetSetOfSpellsInSpellGroup(currGroup, foundSpells, usedGroups);
        }
        else
        {
            foundSpells.insert(itr->second);
        }
    }
}

bool SpellMgr::AddSameEffectStackRuleSpellGroups(SpellInfo const* spellInfo, uint32 auraType, int32 amount, std::map<SpellGroup, int32>& groups) const
{
    uint32 spellId = spellInfo->GetFirstRankSpell()->Id;
    auto spellGroupBounds = GetSpellSpellGroupMapBounds(spellId);
    // Find group with SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT if it belongs to one
    for (auto itr = spellGroupBounds.first; itr != spellGroupBounds.second; ++itr)
    {
        SpellGroup group = itr->second;
        auto found = mSpellSameEffectStack.find(group);
        if (found != mSpellSameEffectStack.end())
        {
            // check auraTypes
            if (!found->second.count(auraType))
                continue;

            // Put the highest amount in the map
            auto groupItr = groups.find(group);
            if (groupItr == groups.end())
                groups.emplace(group, amount);
            else
            {
                int32 curr_amount = groups[group];
                // Take absolute value because this also counts for the highest negative aura
                if (std::abs(curr_amount) < std::abs(amount))
                    groupItr->second = amount;
            }
            // return because a spell should be in only one SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT group per auraType
            return true;
        }
    }
    // Not in a SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT group, so return false
    return false;
}

SpellGroupStackRule SpellMgr::CheckSpellGroupStackRules(SpellInfo const* spellInfo1, SpellInfo const* spellInfo2) const
{
    ASSERT(spellInfo1);
    ASSERT(spellInfo2);

    uint32 spellid_1 = spellInfo1->GetFirstRankSpell()->Id;
    uint32 spellid_2 = spellInfo2->GetFirstRankSpell()->Id;

    // find SpellGroups which are common for both spells
    SpellSpellGroupMapBounds spellGroup1 = GetSpellSpellGroupMapBounds(spellid_1);
    std::set<SpellGroup> groups;
    for (SpellSpellGroupMap::const_iterator itr = spellGroup1.first; itr != spellGroup1.second; ++itr)
    {
        if (IsSpellMemberOfSpellGroup(spellid_2, itr->second))
        {
            bool add = true;
            SpellGroupSpellMapBounds groupSpell = GetSpellGroupSpellMapBounds(itr->second);
            for (SpellGroupSpellMap::const_iterator itr2 = groupSpell.first; itr2 != groupSpell.second; ++itr2)
            {
                if (itr2->second < 0)
                {
                    SpellGroup currGroup = (SpellGroup)abs(itr2->second);
                    if (IsSpellMemberOfSpellGroup(spellid_1, currGroup) && IsSpellMemberOfSpellGroup(spellid_2, currGroup))
                    {
                        add = false;
                        break;
                    }
                }
            }
            if (add)
                groups.insert(itr->second);
        }
    }

    SpellGroupStackRule rule = SPELL_GROUP_STACK_RULE_DEFAULT;

    for (std::set<SpellGroup>::iterator itr = groups.begin(); itr!= groups.end(); ++itr)
    {
        SpellGroupStackMap::const_iterator found = mSpellGroupStack.find(*itr);
        if (found != mSpellGroupStack.end())
            rule = found->second;
        if (rule)
            break;
    }
    return rule;
}

SpellGroupStackRule SpellMgr::GetSpellGroupStackRule(SpellGroup group) const
{
    SpellGroupStackMap::const_iterator itr = mSpellGroupStack.find(group);
    if (itr != mSpellGroupStack.end())
        return itr->second;

    return SPELL_GROUP_STACK_RULE_DEFAULT;
}

SpellProcEntry const* SpellMgr::GetSpellProcEntry(SpellInfo const* spellInfo) const
{
    SpellProcEntry const* procEntry = Trinity::Containers::MapGetValuePtr(mSpellProcMap, { spellInfo->Id, spellInfo->Difficulty });
    if (procEntry)
        return procEntry;

    if (DifficultyEntry const* difficulty = sDifficultyStore.LookupEntry(spellInfo->Difficulty))
    {
        do
        {
            procEntry = Trinity::Containers::MapGetValuePtr(mSpellProcMap, { spellInfo->Id, Difficulty(difficulty->FallbackDifficultyID) });
            if (procEntry)
                return procEntry;

            difficulty = sDifficultyStore.LookupEntry(difficulty->FallbackDifficultyID);
        } while (difficulty);
    }

    return nullptr;
}

bool SpellMgr::CanSpellTriggerProcOnEvent(SpellProcEntry const& procEntry, ProcEventInfo& eventInfo)
{
    // proc type doesn't match
    if (!(eventInfo.GetTypeMask() & procEntry.ProcFlags))
        return false;

    // check XP or honor target requirement
    if (procEntry.AttributesMask & PROC_ATTR_REQ_EXP_OR_HONOR)
        if (Player* actor = eventInfo.GetActor()->ToPlayer())
            if (eventInfo.GetActionTarget() && !actor->isHonorOrXPTarget(eventInfo.GetActionTarget()))
                return false;

    // check power requirement
    if (procEntry.AttributesMask & PROC_ATTR_REQ_POWER_COST)
    {
        if (!eventInfo.GetProcSpell())
            return false;

        std::vector<SpellPowerCost> const& costs = eventInfo.GetProcSpell()->GetPowerCost();
        auto m = std::find_if(costs.begin(), costs.end(), [](SpellPowerCost const& cost) { return cost.Amount > 0; });
        if (m == costs.end())
            return false;
    }

    // always trigger for these types
    if (eventInfo.GetTypeMask() & (PROC_FLAG_HEARTBEAT | PROC_FLAG_KILL | PROC_FLAG_DEATH))
        return true;

    // do triggered cast checks
    // Do not consider autoattacks as triggered spells
    if (!(procEntry.AttributesMask & PROC_ATTR_TRIGGERED_CAN_PROC) && !(eventInfo.GetTypeMask() & AUTO_ATTACK_PROC_FLAG_MASK))
    {
        if (Spell const* spell = eventInfo.GetProcSpell())
        {
            if (spell->IsTriggered())
            {
                SpellInfo const* spellInfo = spell->GetSpellInfo();
                if (!spellInfo->HasAttribute(SPELL_ATTR3_NOT_A_PROC) &&
                    !spellInfo->HasAttribute(SPELL_ATTR2_ACTIVE_THREAT)) // SPELL_ATTR2_TRIGGERED_CAN_TRIGGER_PROC
                    return false;
            }
        }
    }

    // check school mask (if set) for other trigger types
    if (procEntry.SchoolMask && !(eventInfo.GetSchoolMask() & procEntry.SchoolMask))
        return false;

    // check spell family name/flags (if set) for spells
    if (eventInfo.GetTypeMask() & SPELL_PROC_FLAG_MASK)
    {
        if (SpellInfo const* eventSpellInfo = eventInfo.GetSpellInfo())
            if (!eventSpellInfo->IsAffected(procEntry.SpellFamilyName, procEntry.SpellFamilyMask))
                return false;

        // check spell type mask (if set)
        if (procEntry.SpellTypeMask && !(eventInfo.GetSpellTypeMask() & procEntry.SpellTypeMask))
            return false;
    }

    // check spell phase mask
    if (eventInfo.GetTypeMask() & REQ_SPELL_PHASE_PROC_FLAG_MASK)
    {
        if (!(eventInfo.GetSpellPhaseMask() & procEntry.SpellPhaseMask))
            return false;
    }

    // check hit mask (on taken hit or on done hit, but not on spell cast phase)
    if ((eventInfo.GetTypeMask() & TAKEN_HIT_PROC_FLAG_MASK) || ((eventInfo.GetTypeMask() & DONE_HIT_PROC_FLAG_MASK) && !(eventInfo.GetSpellPhaseMask() & PROC_SPELL_PHASE_CAST)))
    {
        uint32 hitMask = procEntry.HitMask;
        // get default values if hit mask not set
        if (!hitMask)
        {
            // for taken procs allow normal + critical hits by default
            if (eventInfo.GetTypeMask() & TAKEN_HIT_PROC_FLAG_MASK)
                hitMask |= PROC_HIT_NORMAL | PROC_HIT_CRITICAL;
            // for done procs allow normal + critical + absorbs by default
            else
                hitMask |= PROC_HIT_NORMAL | PROC_HIT_CRITICAL | PROC_HIT_ABSORB;
        }
        if (!(eventInfo.GetHitMask() & hitMask))
            return false;
    }

    return true;
}

SpellBonusEntry const* SpellMgr::GetSpellBonusData(uint32 spellId) const
{
    // Lookup data
    SpellBonusMap::const_iterator itr = mSpellBonusMap.find(spellId);
    if (itr != mSpellBonusMap.end())
        return &itr->second;
    // Not found, try lookup for 1 spell rank if exist
    if (uint32 rank_1 = GetFirstSpellInChain(spellId))
    {
        SpellBonusMap::const_iterator itr2 = mSpellBonusMap.find(rank_1);
        if (itr2 != mSpellBonusMap.end())
            return &itr2->second;
    }
    return nullptr;
}

SpellThreatEntry const* SpellMgr::GetSpellThreatEntry(uint32 spellID) const
{
    SpellThreatMap::const_iterator itr = mSpellThreatMap.find(spellID);
    if (itr != mSpellThreatMap.end())
        return &itr->second;
    else
    {
        uint32 firstSpell = GetFirstSpellInChain(spellID);
        itr = mSpellThreatMap.find(firstSpell);
        if (itr != mSpellThreatMap.end())
            return &itr->second;
    }
    return nullptr;
}

SkillLineAbilityMapBounds SpellMgr::GetSkillLineAbilityMapBounds(uint32 spell_id) const
{
    return mSkillLineAbilityMap.equal_range(spell_id);
}

PetAura const* SpellMgr::GetPetAura(uint32 spell_id, uint8 eff) const
{
    SpellPetAuraMap::const_iterator itr = mSpellPetAuraMap.find((spell_id<<8) + eff);
    if (itr != mSpellPetAuraMap.end())
        return &itr->second;
    else
        return nullptr;
}

SpellEnchantProcEntry const* SpellMgr::GetSpellEnchantProcEvent(uint32 enchId) const
{
    SpellEnchantProcEventMap::const_iterator itr = mSpellEnchantProcEventMap.find(enchId);
    if (itr != mSpellEnchantProcEventMap.end())
        return &itr->second;
    return nullptr;
}

bool SpellMgr::IsArenaAllowedEnchancment(uint32 ench_id) const
{
    if (SpellItemEnchantmentEntry const* enchantment = sSpellItemEnchantmentStore.LookupEntry(ench_id))
        return enchantment->GetFlags().HasFlag(SpellItemEnchantmentFlags::AllowEnteringArena);

    return false;
}

std::vector<int32> const* SpellMgr::GetSpellLinked(SpellLinkedType type, uint32 spell_id) const
{
    return Trinity::Containers::MapGetValuePtr(mSpellLinkedMap, { type, spell_id });
}

PetLevelupSpellSet const* SpellMgr::GetPetLevelupSpellList(uint32 petFamily) const
{
    PetLevelupSpellMap::const_iterator itr = mPetLevelupSpellMap.find(petFamily);
    if (itr != mPetLevelupSpellMap.end())
        return &itr->second;
    else
        return nullptr;
}

PetDefaultSpellsEntry const* SpellMgr::GetPetDefaultSpellsEntry(int32 id) const
{
    PetDefaultSpellsMap::const_iterator itr = mPetDefaultSpellsMap.find(id);
    if (itr != mPetDefaultSpellsMap.end())
        return &itr->second;
    return nullptr;
}

SpellAreaMapBounds SpellMgr::GetSpellAreaMapBounds(uint32 spell_id) const
{
    return mSpellAreaMap.equal_range(spell_id);
}

SpellAreaForQuestMapBounds SpellMgr::GetSpellAreaForQuestMapBounds(uint32 quest_id) const
{
    return mSpellAreaForQuestMap.equal_range(quest_id);
}

SpellAreaForQuestMapBounds SpellMgr::GetSpellAreaForQuestEndMapBounds(uint32 quest_id) const
{
    return mSpellAreaForQuestEndMap.equal_range(quest_id);
}

SpellAreaForAuraMapBounds SpellMgr::GetSpellAreaForAuraMapBounds(uint32 spell_id) const
{
    return mSpellAreaForAuraMap.equal_range(spell_id);
}

SpellAreaForAreaMapBounds SpellMgr::GetSpellAreaForAreaMapBounds(uint32 area_id) const
{
    return mSpellAreaForAreaMap.equal_range(area_id);
}

SpellInfo const* SpellMgr::GetSpellInfo(uint32 spellId, Difficulty difficulty) const
{
    auto itr = mSpellInfoMap.find(boost::make_tuple(spellId, difficulty));
    if (itr != mSpellInfoMap.end())
        return &*itr;

    if (DifficultyEntry const* difficultyEntry = sDifficultyStore.LookupEntry(difficulty))
    {
        do
        {
            itr = mSpellInfoMap.find(boost::make_tuple(spellId, Difficulty(difficultyEntry->FallbackDifficultyID)));
            if (itr != mSpellInfoMap.end())
                return &*itr;

            difficultyEntry = sDifficultyStore.LookupEntry(difficultyEntry->FallbackDifficultyID);
        } while (difficultyEntry);
    }

    return nullptr;
}

auto _GetSpellInfo(uint32 spellId)
{
    return Trinity::Containers::MakeIteratorPair(mSpellInfoMap.get<SpellIdIndex>().equal_range(spellId));
}

void SpellMgr::ForEachSpellInfo(std::function<void(SpellInfo const*)> callback)
{
    for (SpellInfo const& spellInfo : mSpellInfoMap)
        callback(&spellInfo);
}

void SpellMgr::ForEachSpellInfoDifficulty(uint32 spellId, std::function<void(SpellInfo const*)> callback)
{
    for (SpellInfo const& spellInfo : _GetSpellInfo(spellId))
        callback(&spellInfo);
}

bool SpellArea::IsFitToRequirements(Player const* player, uint32 newZone, uint32 newArea) const
{
    if (gender != GENDER_NONE)                   // is not expected gender
        if (!player || gender != player->GetNativeGender())
            return false;

    if (!raceMask.IsEmpty())                     // is not expected race
        if (!player || !raceMask.HasRace(player->GetRace()))
            return false;

    if (areaId)                                  // is not in expected zone
        if (newZone != areaId && newArea != areaId)
            return false;

    if (questStart)                              // is not in expected required quest state
        if (!player || (((1 << player->GetQuestStatus(questStart)) & questStartStatus) == 0))
            return false;

    if (questEnd)                                // is not in expected forbidden quest state
        if (!player || (((1 << player->GetQuestStatus(questEnd)) & questEndStatus) == 0))
            return false;

    if (auraSpell)                               // does not have expected aura
        if (!player || (auraSpell > 0 && !player->HasAura(auraSpell)) || (auraSpell < 0 && player->HasAura(-auraSpell)))
            return false;

    if (player)
    {
        if (Battleground* bg = player->GetBattleground())
            return bg->IsSpellAllowed(spellId, player);
    }

    // Extra conditions
    switch (spellId)
    {
        case 58600: // No fly Zone - Dalaran
        {
            if (!player)
                return false;
            if (!player->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) && !player->HasAuraType(SPELL_AURA_FLY))
                return false;
            break;
        }
        case 58730: // No fly Zone - Wintergrasp
        {
            if (!player)
                return false;

            Battlefield* Bf = sBattlefieldMgr->GetBattlefieldToZoneId(player->GetMap(), player->GetZoneId());
            if (!Bf || Bf->CanFlyIn() || (!player->HasAuraType(SPELL_AURA_MOD_INCREASE_MOUNTED_FLIGHT_SPEED) && !player->HasAuraType(SPELL_AURA_FLY)))
                return false;
            break;
        }
        case 56618: // Horde Controls Factory Phase Shift
        case 56617: // Alliance Controls Factory Phase Shift
        {
            if (!player)
                return false;

            Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(player->GetMap(), player->GetZoneId());

            if (!bf || bf->GetTypeId() != BATTLEFIELD_WG)
                return false;

            // team that controls the workshop in the specified area
            uint32 team = bf->GetData(newArea);

            if (team == TEAM_HORDE)
                return spellId == 56618;
            else if (team == TEAM_ALLIANCE)
                return spellId == 56617;
            break;
        }
        case 57940: // Essence of Wintergrasp - Northrend
        case 58045: // Essence of Wintergrasp - Wintergrasp
        {
            if (!player)
                return false;

            if (Battlefield* battlefieldWG = sBattlefieldMgr->GetBattlefieldByBattleId(player->GetMap(), BATTLEFIELD_BATTLEID_WG))
                return battlefieldWG->IsEnabled() && (player->GetTeamId() == battlefieldWG->GetDefenderTeam()) && !battlefieldWG->IsWarTime();
            break;
        }
        case 74411: // Battleground - Dampening
        {
            if (!player)
                return false;

            if (Battlefield* bf = sBattlefieldMgr->GetBattlefieldToZoneId(player->GetMap(), player->GetZoneId()))
                return bf->IsWarTime();
            break;
        }

    }

    return true;
}

void SpellMgr::UnloadSpellInfoChains()
{
    for (SpellChainMap::iterator itr = mSpellChains.begin(); itr != mSpellChains.end(); ++itr)
        for (SpellInfo const& spellInfo : _GetSpellInfo(itr->first))
            const_cast<SpellInfo&>(spellInfo).ChainEntry = nullptr;

    mSpellChains.clear();
}

void SpellMgr::LoadSpellTalentRanks()
{
    // cleanup core data before reload - remove reference to ChainNode from SpellInfo
    UnloadSpellInfoChains();

    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
    {
        TalentEntry const* talentInfo = sTalentStore.LookupEntry(i);
        if (!talentInfo)
            continue;

        SpellInfo const* lastSpell = nullptr;
        for (uint8 rank = MAX_TALENT_RANK - 1; rank > 0; --rank)
        {
            if (talentInfo->SpellRank[rank])
            {
                lastSpell = GetSpellInfo(talentInfo->SpellRank[rank], DIFFICULTY_NONE);
                break;
            }
        }

        if (!lastSpell)
            continue;

        SpellInfo const* firstSpell = GetSpellInfo(talentInfo->SpellRank[0], DIFFICULTY_NONE);
        if (!firstSpell)
        {
            TC_LOG_ERROR("spells", "SpellMgr::LoadSpellTalentRanks: First Rank Spell {} for TalentEntry {} does not exist.", talentInfo->SpellRank[0], i);
            continue;
        }

        SpellInfo const* prevSpell = nullptr;
        for (uint8 rank = 0; rank < MAX_TALENT_RANK; ++rank)
        {
            uint32 spellId = talentInfo->SpellRank[rank];
            if (!spellId)
                break;

            SpellInfo const* currentSpell = GetSpellInfo(spellId, DIFFICULTY_NONE);
            if (!currentSpell)
            {
                TC_LOG_ERROR("spells", "SpellMgr::LoadSpellTalentRanks: Spell {} (Rank: {}) for TalentEntry {} does not exist.", spellId, rank + 1, i);
                break;
            }

            SpellChainNode node;
            node.first = firstSpell;
            node.last = lastSpell;
            node.rank = rank + 1;

            node.prev = prevSpell;
            node.next = node.rank < MAX_TALENT_RANK ? GetSpellInfo(talentInfo->SpellRank[node.rank], DIFFICULTY_NONE) : nullptr;

            mSpellChains[spellId] = node;

            for (SpellInfo const& difficultyInfo : _GetSpellInfo(spellId))
                const_cast<SpellInfo&>(difficultyInfo).ChainEntry = &mSpellChains[spellId];

            prevSpell = currentSpell;
        }
    }
}

void SpellMgr::LoadSpellRanks()
{
    // cleanup data and load spell ranks for talents from dbc
    LoadSpellTalentRanks();

    uint32 oldMSTime = getMSTime();

    // Alistar: First we load from DB then from sSkillLineAbilityStore
    {
        //                                                     0             1       2
        QueryResult result = WorldDatabase.Query("SELECT first_spell_id, spell_id, `rank` from spell_ranks ORDER BY first_spell_id, `rank`");

        if (!result)
        {
            TC_LOG_INFO("server.loading", ">> Loaded 0 spell rank records. DB table `spell_ranks` is empty.");
            return;
        }

        bool finished = false;

        do
        {
            // spellid, rank
            std::list < std::pair < int32, int32 > > rankChain;
            int32 currentSpell = -1;
            int32 lastSpell = -1;

            // fill one chain
            while (currentSpell == lastSpell && !finished)
            {
                Field* fields = result->Fetch();

                currentSpell = fields[0].GetUInt32();
                if (lastSpell == -1)
                    lastSpell = currentSpell;
                uint32 spell_id = fields[1].GetUInt32();
                uint32 rank = fields[2].GetUInt8();

                // don't drop the row if we're moving to the next rank
                if (currentSpell == lastSpell)
                {
                    rankChain.push_back(std::make_pair(spell_id, rank));
                    if (!result->NextRow())
                        finished = true;
                }
                else
                    break;
            }
            // check if chain is made with valid first spell
            SpellInfo const* first = GetSpellInfo(lastSpell, DIFFICULTY_NONE);
            if (!first)
            {
                TC_LOG_ERROR("sql.sql", "The spell rank identifier(first_spell_id) {} listed in `spell_ranks` does not exist!", lastSpell);
                continue;
            }
            // check if chain is long enough
            if (rankChain.size() < 2)
            {
                TC_LOG_ERROR("sql.sql", "There is only 1 spell rank for identifier(first_spell_id) {} in `spell_ranks`, entry is not needed!", lastSpell);
                continue;
            }
            int32 curRank = 0;
            bool valid = true;
            // check spells in chain
            for (std::list<std::pair<int32, int32> >::iterator itr = rankChain.begin(); itr != rankChain.end(); ++itr)
            {
                SpellInfo const* spell = GetSpellInfo(itr->first, DIFFICULTY_NONE);
                if (!spell)
                {
                    TC_LOG_ERROR("sql.sql", "The spell {} (rank {}) listed in `spell_ranks` for chain {} does not exist!", itr->first, itr->second, lastSpell);
                    valid = false;
                    break;
                }
                ++curRank;
                if (itr->second != curRank)
                {
                    TC_LOG_ERROR("sql.sql", "The spell {} (rank {}) listed in `spell_ranks` for chain {} does not have a proper rank value (should be {})!", itr->first, itr->second, lastSpell, curRank);
                    valid = false;
                    break;
                }
            }
            if (!valid)
                continue;
            int32 prevRank = 0;
            // insert the chain
            std::list<std::pair<int32, int32> >::iterator itr = rankChain.begin();
            do
            {
                int32 addedSpell = itr->first;

                mSpellChains[addedSpell].first = GetSpellInfo(lastSpell, DIFFICULTY_NONE);
                mSpellChains[addedSpell].last = GetSpellInfo(rankChain.back().first, DIFFICULTY_NONE);
                mSpellChains[addedSpell].rank = itr->second;
                mSpellChains[addedSpell].prev = GetSpellInfo(prevRank, DIFFICULTY_NONE);

                for (SpellInfo const& difficultyInfo : _GetSpellInfo(addedSpell))
                    const_cast<SpellInfo&>(difficultyInfo).ChainEntry = &mSpellChains[addedSpell];

                prevRank = addedSpell;
                ++itr;

                if (itr == rankChain.end())
                {
                    mSpellChains[addedSpell].next = nullptr;
                    break;
                }
                else
                    mSpellChains[addedSpell].next = GetSpellInfo(itr->first, DIFFICULTY_NONE);
            } while (true);
        } while (!finished);
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} spell rank records in {} ms", uint32(mSpellChains.size()), GetMSTimeDiffToNow(oldMSTime));
}

static bool WouldCreateSpellRequirementCycle(SpellRequiredMap const& requirements, uint32 spellId, uint32 requiredSpellId, SpellMgr const& spellMgr)
{
    // Player::LearnSpell/RemoveSpell traverse these dependencies recursively.
    // Reject the edge that closes a cycle rather than exposing that graph to
    // every bot spell reset (or normal player spell learning/unlearning).
    std::vector<uint32> pending{ requiredSpellId };
    std::unordered_set<uint32> visited;
    while (!pending.empty())
    {
        uint32 current = pending.back();
        pending.pop_back();
        if (current == spellId)
            return true;
        if (!visited.insert(current).second)
            continue;

        // Removing a lower rank also removes known higher ranks. Include the
        // reverse rank edge when checking requirements, or a mixed rank/required
        // cycle could still get through an otherwise acyclic spell_required table.
        if (uint32 previous = spellMgr.GetPrevSpellInChain(current))
            pending.push_back(previous);

        auto bounds = requirements.equal_range(current);
        for (auto it = bounds.first; it != bounds.second; ++it)
            pending.push_back(it->second);
    }
    return false;
}

void SpellMgr::LoadSpellRequired()
{
    uint32 oldMSTime = getMSTime();

    mSpellsReqSpell.clear();                                   // need for reload case
    mSpellReq.clear();                                         // need for reload case

    //                                                   0        1
    QueryResult result = WorldDatabase.Query("SELECT spell_id, req_spell from spell_required");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell required records. DB table `spell_required` is empty.");

        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 spell_id = fields[0].GetUInt32();
        uint32 spell_req = fields[1].GetUInt32();

        // check if chain is made with valid first spell
        SpellInfo const* spell = GetSpellInfo(spell_id, DIFFICULTY_NONE);
        if (!spell)
        {
            TC_LOG_ERROR("sql.sql", "spell_id {} in `spell_required` table could not be found in dbc, skipped.", spell_id);
            continue;
        }

        SpellInfo const* reqSpell = GetSpellInfo(spell_req, DIFFICULTY_NONE);
        if (!reqSpell)
        {
            TC_LOG_ERROR("sql.sql", "req_spell {} in `spell_required` table could not be found in dbc, skipped.", spell_req);
            continue;
        }

        if (spell->IsRankOf(reqSpell))
        {
            TC_LOG_ERROR("sql.sql", "req_spell {} and spell_id {} in `spell_required` table are ranks of the same spell, entry not needed, skipped.", spell_req, spell_id);
            continue;
        }

        if (IsSpellRequiringSpell(spell_id, spell_req))
        {
            TC_LOG_ERROR("sql.sql", "Duplicate entry of req_spell {} and spell_id {} in `spell_required`, skipped.", spell_req, spell_id);
            continue;
        }

        if (WouldCreateSpellRequirementCycle(mSpellReq, spell_id, spell_req, *this))
        {
            TC_LOG_ERROR("sql.sql", "spell_id {} and req_spell {} in `spell_required` would create a dependency cycle; skipped.", spell_id, spell_req);
            continue;
        }

        mSpellReq.insert (std::pair<uint32, uint32>(spell_id, spell_req));
        mSpellsReqSpell.insert (std::pair<uint32, uint32>(spell_req, spell_id));
        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} spell required records in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

}

void SpellMgr::LoadSpellLearnSkills()
{
    uint32 oldMSTime = getMSTime();

    mSpellLearnSkills.clear();                              // need for reload case

    // search auto-learned skills and add its to map also for use in unlearn spells/talents
    uint32 dbc_count = 0;
    for (SpellInfo const& entry : mSpellInfoMap)
    {
        if (entry.Difficulty != DIFFICULTY_NONE)
            continue;

        for (SpellEffectInfo const& spellEffectInfo : entry.GetEffects())
        {
            SpellLearnSkillNode dbc_node;
            switch (spellEffectInfo.Effect)
            {
                case SPELL_EFFECT_SKILL:
                    dbc_node.skill = uint16(spellEffectInfo.MiscValue);
                    dbc_node.step  = uint16(spellEffectInfo.CalcValue());
                    if (dbc_node.skill != SKILL_RIDING)
                        dbc_node.value = 1;
                    else
                        dbc_node.value = dbc_node.step * 75;
                    dbc_node.maxvalue = dbc_node.step * 75;
                    break;
                case SPELL_EFFECT_DUAL_WIELD:
                    dbc_node.skill = SKILL_DUAL_WIELD;
                    dbc_node.step = 1;
                    dbc_node.value = 1;
                    dbc_node.maxvalue = 1;
                    break;
                default:
                    continue;
            }

            mSpellLearnSkills[entry.Id] = dbc_node;
            ++dbc_count;
            break;
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} Spell Learn Skills from DBC in {} ms", dbc_count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellLearnSpells()
{
    uint32 oldMSTime = getMSTime();

    mSpellLearnSpells.clear();                              // need for reload case

    //                                                  0      1        2
    QueryResult result = WorldDatabase.Query("SELECT entry, SpellID, Active FROM spell_learn_spell");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell learn spells. DB table `spell_learn_spell` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 spell_id = fields[0].GetUInt32();

        SpellLearnSpellNode node;
        node.Spell       = fields[1].GetUInt32();
        node.OverridesSpell = 0;
        node.Active      = fields[2].GetBool();
        node.AutoLearned = false;

        SpellInfo const* spellInfo = GetSpellInfo(spell_id, DIFFICULTY_NONE);
        if (!spellInfo)
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_learn_spell` does not exist.", spell_id);
            continue;
        }

        if (!GetSpellInfo(node.Spell, DIFFICULTY_NONE))
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_learn_spell` learning non-existing spell {}.", spell_id, node.Spell);
            continue;
        }

        if (spellInfo->HasAttribute(SPELL_ATTR0_CU_IS_TALENT))
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_learn_spell` attempts learning talent spell {}, skipped.", spell_id, node.Spell);
            continue;
        }

        mSpellLearnSpells.insert(SpellLearnSpellMap::value_type(spell_id, node));

        ++count;
    } while (result->NextRow());

    // copy state loaded from db
    SpellLearnSpellMap dbSpellLearnSpells = mSpellLearnSpells;

    // search auto-learned spells and add its to map also for use in unlearn spells/talents
    uint32 dbc_count = 0;
    for (SpellInfo const& entry : mSpellInfoMap)
    {
        if (entry.Difficulty != DIFFICULTY_NONE)
            continue;

        for (SpellEffectInfo const& spellEffectInfo : entry.GetEffects())
        {
            if (spellEffectInfo.IsEffect(SPELL_EFFECT_LEARN_SPELL))
            {
                SpellLearnSpellNode dbc_node;
                dbc_node.Spell = spellEffectInfo.TriggerSpell;
                dbc_node.Active = true;                     // all dbc based learned spells is active (show in spell book or hide by client itself)
                dbc_node.OverridesSpell = 0;

                // ignore learning not existed spells (broken/outdated/or generic learnig spell 483
                if (!GetSpellInfo(dbc_node.Spell, DIFFICULTY_NONE))
                    continue;

                // talent or passive spells or skill-step spells auto-cast and not need dependent learning,
                // pet teaching spells must not be dependent learning (cast)
                // other required explicit dependent learning
                dbc_node.AutoLearned = spellEffectInfo.TargetA.GetTarget() == TARGET_UNIT_PET || entry.HasAttribute(SPELL_ATTR0_CU_IS_TALENT) || entry.IsPassive() || entry.HasEffect(SPELL_EFFECT_SKILL_STEP);

                SpellLearnSpellMapBounds db_node_bounds = dbSpellLearnSpells.equal_range(entry.Id);

                bool found = false;
                for (SpellLearnSpellMap::const_iterator itr = db_node_bounds.first; itr != db_node_bounds.second; ++itr)
                {
                    if (itr->second.Spell == dbc_node.Spell)
                    {
                        TC_LOG_ERROR("sql.sql", "The spell {} is an auto-learn spell {} in spell.dbc and the record in `spell_learn_spell` is redundant. Please update your DB.",
                            entry.Id, dbc_node.Spell);
                        found = true;
                        break;
                    }
                }

                if (!found)                                  // add new spell-spell pair if not found
                {
                    mSpellLearnSpells.insert(SpellLearnSpellMap::value_type(entry.Id, dbc_node));
                    ++dbc_count;
                }
            }
        }
    }

    for (SpellLearnSpellEntry const* spellLearnSpell : sSpellLearnSpellStore)
    {
        if (!GetSpellInfo(spellLearnSpell->SpellID, DIFFICULTY_NONE) ||
            !GetSpellInfo(spellLearnSpell->LearnSpellID, DIFFICULTY_NONE))
            continue;

        SpellLearnSpellMapBounds db_node_bounds = dbSpellLearnSpells.equal_range(spellLearnSpell->SpellID);
        bool found = false;
        for (SpellLearnSpellMap::const_iterator itr = db_node_bounds.first; itr != db_node_bounds.second; ++itr)
        {
            if (int32(itr->second.Spell) == spellLearnSpell->LearnSpellID)
            {
                TC_LOG_ERROR("sql.sql", "Found redundant record (entry: {}, SpellID: {}) in `spell_learn_spell`, spell added automatically from SpellLearnSpell.db2", spellLearnSpell->SpellID, spellLearnSpell->LearnSpellID);
                found = true;
                break;
            }
        }

        if (found)
            continue;

        // Check if it is already found in Spell.dbc, ignore silently if yes
        SpellLearnSpellMapBounds dbc_node_bounds = GetSpellLearnSpellMapBounds(spellLearnSpell->SpellID);
        found = false;
        for (SpellLearnSpellMap::const_iterator itr = dbc_node_bounds.first; itr != dbc_node_bounds.second; ++itr)
        {
            if (int32(itr->second.Spell) == spellLearnSpell->LearnSpellID)
            {
                found = true;
                break;
            }
        }

        if (found)
            continue;

        SpellLearnSpellNode dbcLearnNode;
        dbcLearnNode.Spell = spellLearnSpell->LearnSpellID;
        dbcLearnNode.OverridesSpell = spellLearnSpell->OverridesSpellID;
        dbcLearnNode.Active = true;
        dbcLearnNode.AutoLearned = false;

        mSpellLearnSpells.insert(SpellLearnSpellMap::value_type(spellLearnSpell->SpellID, dbcLearnNode));
        ++dbc_count;
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} spell learn spells, {} found in Spell.dbc in {} ms", count, dbc_count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellTargetPositions()
{
    uint32 oldMSTime = getMSTime();

    mSpellTargetPositions.clear();                                // need for reload case

    //                                               0   1            2      3          4          5          6
    QueryResult result = WorldDatabase.Query("SELECT ID, EffectIndex, MapID, PositionX, PositionY, PositionZ, Orientation FROM spell_target_position");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell target coordinates. DB table `spell_target_position` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 spellId = fields[0].GetUInt32();
        SpellEffIndex effIndex = SpellEffIndex(fields[1].GetUInt8());

        SpellTargetPosition st;

        st.target_mapId       = fields[2].GetUInt16();
        st.target_X           = fields[3].GetFloat();
        st.target_Y           = fields[4].GetFloat();
        st.target_Z           = fields[5].GetFloat();

        MapEntry const* mapEntry = sMapStore.LookupEntry(st.target_mapId);
        if (!mapEntry)
        {
            TC_LOG_ERROR("sql.sql", "Spell (Id: {}, EffectIndex: {}) is using a non-existant MapID (ID: {}).", spellId, uint32(effIndex), st.target_mapId);
            continue;
        }

        if (st.target_X == 0 && st.target_Y == 0 && st.target_Z == 0)
        {
            TC_LOG_ERROR("sql.sql", "Spell (Id: {}, EffectIndex: {}): target coordinates not provided.", spellId, uint32(effIndex));
            continue;
        }

        SpellInfo const* spellInfo = GetSpellInfo(spellId, DIFFICULTY_NONE);
        if (!spellInfo)
        {
            TC_LOG_ERROR("sql.sql", "Spell (Id: {}) listed in `spell_target_position` does not exist.", spellId);
            continue;
        }

        if (effIndex >= spellInfo->GetEffects().size())
        {
            TC_LOG_ERROR("sql.sql", "Spell (Id: {}, EffectIndex: {}) listed in `spell_target_position` does not have an effect at index {}.", spellId, uint32(effIndex), uint32(effIndex));
            continue;
        }

        if (!fields[6].IsNull())
            st.target_Orientation = fields[6].GetFloat();
        else
        {
            // target facing is in degrees for 6484 & 9268... (blizz sucks)
            if (spellInfo->GetEffect(effIndex).PositionFacing > 2 * float(M_PI))
                st.target_Orientation = spellInfo->GetEffect(effIndex).PositionFacing * float(M_PI) / 180;
            else
                st.target_Orientation = spellInfo->GetEffect(effIndex).PositionFacing;
        }

        auto hasTarget = [&](Targets target)
        {
            SpellEffectInfo const& spellEffectInfo = spellInfo->GetEffect(effIndex);
            return spellEffectInfo.TargetA.GetTarget() == target || spellEffectInfo.TargetB.GetTarget() == target;
        };

        if (hasTarget(TARGET_DEST_DB) || hasTarget(TARGET_DEST_NEARBY_ENTRY_OR_DB))
        {
            std::pair<uint32, SpellEffIndex> key = std::make_pair(spellId, effIndex);
            mSpellTargetPositions[key] = st;
            ++count;
        }
        else
        {
            TC_LOG_ERROR("sql.sql", "Spell (Id: {}, effIndex: {}) listed in `spell_target_position` does not have a target TARGET_DEST_DB (17).", spellId, uint32(effIndex));
            continue;
        }

    } while (result->NextRow());

    /*
    // Check all spells
    for (uint32 i = 1; i < GetSpellInfoStoreSize(); ++i)
    {
        SpellInfo const* spellInfo = GetSpellInfo(i);
        if (!spellInfo)
            continue;

        for (uint8 j = 0; j < MAX_SPELL_EFFECTS; ++j)
        {
            SpellEffectInfo const* effect = spellInfo->GetEffect(j);
            if (!effect)
                continue;

            if (effect->TargetA.GetTarget() != TARGET_DEST_DB && effect->TargetB.GetTarget() != TARGET_DEST_DB)
                continue;

            if (!GetSpellTargetPosition(i, SpellEffIndex(j)))
                TC_LOG_DEBUG("spells", "Spell (Id: {}, EffectIndex: {}) does not have record in `spell_target_position`.", i, j);
        }
    }
    */

    TC_LOG_INFO("server.loading", ">> Loaded {} spell teleport coordinates in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellGroups()
{
    uint32 oldMSTime = getMSTime();

    mSpellSpellGroup.clear();                                  // need for reload case
    mSpellGroupSpell.clear();

    //                                                0     1
    QueryResult result = WorldDatabase.Query("SELECT id, spell_id FROM spell_group");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell group definitions. DB table `spell_group` is empty.");
        return;
    }

    std::set<uint32> groups;
    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 group_id = fields[0].GetUInt32();
        if (group_id <= SPELL_GROUP_DB_RANGE_MIN && group_id >= SPELL_GROUP_CORE_RANGE_MAX)
        {
            TC_LOG_ERROR("sql.sql", "SpellGroup id {} listed in `spell_group` is in core range, but is not defined in core!", group_id);
            continue;
        }
        int32 spell_id = fields[1].GetInt32();

        groups.insert(group_id);
        mSpellGroupSpell.emplace(SpellGroup(group_id), spell_id);

    } while (result->NextRow());

    for (auto itr = mSpellGroupSpell.begin(); itr!= mSpellGroupSpell.end();)
    {
        if (itr->second < 0)
        {
            if (groups.find(abs(itr->second)) == groups.end())
            {
                TC_LOG_ERROR("sql.sql", "SpellGroup id {} listed in `spell_group` does not exist", abs(itr->second));
                itr = mSpellGroupSpell.erase(itr);
            }
            else
                ++itr;
        }
        else
        {
            SpellInfo const* spellInfo = GetSpellInfo(itr->second, DIFFICULTY_NONE);
            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_group` does not exist", itr->second);
                itr = mSpellGroupSpell.erase(itr);
            }
            else if (spellInfo->GetRank() > 1)
            {
                TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_group` is not the first rank of the spell.", itr->second);
                itr = mSpellGroupSpell.erase(itr);
            }
            else
                ++itr;
        }
    }

    for (auto groupItr = groups.begin(); groupItr != groups.end(); ++groupItr)
    {
        std::set<uint32> spells;
        GetSetOfSpellsInSpellGroup(SpellGroup(*groupItr), spells);

        for (auto spellItr = spells.begin(); spellItr != spells.end(); ++spellItr)
        {
            ++count;
            mSpellSpellGroup.emplace(*spellItr, SpellGroup(*groupItr));
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} spell group definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellGroupStackRules()
{
    uint32 oldMSTime = getMSTime();

    mSpellGroupStack.clear();                                  // need for reload case
    mSpellSameEffectStack.clear();

    std::vector<uint32> sameEffectGroups;

    //                                                       0         1
    QueryResult result = WorldDatabase.Query("SELECT group_id, stack_rule FROM spell_group_stack_rules");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell group stack rules. DB table `spell_group_stack_rules` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 group_id = fields[0].GetUInt32();
        uint8 stack_rule = fields[1].GetInt8();
        if (stack_rule >= SPELL_GROUP_STACK_RULE_MAX)
        {
            TC_LOG_ERROR("sql.sql", "SpellGroupStackRule {} listed in `spell_group_stack_rules` does not exist.", stack_rule);
            continue;
        }

        auto bounds = GetSpellGroupSpellMapBounds((SpellGroup)group_id);
        if (bounds.first == bounds.second)
        {
            TC_LOG_ERROR("sql.sql", "SpellGroup id {} listed in `spell_group_stack_rules` does not exist.", group_id);
            continue;
        }

        mSpellGroupStack.emplace(SpellGroup(group_id), SpellGroupStackRule(stack_rule));

        // different container for same effect stack rules, need to check effect types
        if (stack_rule == SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT)
            sameEffectGroups.push_back(group_id);

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} spell group stack rules in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

    count = 0;
    oldMSTime = getMSTime();
    TC_LOG_INFO("server.loading", ">> Parsing SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT stack rules...");

    for (uint32 group_id : sameEffectGroups)
    {
        std::set<uint32> spellIds;
        GetSetOfSpellsInSpellGroup(SpellGroup(group_id), spellIds);

        std::unordered_set<uint32> auraTypes;

        // we have to 'guess' what effect this group corresponds to
        {
            std::unordered_multiset<uint32 /*auraName*/> frequencyContainer;

            // only waylay for the moment (shared group)
            std::vector<std::vector<uint32 /*auraName*/>> const SubGroups =
            {
                { SPELL_AURA_MOD_MELEE_HASTE, SPELL_AURA_MOD_MELEE_RANGED_HASTE, SPELL_AURA_MOD_RANGED_HASTE }
            };

            for (uint32 spellId : spellIds)
            {
                SpellInfo const* spellInfo = AssertSpellInfo(spellId, DIFFICULTY_NONE);
                for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
                {
                    if (!spellEffectInfo.IsAura())
                        continue;

                    uint32 auraName = spellEffectInfo.ApplyAuraName;
                    for (std::vector<uint32> const& subGroup : SubGroups)
                    {
                        if (std::find(subGroup.begin(), subGroup.end(), auraName) != subGroup.end())
                        {
                            // count as first aura
                            auraName = subGroup.front();
                            break;
                        }
                    }

                    frequencyContainer.insert(auraName);
                }
            }

            uint32 auraType = 0;
            size_t auraTypeCount = 0;
            for (uint32 auraName : frequencyContainer)
            {
                size_t currentCount = frequencyContainer.count(auraName);
                if (currentCount > auraTypeCount)
                {
                    auraType = auraName;
                    auraTypeCount = currentCount;
                }
            }

            for (std::vector<uint32> const& subGroup : SubGroups)
            {
                if (auraType == subGroup.front())
                {
                    auraTypes.insert(subGroup.begin(), subGroup.end());
                    break;
                }
            }

            if (auraTypes.empty())
                auraTypes.insert(auraType);
        }

        // re-check spells against guessed group
        for (uint32 spellId : spellIds)
        {
            SpellInfo const* spellInfo = AssertSpellInfo(spellId, DIFFICULTY_NONE);

            bool found = false;
            while (spellInfo)
            {
                for (uint32 auraType : auraTypes)
                {
                    if (spellInfo->HasAura(AuraType(auraType)))
                    {
                        found = true;
                        break;
                    }
                }

                if (found)
                    break;

                spellInfo = spellInfo->GetNextRankSpell();
            }

            // not found either, log error
            if (!found)
                TC_LOG_ERROR("sql.sql", "SpellId {} listed in `spell_group` with stack rule 3 does not share aura assigned for group {}", spellId, group_id);
        }

        mSpellSameEffectStack[SpellGroup(group_id)] = auraTypes;
        ++count;
    }

    TC_LOG_INFO("server.loading", ">> Parsed {} SPELL_GROUP_STACK_RULE_EXCLUSIVE_SAME_EFFECT stack rules in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellProcs()
{
    uint32 oldMSTime = getMSTime();

    mSpellProcMap.clear();                             // need for reload case

    //                                                     0           1                2                 3                 4                 5                 6
    QueryResult result = WorldDatabase.Query("SELECT SpellId, SchoolMask, SpellFamilyName, SpellFamilyMask0, SpellFamilyMask1, SpellFamilyMask2, SpellFamilyMask3, "
    //           7           8              9              10       11              12                  13              14      15        16       17
        "ProcFlags, ProcFlags2, SpellTypeMask, SpellPhaseMask, HitMask, AttributesMask, DisableEffectsMask, ProcsPerMinute, Chance, Cooldown, Charges FROM spell_proc");

    uint32 count = 0;
    if (result)
    {
        do
        {
            Field* fields = result->Fetch();

            int32 spellId = fields[0].GetInt32();

            bool allRanks = false;
            if (spellId < 0)
            {
                allRanks = true;
                spellId = -spellId;
            }

            SpellInfo const* spellInfo = GetSpellInfo(spellId, DIFFICULTY_NONE);
            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_proc` does not exist", spellId);
                continue;
            }

            if (allRanks)
            {
                if (!spellInfo->IsRanked())
                    TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_proc` with all ranks, but spell has no ranks.", spellId);

                if (spellInfo->GetFirstRankSpell()->Id != uint32(spellId))
                {
                    TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_proc` is not the first rank of the spell.", spellId);
                    continue;
                }
            }

            SpellProcEntry baseProcEntry;

            baseProcEntry.SchoolMask         = fields[1].GetUInt8();
            baseProcEntry.SpellFamilyName    = fields[2].GetUInt16();
            baseProcEntry.SpellFamilyMask[0] = fields[3].GetUInt32();
            baseProcEntry.SpellFamilyMask[1] = fields[4].GetUInt32();
            baseProcEntry.SpellFamilyMask[2] = fields[5].GetUInt32();
            baseProcEntry.SpellFamilyMask[3] = fields[6].GetUInt32();
            baseProcEntry.ProcFlags[0]       = fields[7].GetUInt32();
            baseProcEntry.ProcFlags[1]       = fields[8].GetUInt32();
            baseProcEntry.SpellTypeMask      = ProcFlagsSpellType(fields[9].GetUInt32());
            baseProcEntry.SpellPhaseMask     = ProcFlagsSpellPhase(fields[10].GetUInt32());
            baseProcEntry.HitMask            = ProcFlagsHit(fields[11].GetUInt32());
            baseProcEntry.AttributesMask     = ProcAttributes(fields[12].GetUInt32());
            baseProcEntry.DisableEffectsMask = fields[13].GetUInt32();
            baseProcEntry.ProcsPerMinute     = fields[14].GetFloat();
            baseProcEntry.Chance             = fields[15].GetFloat();
            baseProcEntry.Cooldown           = Milliseconds(fields[16].GetUInt32());
            baseProcEntry.Charges            = fields[17].GetUInt8();

            while (spellInfo)
            {
                if (mSpellProcMap.find({ spellInfo->Id, spellInfo->Difficulty }) != mSpellProcMap.end())
                {
                    TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_proc` already has its first rank in the table.", spellInfo->Id);
                    break;
                }

                SpellProcEntry procEntry = SpellProcEntry(baseProcEntry);

                // take defaults from dbcs
                if (!procEntry.ProcFlags)
                    procEntry.ProcFlags = spellInfo->ProcFlags;
                if (!procEntry.Charges)
                    procEntry.Charges = spellInfo->ProcCharges;
                if (!procEntry.Chance && !procEntry.ProcsPerMinute)
                    procEntry.Chance = float(spellInfo->ProcChance);
                if (procEntry.Cooldown == Milliseconds::zero())
                    procEntry.Cooldown = Milliseconds(spellInfo->ProcCooldown);

                // validate data
                if (procEntry.SchoolMask & ~SPELL_SCHOOL_MASK_ALL)
                    TC_LOG_ERROR("sql.sql", "`spell_proc` table entry for spellId {} has wrong `SchoolMask` set: {}", spellInfo->Id, procEntry.SchoolMask);
                if (procEntry.SpellFamilyName && !DB2Manager::IsValidSpellFamiliyName(SpellFamilyNames(procEntry.SpellFamilyName)))
                    TC_LOG_ERROR("sql.sql", "`spell_proc` table entry for spellId {} has wrong `SpellFamilyName` set: {}", spellInfo->Id, procEntry.SpellFamilyName);
                if (procEntry.Chance < 0)
                {
                    TC_LOG_ERROR("sql.sql", "`spell_proc` table entry for spellId {} has negative value in the `Chance` field", spellInfo->Id);
                    procEntry.Chance = 0;
                }
                if (procEntry.ProcsPerMinute < 0)
                {
                    TC_LOG_ERROR("sql.sql", "`spell_proc` table entry for spellId {} has negative value in the `ProcsPerMinute` field", spellInfo->Id);
                    procEntry.ProcsPerMinute = 0;
                }
                if (!procEntry.ProcFlags)
                    TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} doesn't have any `ProcFlags` value defined, proc will not be triggered.", spellInfo->Id);
                if (procEntry.SpellTypeMask & ~PROC_SPELL_TYPE_MASK_ALL)
                    TC_LOG_ERROR("sql.sql", "`spell_proc` table entry for spellId {} has wrong `SpellTypeMask` set: {}", spellInfo->Id, procEntry.SpellTypeMask);
                if (procEntry.SpellTypeMask && !(procEntry.ProcFlags & SPELL_PROC_FLAG_MASK))
                    TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} has `SpellTypeMask` value defined, but it will not be used for the defined `ProcFlags` value.", spellInfo->Id);
                if (!procEntry.SpellPhaseMask && procEntry.ProcFlags & REQ_SPELL_PHASE_PROC_FLAG_MASK)
                    TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} doesn't have any `SpellPhaseMask` value defined, but it is required for the defined `ProcFlags` value. Proc will not be triggered.", spellInfo->Id);
                if (procEntry.SpellPhaseMask & ~PROC_SPELL_PHASE_MASK_ALL)
                    TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} has wrong `SpellPhaseMask` set: {}", spellInfo->Id, procEntry.SpellPhaseMask);
                if (procEntry.SpellPhaseMask && !(procEntry.ProcFlags & REQ_SPELL_PHASE_PROC_FLAG_MASK))
                    TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} has a `SpellPhaseMask` value defined, but it will not be used for the defined `ProcFlags` value.", spellInfo->Id);
                if (!procEntry.SpellPhaseMask && !(procEntry.ProcFlags & REQ_SPELL_PHASE_PROC_FLAG_MASK) && procEntry.ProcFlags & PROC_FLAG_2_CAST_SUCCESSFUL)
                    procEntry.SpellPhaseMask = PROC_SPELL_PHASE_CAST; // set default phase for PROC_FLAG_2_CAST_SUCCESSFUL
                if (procEntry.HitMask & ~PROC_HIT_MASK_ALL)
                    TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} has wrong `HitMask` set: {}", spellInfo->Id, procEntry.HitMask);
                if (procEntry.HitMask && !(procEntry.ProcFlags & TAKEN_HIT_PROC_FLAG_MASK || (procEntry.ProcFlags & DONE_HIT_PROC_FLAG_MASK && (!procEntry.SpellPhaseMask || procEntry.SpellPhaseMask & (PROC_SPELL_PHASE_HIT | PROC_SPELL_PHASE_FINISH)))))
                    TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} has `HitMask` value defined, but it will not be used for defined `ProcFlags` and `SpellPhaseMask` values.", spellInfo->Id);
                for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
                    if ((procEntry.DisableEffectsMask & (1u << spellEffectInfo.EffectIndex)) && !spellEffectInfo.IsAura())
                        TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} has DisableEffectsMask with effect {}, but effect {} is not an aura effect", spellInfo->Id, static_cast<uint32>(spellEffectInfo.EffectIndex), static_cast<uint32>(spellEffectInfo.EffectIndex));
                if (procEntry.AttributesMask & PROC_ATTR_REQ_SPELLMOD)
                {
                    bool found = false;
                    for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
                    {
                        if (!spellEffectInfo.IsAura())
                            continue;

                        if (spellEffectInfo.ApplyAuraName == SPELL_AURA_ADD_PCT_MODIFIER || spellEffectInfo.ApplyAuraName == SPELL_AURA_ADD_FLAT_MODIFIER)
                        {
                            found = true;
                            break;
                        }
                    }

                    if (!found)
                        TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} has Attribute PROC_ATTR_REQ_SPELLMOD, but spell has no spell mods. Proc will not be triggered", spellInfo->Id);
                }
                if (procEntry.AttributesMask & ~PROC_ATTR_ALL_ALLOWED)
                {
                    TC_LOG_ERROR("sql.sql", "The `spell_proc` table entry for spellId {} has `AttributesMask` value specifying invalid attributes 0x{:02X}.", spellInfo->Id, procEntry.AttributesMask & ~PROC_ATTR_ALL_ALLOWED);
                    procEntry.AttributesMask &= PROC_ATTR_ALL_ALLOWED;
                }

                mSpellProcMap[{ spellInfo->Id, spellInfo->Difficulty }] = procEntry;

                if (allRanks)
                    spellInfo = spellInfo->GetNextRankSpell();
                else
                    break;
            }
            ++count;
        } while (result->NextRow());
    }
    else
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell proc conditions and data. DB table `spell_proc` is empty.");

    TC_LOG_INFO("server.loading", ">> Loaded {} spell proc conditions and data in {} ms", count, GetMSTimeDiffToNow(oldMSTime));

    // Define can trigger auras
    bool isTriggerAura[TOTAL_AURAS];
    // Triggered always, even from triggered spells
    bool isAlwaysTriggeredAura[TOTAL_AURAS];
    // SpellTypeMask to add to the proc
    ProcFlagsSpellType spellTypeMask[TOTAL_AURAS];

    // List of auras that CAN trigger but may not exist in spell_proc
    // in most cases needed to drop charges

    // some aura types need additional checks (eg SPELL_AURA_MECHANIC_IMMUNITY needs mechanic check)
    // see AuraEffect::CheckEffectProc
    for (uint16 i = 0; i < TOTAL_AURAS; ++i)
    {
        isTriggerAura[i] = false;
        isAlwaysTriggeredAura[i] = false;
        spellTypeMask[i] = PROC_SPELL_TYPE_MASK_ALL;
    }

    isTriggerAura[SPELL_AURA_DUMMY] = true;
    isTriggerAura[SPELL_AURA_PERIODIC_DUMMY] = true;
    isTriggerAura[SPELL_AURA_MOD_CONFUSE] = true;
    isTriggerAura[SPELL_AURA_MOD_THREAT] = true;
    isTriggerAura[SPELL_AURA_MOD_STUN] = true; // Aura does not have charges but needs to be removed on trigger
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_DONE] = true;
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_TAKEN] = true;
    isTriggerAura[SPELL_AURA_MOD_RESISTANCE] = true;
    isTriggerAura[SPELL_AURA_MOD_STEALTH] = true;
    isTriggerAura[SPELL_AURA_MOD_FEAR] = true; // Aura does not have charges but needs to be removed on trigger
    isTriggerAura[SPELL_AURA_MOD_ROOT] = true;
    isTriggerAura[SPELL_AURA_TRANSFORM] = true;
    isTriggerAura[SPELL_AURA_REFLECT_SPELLS] = true;
    isTriggerAura[SPELL_AURA_DAMAGE_IMMUNITY] = true;
    isTriggerAura[SPELL_AURA_PROC_TRIGGER_SPELL] = true;
    isTriggerAura[SPELL_AURA_PROC_TRIGGER_DAMAGE] = true;
    isTriggerAura[SPELL_AURA_MOD_CASTING_SPEED_NOT_STACK] = true;
    isTriggerAura[SPELL_AURA_SCHOOL_ABSORB] = true; // Savage Defense untested
    isTriggerAura[SPELL_AURA_MOD_POWER_COST_SCHOOL_PCT] = true;
    isTriggerAura[SPELL_AURA_MOD_POWER_COST_SCHOOL] = true;
    isTriggerAura[SPELL_AURA_REFLECT_SPELLS_SCHOOL] = true;
    isTriggerAura[SPELL_AURA_MECHANIC_IMMUNITY] = true;
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN] = true;
    isTriggerAura[SPELL_AURA_SPELL_MAGNET] = true;
    isTriggerAura[SPELL_AURA_MOD_ATTACK_POWER] = true;
    isTriggerAura[SPELL_AURA_MOD_POWER_REGEN_PERCENT] = true;
    isTriggerAura[SPELL_AURA_INTERCEPT_MELEE_RANGED_ATTACKS] = true;
    isTriggerAura[SPELL_AURA_OVERRIDE_CLASS_SCRIPTS] = true;
    isTriggerAura[SPELL_AURA_MOD_MECHANIC_RESISTANCE] = true;
    isTriggerAura[SPELL_AURA_RANGED_ATTACK_POWER_ATTACKER_BONUS] = true;
    isTriggerAura[SPELL_AURA_MOD_MELEE_HASTE] = true;
    isTriggerAura[SPELL_AURA_MOD_ATTACKER_MELEE_HIT_CHANCE] = true;
    isTriggerAura[SPELL_AURA_RAID_PROC_FROM_CHARGE] = true;
    isTriggerAura[SPELL_AURA_RAID_PROC_FROM_CHARGE_WITH_VALUE] = true;
    isTriggerAura[SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE] = true;
    isTriggerAura[SPELL_AURA_MOD_SPELL_CRIT_CHANCE] = true;
    isTriggerAura[SPELL_AURA_ADD_FLAT_MODIFIER] = true;
    isTriggerAura[SPELL_AURA_ADD_PCT_MODIFIER] = true;
    isTriggerAura[SPELL_AURA_ABILITY_IGNORE_AURASTATE] = true;
    isTriggerAura[SPELL_AURA_MOD_INVISIBILITY] = true;
    isTriggerAura[SPELL_AURA_FORCE_REACTION] = true;
    isTriggerAura[SPELL_AURA_MOD_TAUNT] = true;
    isTriggerAura[SPELL_AURA_MOD_DETAUNT] = true;
    isTriggerAura[SPELL_AURA_MOD_DAMAGE_PERCENT_DONE] = true;
    isTriggerAura[SPELL_AURA_MOD_ATTACK_POWER_PCT] = true;
    isTriggerAura[SPELL_AURA_MOD_HIT_CHANCE] = true;
    isTriggerAura[SPELL_AURA_MOD_WEAPON_CRIT_PERCENT] = true;
    isTriggerAura[SPELL_AURA_MOD_BLOCK_PERCENT] = true;
    isTriggerAura[SPELL_AURA_MOD_ROOT_2] = true;

    isAlwaysTriggeredAura[SPELL_AURA_OVERRIDE_CLASS_SCRIPTS] = true;
    isAlwaysTriggeredAura[SPELL_AURA_MOD_STEALTH] = true;
    isAlwaysTriggeredAura[SPELL_AURA_MOD_CONFUSE] = true;
    isAlwaysTriggeredAura[SPELL_AURA_MOD_FEAR] = true;
    isAlwaysTriggeredAura[SPELL_AURA_MOD_ROOT] = true;
    isAlwaysTriggeredAura[SPELL_AURA_MOD_STUN] = true;
    isAlwaysTriggeredAura[SPELL_AURA_TRANSFORM] = true;
    isAlwaysTriggeredAura[SPELL_AURA_MOD_INVISIBILITY] = true;
    isAlwaysTriggeredAura[SPELL_AURA_SPELL_MAGNET] = true;
    isAlwaysTriggeredAura[SPELL_AURA_SCHOOL_ABSORB] = true;
    isAlwaysTriggeredAura[SPELL_AURA_MOD_STEALTH] = true;
    isAlwaysTriggeredAura[SPELL_AURA_MOD_ROOT_2] = true;

    spellTypeMask[SPELL_AURA_MOD_STEALTH] = PROC_SPELL_TYPE_DAMAGE | PROC_SPELL_TYPE_NO_DMG_HEAL;
    spellTypeMask[SPELL_AURA_MOD_CONFUSE] = PROC_SPELL_TYPE_DAMAGE;
    spellTypeMask[SPELL_AURA_MOD_FEAR] = PROC_SPELL_TYPE_DAMAGE;
    spellTypeMask[SPELL_AURA_MOD_ROOT] = PROC_SPELL_TYPE_DAMAGE;
    spellTypeMask[SPELL_AURA_MOD_ROOT_2] = PROC_SPELL_TYPE_DAMAGE;
    spellTypeMask[SPELL_AURA_MOD_STUN] = PROC_SPELL_TYPE_DAMAGE;
    spellTypeMask[SPELL_AURA_TRANSFORM] = PROC_SPELL_TYPE_DAMAGE;
    spellTypeMask[SPELL_AURA_MOD_INVISIBILITY] = PROC_SPELL_TYPE_DAMAGE;

    // This generates default procs to retain compatibility with previous proc system
    TC_LOG_INFO("server.loading", "Generating spell proc data from SpellMap...");
    count = 0;
    oldMSTime = getMSTime();

    for (SpellInfo const& spellInfo : mSpellInfoMap)
    {
        // Data already present in DB, overwrites default proc
        if (mSpellProcMap.find({ spellInfo.Id, spellInfo.Difficulty }) != mSpellProcMap.end())
            continue;

        // Nothing to do if no flags set
        if (!spellInfo.ProcFlags)
            continue;

        bool addTriggerFlag = false;
        ProcFlagsSpellType procSpellTypeMask = PROC_SPELL_TYPE_NONE;
        uint32 nonProcMask = 0;
        for (SpellEffectInfo const& spellEffectInfo : spellInfo.GetEffects())
        {
            if (!spellEffectInfo.IsEffect())
                continue;

            uint32 auraName = spellEffectInfo.ApplyAuraName;
            if (!auraName)
                continue;

            if (!isTriggerAura[auraName])
            {
                // explicitly disable non proccing auras to avoid losing charges on self proc
                nonProcMask |= 1 << spellEffectInfo.EffectIndex;
                continue;
            }

            procSpellTypeMask |= spellTypeMask[auraName];
            if (isAlwaysTriggeredAura[auraName])
                addTriggerFlag = true;

            // many proc auras with taken procFlag mask don't have attribute "can proc with triggered"
            // they should proc nevertheless (example mage armor spells with judgement)
            if (!addTriggerFlag && (spellInfo.ProcFlags & TAKEN_HIT_PROC_FLAG_MASK) != 0)
            {
                switch (auraName)
                {
                    case SPELL_AURA_PROC_TRIGGER_SPELL:
                    case SPELL_AURA_PROC_TRIGGER_DAMAGE:
                        addTriggerFlag = true;
                        break;
                    default:
                        break;
                }
            }
        }

        if (!procSpellTypeMask)
        {
            for (SpellEffectInfo const& spellEffectInfo : spellInfo.GetEffects())
            {
                if (spellEffectInfo.IsAura())
                {
                    TC_LOG_ERROR("sql.sql", "Spell Id {} has DBC ProcFlags 0x{:X} 0x{:X}, but it's of non-proc aura type, it probably needs an entry in `spell_proc` table to be handled correctly.",
                                 spellInfo.Id, uint32(spellInfo.ProcFlags[0]), uint32(spellInfo.ProcFlags[1]));
                    break;
                }
            }

            continue;
        }

        SpellProcEntry procEntry;
        procEntry.SchoolMask      = 0;
        procEntry.ProcFlags = spellInfo.ProcFlags;
        procEntry.SpellFamilyName = 0;
        for (SpellEffectInfo const& spellEffectInfo : spellInfo.GetEffects())
            if (spellEffectInfo.IsEffect() && isTriggerAura[spellEffectInfo.ApplyAuraName])
                procEntry.SpellFamilyMask |= spellEffectInfo.SpellClassMask;

        if (procEntry.SpellFamilyMask)
            procEntry.SpellFamilyName = spellInfo.SpellFamilyName;

        procEntry.SpellTypeMask   = procSpellTypeMask;
        procEntry.SpellPhaseMask  = PROC_SPELL_PHASE_HIT;
        procEntry.HitMask         = PROC_HIT_NONE; // uses default proc @see SpellMgr::CanSpellTriggerProcOnEvent

        if (!(procEntry.ProcFlags & REQ_SPELL_PHASE_PROC_FLAG_MASK) && procEntry.ProcFlags & PROC_FLAG_2_CAST_SUCCESSFUL)
            procEntry.SpellPhaseMask = PROC_SPELL_PHASE_CAST; // set default phase for PROC_FLAG_2_CAST_SUCCESSFUL

        bool triggersSpell = false;
        for (SpellEffectInfo const& spellEffectInfo : spellInfo.GetEffects())
        {
            if (!spellEffectInfo.IsAura())
                continue;

            switch (spellEffectInfo.ApplyAuraName)
            {
                // Reflect auras should only proc off reflects
                case SPELL_AURA_REFLECT_SPELLS:
                case SPELL_AURA_REFLECT_SPELLS_SCHOOL:
                    procEntry.HitMask = PROC_HIT_REFLECT;
                    break;
                // Only drop charge on crit
                case SPELL_AURA_MOD_WEAPON_CRIT_PERCENT:
                    procEntry.HitMask = PROC_HIT_CRITICAL;
                    break;
                // Only drop charge on block
                case SPELL_AURA_MOD_BLOCK_PERCENT:
                    procEntry.HitMask = PROC_HIT_BLOCK;
                    break;
                // proc auras with another aura reducing hit chance (eg 63767) only proc on missed attack
                case SPELL_AURA_MOD_HIT_CHANCE:
                    if (spellEffectInfo.CalcValue() <= -100)
                        procEntry.HitMask = PROC_HIT_MISS;
                    break;
                case SPELL_AURA_PROC_TRIGGER_SPELL:
                case SPELL_AURA_PROC_TRIGGER_SPELL_WITH_VALUE:
                    triggersSpell = spellEffectInfo.TriggerSpell != 0;
                    break;
                default:
                    continue;
            }
            break;
        }

        procEntry.AttributesMask  = PROC_ATTR_NONE;
        procEntry.DisableEffectsMask = nonProcMask;
        if (spellInfo.ProcFlags & PROC_FLAG_KILL)
            procEntry.AttributesMask |= PROC_ATTR_REQ_EXP_OR_HONOR;
        if (addTriggerFlag)
            procEntry.AttributesMask |= PROC_ATTR_TRIGGERED_CAN_PROC;

        procEntry.ProcsPerMinute  = 0;
        procEntry.Chance          = spellInfo.ProcChance;
        procEntry.Cooldown        = Milliseconds(spellInfo.ProcCooldown);
        procEntry.Charges         = spellInfo.ProcCharges;

#if 0
        if (spellInfo.HasAttribute(SPELL_ATTR3_CAN_PROC_FROM_PROCS) && !procEntry.SpellFamilyMask
            && procEntry.Chance >= 100
            && spellInfo.ProcBasePPM <= 0.0f
            && procEntry.Cooldown <= 0ms
            && procEntry.Charges <= 0
            && procEntry.ProcFlags & (PROC_FLAG_DEAL_MELEE_ABILITY | PROC_FLAG_DEAL_RANGED_ATTACK | PROC_FLAG_DEAL_RANGED_ABILITY | PROC_FLAG_DEAL_HELPFUL_ABILITY
                | PROC_FLAG_DEAL_HARMFUL_ABILITY | PROC_FLAG_DEAL_HELPFUL_SPELL | PROC_FLAG_DEAL_HARMFUL_SPELL | PROC_FLAG_DEAL_HARMFUL_PERIODIC
                | PROC_FLAG_DEAL_HELPFUL_PERIODIC)
            && triggersSpell)
        {
            TC_LOG_ERROR("sql.sql", "Spell Id {} has SPELL_ATTR3_CAN_PROC_FROM_PROCS attribute and no restriction on what spells can cause it to proc and no cooldown. "
                "This spell can cause infinite proc loops. Proc data for this spell was not generated, data in `spell_proc` table is required for it to function!",
                spellInfo.Id);
            continue;
        }
#endif

        mSpellProcMap[{ spellInfo.Id, spellInfo.Difficulty }] = procEntry;
        ++count;
    }

    TC_LOG_INFO("server.loading", ">> Generated spell proc data for {} spells in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellBonuses()
{
    uint32 oldMSTime = getMSTime();

    mSpellBonusMap.clear();                             // need for reload case

    //                                                0      1             2          3         4
    QueryResult result = WorldDatabase.Query("SELECT entry, direct_bonus, dot_bonus, ap_bonus, ap_dot_bonus FROM spell_bonus_data");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell bonus data. DB table `spell_bonus_data` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();
        uint32 entry = fields[0].GetUInt32();

        SpellInfo const* spell = GetSpellInfo(entry, DIFFICULTY_NONE);
        if (!spell)
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_bonus_data` does not exist.", entry);
            continue;
        }

        SpellBonusEntry& sbe = mSpellBonusMap[entry];
        sbe.direct_damage = fields[1].GetFloat();
        sbe.dot_damage = fields[2].GetFloat();
        sbe.ap_bonus = fields[3].GetFloat();
        sbe.ap_dot_bonus = fields[4].GetFloat();

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} extra spell bonus data in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellThreats()
{
    uint32 oldMSTime = getMSTime();

    mSpellThreatMap.clear();                                // need for reload case

    //                                                0      1        2       3
    QueryResult result = WorldDatabase.Query("SELECT entry, flatMod, pctMod, apPctMod FROM spell_threat");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 aggro generating spells. DB table `spell_threat` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 entry = fields[0].GetUInt32();

        if (!GetSpellInfo(entry, DIFFICULTY_NONE))
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_threat` does not exist.", entry);
            continue;
        }

        SpellThreatEntry ste;
        ste.flatMod  = fields[1].GetInt32();
        ste.pctMod   = fields[2].GetFloat();
        ste.apPctMod = fields[3].GetFloat();

        mSpellThreatMap[entry] = ste;
        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} SpellThreatEntries in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSkillLineAbilityMap()
{
    uint32 oldMSTime = getMSTime();

    mSkillLineAbilityMap.clear();

    uint32 count = 0;

    for (uint32 i = 0; i < sSkillLineAbilityStore.GetNumRows(); ++i)
    {
        SkillLineAbilityEntry const* SkillInfo = sSkillLineAbilityStore.LookupEntry(i);
        if (!SkillInfo)
            continue;

        mSkillLineAbilityMap.insert(SkillLineAbilityMap::value_type(SkillInfo->Spell, SkillInfo));
        ++count;
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} SkillLineAbility MultiMap Data in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellPetAuras()
{
    uint32 oldMSTime = getMSTime();

    mSpellPetAuraMap.clear();                                  // need for reload case

    //                                                  0       1       2    3
    QueryResult result = WorldDatabase.Query("SELECT spell, effectId, pet, aura FROM spell_pet_auras");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell pet auras. DB table `spell_pet_auras` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 spell = fields[0].GetUInt32();
        SpellEffIndex eff = SpellEffIndex(fields[1].GetUInt8());
        uint32 pet = fields[2].GetUInt32();
        uint32 aura = fields[3].GetUInt32();

        SpellPetAuraMap::iterator itr = mSpellPetAuraMap.find((spell<<8) + eff);
        if (itr != mSpellPetAuraMap.end())
            itr->second.AddAura(pet, aura);
        else
        {
            SpellInfo const* spellInfo = GetSpellInfo(spell, DIFFICULTY_NONE);
            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_pet_auras` does not exist.", spell);
                continue;
            }
            if (eff >= spellInfo->GetEffects().size())
            {
                TC_LOG_ERROR("spells", "The spell {} listed in `spell_pet_auras` does not have any effect at index {}", spell, uint32(eff));
                continue;
            }

            if (spellInfo->GetEffect(eff).Effect != SPELL_EFFECT_DUMMY &&
               (spellInfo->GetEffect(eff).Effect != SPELL_EFFECT_APPLY_AURA ||
                spellInfo->GetEffect(eff).ApplyAuraName != SPELL_AURA_DUMMY))
            {
                TC_LOG_ERROR("spells", "The spell {} listed in `spell_pet_auras` does not have any dummy aura or dummy effect.", spell);
                continue;
            }

            SpellInfo const* spellInfo2 = GetSpellInfo(aura, DIFFICULTY_NONE);
            if (!spellInfo2)
            {
                TC_LOG_ERROR("sql.sql", "The aura {} listed in `spell_pet_auras` does not exist.", aura);
                continue;
            }

            PetAura pa(pet, aura, spellInfo->GetEffect(eff).TargetA.GetTarget() == TARGET_UNIT_PET, spellInfo->GetEffect(eff).CalcValue());
            mSpellPetAuraMap[(spell<<8) + eff] = pa;
        }

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} spell pet auras in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellEnchantProcData()
{
    uint32 oldMSTime = getMSTime();

    mSpellEnchantProcEventMap.clear();                             // need for reload case

    //                                                       0       1               2        3               4
    QueryResult result = WorldDatabase.Query("SELECT EnchantID, Chance, ProcsPerMinute, HitMask, AttributesMask FROM spell_enchant_proc_data");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell enchant proc event conditions. DB table `spell_enchant_proc_data` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 enchantId = fields[0].GetUInt32();

        SpellItemEnchantmentEntry const* ench = sSpellItemEnchantmentStore.LookupEntry(enchantId);
        if (!ench)
        {
            TC_LOG_ERROR("sql.sql", "The enchancment {} listed in `spell_enchant_proc_data` does not exist.", enchantId);
            continue;
        }

        SpellEnchantProcEntry spe;

        spe.Chance = fields[1].GetFloat();
        spe.ProcsPerMinute = fields[2].GetFloat();
        spe.HitMask = fields[3].GetUInt32();
        spe.AttributesMask = fields[4].GetUInt32();

        mSpellEnchantProcEventMap[enchantId] = spe;

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} enchant proc data definitions in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellLinked()
{
    uint32 oldMSTime = getMSTime();

    mSpellLinkedMap.clear();    // need for reload case

    //                                                0              1             2
    QueryResult result = WorldDatabase.Query("SELECT spell_trigger, spell_effect, type FROM spell_linked_spell");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 linked spells. DB table `spell_linked_spell` is empty.");
        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        int32 trigger = fields[0].GetInt32();
        int32 effect = fields[1].GetInt32();
        SpellLinkedType type = SpellLinkedType(fields[2].GetUInt8());

        SpellInfo const* spellInfo = GetSpellInfo(abs(trigger), DIFFICULTY_NONE);
        if (!spellInfo)
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_linked_spell` does not exist.", abs(trigger));
            continue;
        }

        if (effect >= 0)
        {
            for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
            {
                if (spellEffectInfo.CalcValue() == abs(effect))
                    TC_LOG_ERROR("sql.sql", "The spell {} Effect: {} listed in `spell_linked_spell` has same bp{} like effect (possible hack).", abs(trigger), abs(effect), uint32(spellEffectInfo.EffectIndex));
            }
        }

        spellInfo = GetSpellInfo(abs(effect), DIFFICULTY_NONE);
        if (!spellInfo)
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_linked_spell` does not exist.", abs(effect));
            continue;
        }

        if (type < SPELL_LINK_CAST || type > SPELL_LINK_REMOVE)
        {
            TC_LOG_ERROR("sql.sql", "The spell trigger {}, effect {} listed in `spell_linked_spell` has invalid link type {}, skipped.", trigger, effect, type);
            continue;
        }

        if (trigger < 0)
        {
            if (type != SPELL_LINK_CAST)
                TC_LOG_ERROR("sql.sql", "The spell trigger {} listed in `spell_linked_spell` has invalid link type {}, changed to 0.", trigger, type);

            trigger = -trigger;
            type = SPELL_LINK_REMOVE;
        }

        if (type != SPELL_LINK_AURA)
        {
            if (trigger == effect)
            {
                TC_LOG_ERROR("sql.sql", "The spell trigger {}, effect {} listed in `spell_linked_spell` triggers itself (infinite loop), skipped.", trigger, effect);
                continue;
            }
        }

        mSpellLinkedMap[{ type, trigger }].push_back(effect);

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} linked spells in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadPetLevelupSpellMap()
{
    uint32 oldMSTime = getMSTime();

    mPetLevelupSpellMap.clear();                                   // need for reload case

    uint32 count = 0;
    uint32 family_count = 0;

    for (uint32 i = 0; i < sCreatureFamilyStore.GetNumRows(); ++i)
    {
        CreatureFamilyEntry const* creatureFamily = sCreatureFamilyStore.LookupEntry(i);
        if (!creatureFamily)                                     // not exist
            continue;

        for (uint8 j = 0; j < 2; ++j)
        {
            if (!creatureFamily->SkillLine[j])
                continue;

            std::vector<SkillLineAbilityEntry const*> const* skillLineAbilities = sDB2Manager.GetSkillLineAbilitiesBySkill(creatureFamily->SkillLine[j]);
            if (!skillLineAbilities)
                continue;

            for (SkillLineAbilityEntry const* skillLine : *skillLineAbilities)
            {
                if (skillLine->AcquireMethod != SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN)
                    continue;

                SpellInfo const* spell = GetSpellInfo(skillLine->Spell, DIFFICULTY_NONE);
                if (!spell) // not exist or triggered or talent
                    continue;

                if (!spell->SpellLevel)
                    continue;

                PetLevelupSpellSet& spellSet = mPetLevelupSpellMap[i];
                if (spellSet.empty())
                    ++family_count;

                spellSet.insert(PetLevelupSpellSet::value_type(spell->SpellLevel, spell->Id));
                ++count;
            }
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} pet levelup and default spells for {} families in {} ms", count, family_count, GetMSTimeDiffToNow(oldMSTime));
}

bool LoadPetDefaultSpells_helper(CreatureTemplate const* cInfo, PetDefaultSpellsEntry& petDefSpells)
{
    // skip empty list;
    bool have_spell = false;
    for (uint8 j = 0; j < MAX_CREATURE_SPELL_DATA_SLOT; ++j)
    {
        if (petDefSpells.spellid[j])
        {
            have_spell = true;
            break;
        }
    }
    if (!have_spell)
        return false;

    // remove duplicates with levelupSpells if any
    if (PetLevelupSpellSet const* levelupSpells = cInfo->family ? sSpellMgr->GetPetLevelupSpellList(cInfo->family) : nullptr)
    {
        for (uint8 j = 0; j < MAX_CREATURE_SPELL_DATA_SLOT; ++j)
        {
            if (!petDefSpells.spellid[j])
                continue;

            for (PetLevelupSpellSet::const_iterator itr = levelupSpells->begin(); itr != levelupSpells->end(); ++itr)
            {
                if (itr->second == petDefSpells.spellid[j])
                {
                    petDefSpells.spellid[j] = 0;
                    break;
                }
            }
        }
    }

    // skip empty list;
    have_spell = false;
    for (uint8 j = 0; j < MAX_CREATURE_SPELL_DATA_SLOT; ++j)
    {
        if (petDefSpells.spellid[j])
        {
            have_spell = true;
            break;
        }
    }

    return have_spell;
}

void SpellMgr::LoadPetDefaultSpells()
{
    uint32 oldMSTime = getMSTime();

    mPetDefaultSpellsMap.clear();

    uint32 countCreature = 0;

    TC_LOG_INFO("server.loading", "Loading summonable creature templates...");
    oldMSTime = getMSTime();

    // different summon spells
    for (SpellInfo const& spellEntry : mSpellInfoMap)
    {
        if (spellEntry.Difficulty != DIFFICULTY_NONE)
            continue;

        for (SpellEffectInfo const& spellEffectInfo : spellEntry.GetEffects())
        {
            if (spellEffectInfo.IsEffect(SPELL_EFFECT_SUMMON) || spellEffectInfo.IsEffect(SPELL_EFFECT_SUMMON_PET))
            {
                uint32 creature_id = spellEffectInfo.MiscValue;
                CreatureTemplate const* cInfo = sObjectMgr->GetCreatureTemplate(creature_id);
                if (!cInfo)
                    continue;

                // get default pet spells from creature_template
                int32 petSpellsId = cInfo->Entry;
                if (mPetDefaultSpellsMap.find(cInfo->Entry) != mPetDefaultSpellsMap.end())
                    continue;

                PetDefaultSpellsEntry petDefSpells;
                for (uint8 j = 0; j < MAX_CREATURE_SPELL_DATA_SLOT; ++j)
                    petDefSpells.spellid[j] = cInfo->spells[j];

                if (LoadPetDefaultSpells_helper(cInfo, petDefSpells))
                {
                    mPetDefaultSpellsMap[petSpellsId] = petDefSpells;
                    ++countCreature;
                }
            }
        }
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} summonable creature templates in {} ms", countCreature, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellAreas()
{
    uint32 oldMSTime = getMSTime();

    mSpellAreaMap.clear();                                  // need for reload case
    mSpellAreaForAreaMap.clear();
    mSpellAreaForQuestMap.clear();
    mSpellAreaForQuestEndMap.clear();
    mSpellAreaForAuraMap.clear();

    //                                                  0     1         2              3               4                 5          6          7       8         9
    QueryResult result = WorldDatabase.Query("SELECT spell, area, quest_start, quest_start_status, quest_end_status, quest_end, aura_spell, racemask, gender, flags FROM spell_area");
    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell area requirements. DB table `spell_area` is empty.");

        return;
    }

    uint32 count = 0;
    do
    {
        Field* fields = result->Fetch();

        uint32 spell = fields[0].GetUInt32();
        SpellArea spellArea;
        spellArea.spellId             = spell;
        spellArea.areaId              = fields[1].GetUInt32();
        spellArea.questStart          = fields[2].GetUInt32();
        spellArea.questStartStatus    = fields[3].GetUInt32();
        spellArea.questEndStatus      = fields[4].GetUInt32();
        spellArea.questEnd            = fields[5].GetUInt32();
        spellArea.auraSpell           = fields[6].GetInt32();
        spellArea.raceMask.RawValue   = fields[7].GetUInt64();
        spellArea.gender              = Gender(fields[8].GetUInt8());
        spellArea.flags               = fields[9].GetUInt8();

        if (SpellInfo const* spellInfo = GetSpellInfo(spell, DIFFICULTY_NONE))
        {
            if (spellArea.flags & SPELL_AREA_FLAG_AUTOCAST)
                const_cast<SpellInfo*>(spellInfo)->Attributes |= SPELL_ATTR0_NO_AURA_CANCEL;
        }
        else
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` does not exist", spell);
            continue;
        }

        {
            bool ok = true;
            SpellAreaMapBounds sa_bounds = GetSpellAreaMapBounds(spellArea.spellId);
            for (SpellAreaMap::const_iterator itr = sa_bounds.first; itr != sa_bounds.second; ++itr)
            {
                if (spellArea.spellId != itr->second.spellId)
                    continue;
                if (spellArea.areaId != itr->second.areaId)
                    continue;
                if (spellArea.questStart != itr->second.questStart)
                    continue;
                if (spellArea.auraSpell != itr->second.auraSpell)
                    continue;
                if ((spellArea.raceMask & itr->second.raceMask).IsEmpty())
                    continue;
                if (spellArea.gender != itr->second.gender)
                    continue;

                // duplicate by requirements
                ok = false;
                break;
            }

            if (!ok)
            {
                TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` is already listed with similar requirements.", spell);
                continue;
            }
        }

        if (spellArea.areaId && !sAreaTableStore.LookupEntry(spellArea.areaId))
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` has a wrong area ({}) requirement.", spell, spellArea.areaId);
            continue;
        }

        if (spellArea.questStart && !sObjectMgr->GetQuestTemplate(spellArea.questStart))
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` has a wrong start quest ({}) requirement.", spell, spellArea.questStart);
            continue;
        }

        if (spellArea.questEnd)
        {
            if (!sObjectMgr->GetQuestTemplate(spellArea.questEnd))
            {
                TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` has a wrong ending quest ({}) requirement.", spell, spellArea.questEnd);
                continue;
            }
        }

        if (spellArea.auraSpell)
        {
            SpellInfo const* spellInfo = GetSpellInfo(abs(spellArea.auraSpell), DIFFICULTY_NONE);
            if (!spellInfo)
            {
                TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` has wrong aura spell ({}) requirement", spell, abs(spellArea.auraSpell));
                continue;
            }

            if (uint32(abs(spellArea.auraSpell)) == spellArea.spellId)
            {
                TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` has aura spell ({}) requirement for itself", spell, abs(spellArea.auraSpell));
                continue;
            }

            // not allow autocast chains by auraSpell field (but allow use as alternative if not present)
            if (spellArea.flags & SPELL_AREA_FLAG_AUTOCAST && spellArea.auraSpell > 0)
            {
                bool chain = false;
                SpellAreaForAuraMapBounds saBound = GetSpellAreaForAuraMapBounds(spellArea.spellId);
                for (SpellAreaForAuraMap::const_iterator itr = saBound.first; itr != saBound.second; ++itr)
                {
                    if (itr->second->flags & SPELL_AREA_FLAG_AUTOCAST && itr->second->auraSpell > 0)
                    {
                        chain = true;
                        break;
                    }
                }

                if (chain)
                {
                    TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` has the aura spell ({}) requirement that it autocasts itself from the aura.", spell, spellArea.auraSpell);
                    continue;
                }

                SpellAreaMapBounds saBound2 = GetSpellAreaMapBounds(spellArea.auraSpell);
                for (SpellAreaMap::const_iterator itr2 = saBound2.first; itr2 != saBound2.second; ++itr2)
                {
                    if (itr2->second.flags & SPELL_AREA_FLAG_AUTOCAST && itr2->second.auraSpell > 0)
                    {
                        chain = true;
                        break;
                    }
                }

                if (chain)
                {
                    TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` has the aura spell ({}) requirement that the spell itself autocasts from the aura.", spell, spellArea.auraSpell);
                    continue;
                }
            }
        }

        if (!spellArea.raceMask.IsEmpty() && (spellArea.raceMask & RACEMASK_ALL_PLAYABLE).IsEmpty())
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` has wrong race mask ({}) requirement.", spell, spellArea.raceMask.RawValue);
            continue;
        }

        if (spellArea.gender != GENDER_NONE && spellArea.gender != GENDER_FEMALE && spellArea.gender != GENDER_MALE)
        {
            TC_LOG_ERROR("sql.sql", "The spell {} listed in `spell_area` has wrong gender ({}) requirement.", spell, spellArea.gender);
            continue;
        }

        SpellArea const* sa = &mSpellAreaMap.insert(SpellAreaMap::value_type(spell, spellArea))->second;

        // for search by current zone/subzone at zone/subzone change
        if (spellArea.areaId)
            mSpellAreaForAreaMap.insert(SpellAreaForAreaMap::value_type(spellArea.areaId, sa));

        // for search at quest update checks
        if (spellArea.questStart || spellArea.questEnd)
        {
            if (spellArea.questStart == spellArea.questEnd)
                mSpellAreaForQuestMap.insert(SpellAreaForQuestMap::value_type(spellArea.questStart, sa));
            else
            {
                if (spellArea.questStart)
                    mSpellAreaForQuestMap.insert(SpellAreaForQuestMap::value_type(spellArea.questStart, sa));
                if (spellArea.questEnd)
                    mSpellAreaForQuestMap.insert(SpellAreaForQuestMap::value_type(spellArea.questEnd, sa));
            }
        }

        // for search at quest start/reward
        if (spellArea.questEnd)
            mSpellAreaForQuestEndMap.insert(SpellAreaForQuestMap::value_type(spellArea.questEnd, sa));

        // for search at aura apply
        if (spellArea.auraSpell)
            mSpellAreaForAuraMap.insert(SpellAreaForAuraMap::value_type(abs(spellArea.auraSpell), sa));

        ++count;
    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} spell area requirements in {} ms", count, GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellAuraOptions335(std::unordered_map<std::pair<uint32, Difficulty>, SpellInfoLoadHelper>& loadData)
{
    QueryResult result = WorldDatabase.Query("SELECT spell_id, cumulative_aura, proc_chance, proc_charges, proc_type_mask_0 FROM spellauraoptions335");

    if (!result)
    {
        TC_LOG_INFO("server.loading", ">> Loaded 0 spellAuraOptions335. DB table `spellauraoptions335` is empty.");
        return;
    }

    uint32 count = 0;

    do
    {
        Field* fields = result->Fetch();

        uint32 spell_id = fields[0].GetUInt32();
        uint32 cumulative_aura = fields[1].GetUInt32();
        uint32 proc_chance = fields[2].GetUInt32();
        uint32 proc_charges = fields[3].GetUInt32();
        uint32 proc_type_mask_0 = fields[4].GetUInt32();

        const std::pair<uint32, Difficulty>& spell = { spell_id, Difficulty(DIFFICULTY_NONE) };

        auto& options = loadData[spell].AuraOptions;

        // only fill missing values
        if (options == nullptr)
        {
            // Do we even bother cleaning this? It's freed on shutdown by OS
            options = new SpellAuraOptionsEntry;

            const_cast<SpellAuraOptionsEntry&>(*options).ID = 0;                            // unused anywhere so 0
            const_cast<SpellAuraOptionsEntry&>(*options).SpellID = spell_id;
            const_cast<SpellAuraOptionsEntry&>(*options).CumulativeAura = cumulative_aura;
            const_cast<SpellAuraOptionsEntry&>(*options).ProcChance = proc_chance;
            const_cast<SpellAuraOptionsEntry&>(*options).ProcCharges = proc_charges;
            const_cast<SpellAuraOptionsEntry&>(*options).ProcTypeMask[0] = proc_type_mask_0;

            const_cast<SpellAuraOptionsEntry&>(*options).DifficultyID = DIFFICULTY_NONE;    // TODO:
            const_cast<SpellAuraOptionsEntry&>(*options).ProcCategoryRecovery = 0;          // TODO: 335 doesn't have this
            const_cast<SpellAuraOptionsEntry&>(*options).ProcTypeMask[1] = 0;               // always 0
            const_cast<SpellAuraOptionsEntry&>(*options).SpellProcsPerMinuteID = 0;         // always 0

            ++count;
        }

    } while (result->NextRow());

    TC_LOG_INFO("server.loading", ">> Loaded {} spellAuraOptions335.", count);
}

typedef std::vector<SpellEffectEntry const*> SpellEffectVector;

void SpellMgr::LoadSpellInfoStore()
{
    uint32 oldMSTime = getMSTime();

    UnloadSpellInfoStore();

    std::unordered_map<std::pair<uint32, Difficulty>, SpellInfoLoadHelper> loadData;

    std::unordered_map<int32, BattlePetSpeciesEntry const*> battlePetSpeciesByCreature;
    for (BattlePetSpeciesEntry const* battlePetSpecies : sBattlePetSpeciesStore)
        if (battlePetSpecies->CreatureID)
            battlePetSpeciesByCreature[battlePetSpecies->CreatureID] = battlePetSpecies;

    for (SpellEffectEntry const* effect : sSpellEffectStore)
    {
        ASSERT(effect->EffectIndex < MAX_SPELL_EFFECTS, "MAX_SPELL_EFFECTS must be at least %d", effect->EffectIndex + 1);
        ASSERT(effect->Effect < TOTAL_SPELL_EFFECTS, "TOTAL_SPELL_EFFECTS must be at least %u", effect->Effect + 1);
        ASSERT(effect->EffectAura < int32(TOTAL_AURAS), "TOTAL_AURAS must be at least %d", effect->EffectAura + 1);
        ASSERT(effect->ImplicitTarget[0] < TOTAL_SPELL_TARGETS, "TOTAL_SPELL_TARGETS must be at least %u", effect->ImplicitTarget[0] + 1);
        ASSERT(effect->ImplicitTarget[1] < TOTAL_SPELL_TARGETS, "TOTAL_SPELL_TARGETS must be at least %u", effect->ImplicitTarget[1] + 1);

        loadData[{ effect->SpellID, Difficulty(effect->DifficultyID) }].Effects[effect->EffectIndex] = effect;

        if (effect->Effect == SPELL_EFFECT_SUMMON)
            if (SummonPropertiesEntry const* summonProperties = sSummonPropertiesStore.LookupEntry(effect->EffectMiscValue[1]))
                if (summonProperties->Slot == SUMMON_SLOT_MINIPET && summonProperties->GetFlags().HasFlag(SummonPropertiesFlags::SummonFromBattlePetJournal))
                    if (BattlePetSpeciesEntry const* battlePetSpecies = Trinity::Containers::MapGetValuePtr(battlePetSpeciesByCreature, effect->EffectMiscValue[0]))
                        BattlePets::BattlePetMgr::AddBattlePetSpeciesBySpell(effect->SpellID, battlePetSpecies);

        if (effect->Effect == SPELL_EFFECT_LANGUAGE)
            sLanguageMgr->LoadSpellEffectLanguage(effect);

        switch (effect->EffectAura)
        {
            case SPELL_AURA_ADD_FLAT_MODIFIER:
            case SPELL_AURA_ADD_PCT_MODIFIER:
                ASSERT(effect->EffectMiscValue[0] < MAX_SPELLMOD, "MAX_SPELLMOD must be at least %d", effect->EffectMiscValue[0] + 1);
                break;
            default:
                break;
        }
    }

    for (SpellAuraOptionsEntry const* auraOptions : sSpellAuraOptionsStore)
        loadData[{ auraOptions->SpellID, Difficulty(auraOptions->DifficultyID) }].AuraOptions = auraOptions;

    // alistar: for some reason when blizz was transitioning from vanilla to tbc they
    // decided to remove 50% of the SpellAuraOptions from the DB2 handling them server side
    // we have no choice but to load 3.3.5a data here and fill in those missing entires
    // this fixes many spells, when I was tracking why the Improved Blizzard was never proccing
    // I stumbled upon this mess.
    LoadSpellAuraOptions335(loadData);

    for (SpellAuraRestrictionsEntry const* auraRestrictions : sSpellAuraRestrictionsStore)
        loadData[{ auraRestrictions->SpellID, Difficulty(auraRestrictions->DifficultyID) }].AuraRestrictions = auraRestrictions;

    for (SpellCastingRequirementsEntry const* castingRequirements : sSpellCastingRequirementsStore)
        loadData[{ castingRequirements->SpellID, DIFFICULTY_NONE }].CastingRequirements = castingRequirements;

    for (SpellCategoriesEntry const* categories : sSpellCategoriesStore)
        loadData[{ categories->SpellID, Difficulty(categories->DifficultyID) }].Categories = categories;

    for (SpellClassOptionsEntry const* classOptions : sSpellClassOptionsStore)
        loadData[{ classOptions->SpellID, DIFFICULTY_NONE }].ClassOptions = classOptions;

    for (SpellCooldownsEntry const* cooldowns : sSpellCooldownsStore)
        loadData[{ cooldowns->SpellID, Difficulty(cooldowns->DifficultyID) }].Cooldowns = cooldowns;

    for (SpellEquippedItemsEntry const* equippedItems : sSpellEquippedItemsStore)
        loadData[{ equippedItems->SpellID, DIFFICULTY_NONE }].EquippedItems = equippedItems;

    for (SpellInterruptsEntry const* interrupts : sSpellInterruptsStore)
        loadData[{ interrupts->SpellID, Difficulty(interrupts->DifficultyID) }].Interrupts = interrupts;

    for (SpellLabelEntry const* label : sSpellLabelStore)
        loadData[{ label->SpellID, DIFFICULTY_NONE }].Labels.push_back(label);

    for (SpellLevelsEntry const* levels : sSpellLevelsStore)
        loadData[{ levels->SpellID, Difficulty(levels->DifficultyID) }].Levels = levels;

    for (SpellMiscEntry const* misc : sSpellMiscStore)
        loadData[{ misc->SpellID, Difficulty(misc->DifficultyID) }].Misc = misc;

    for (SpellPowerEntry const* power : sSpellPowerStore)
    {
        Difficulty difficulty = DIFFICULTY_NONE;
        uint8 index = power->OrderIndex;
        if (SpellPowerDifficultyEntry const* powerDifficulty = sSpellPowerDifficultyStore.LookupEntry(power->ID))
        {
            difficulty = Difficulty(powerDifficulty->DifficultyID);
            index = powerDifficulty->OrderIndex;
        }

        loadData[{ power->SpellID, difficulty }].Powers[index] = power;
    }

    for (SpellReagentsEntry const* reagents : sSpellReagentsStore)
        loadData[{ reagents->SpellID, DIFFICULTY_NONE }].Reagents = reagents;

    for (SpellReagentsCurrencyEntry const* reagentsCurrency : sSpellReagentsCurrencyStore)
        loadData[{ reagentsCurrency->SpellID, DIFFICULTY_NONE }].ReagentsCurrency.push_back(reagentsCurrency);

    for (SpellScalingEntry const* scaling : sSpellScalingStore)
        loadData[{ scaling->SpellID, DIFFICULTY_NONE }].Scaling = scaling;

    for (SpellShapeshiftEntry const* shapeshift : sSpellShapeshiftStore)
        loadData[{ shapeshift->SpellID, DIFFICULTY_NONE }].Shapeshift = shapeshift;

    for (SpellTargetRestrictionsEntry const* targetRestrictions : sSpellTargetRestrictionsStore)
        loadData[{ targetRestrictions->SpellID, Difficulty(targetRestrictions->DifficultyID) }].TargetRestrictions = targetRestrictions;

    for (SpellTotemsEntry const* totems : sSpellTotemsStore)
        loadData[{ totems->SpellID, DIFFICULTY_NONE }].Totems = totems;

    for (SpellXSpellVisualEntry const* visual : sSpellXSpellVisualStore)
    {
        SpellVisualVector& visuals = loadData[{ visual->SpellID, Difficulty(visual->DifficultyID) }].Visuals;

        auto where = std::lower_bound(visuals.begin(), visuals.end(), visual, [](SpellXSpellVisualEntry const* first, SpellXSpellVisualEntry const* second)
        {
            return first->CasterPlayerConditionID > second->CasterPlayerConditionID;
        });

        // sorted with unconditional visuals being last
        visuals.insert(where, visual);
    }

    for (std::pair<std::pair<uint32, Difficulty> const, SpellInfoLoadHelper>& data : loadData)
    {
        SpellNameEntry const* spellNameEntry = sSpellNameStore.LookupEntry(data.first.first);
        if (!spellNameEntry)
            continue;

        // fill blanks
        if (DifficultyEntry const* difficultyEntry = sDifficultyStore.LookupEntry(data.first.second))
        {
            do
            {
                if (SpellInfoLoadHelper const* fallbackData = Trinity::Containers::MapGetValuePtr(loadData, { data.first.first, Difficulty(difficultyEntry->FallbackDifficultyID) }))
                {
                    if (!data.second.AuraOptions)
                        data.second.AuraOptions = fallbackData->AuraOptions;

                    if (!data.second.AuraRestrictions)
                        data.second.AuraRestrictions = fallbackData->AuraRestrictions;

                    if (!data.second.CastingRequirements)
                        data.second.CastingRequirements = fallbackData->CastingRequirements;

                    if (!data.second.Categories)
                        data.second.Categories = fallbackData->Categories;

                    if (!data.second.ClassOptions)
                        data.second.ClassOptions = fallbackData->ClassOptions;

                    if (!data.second.Cooldowns)
                        data.second.Cooldowns = fallbackData->Cooldowns;

                    for (std::size_t i = 0; i < data.second.Effects.size(); ++i)
                        if (!data.second.Effects[i])
                            data.second.Effects[i] = fallbackData->Effects[i];

                    if (!data.second.EquippedItems)
                        data.second.EquippedItems = fallbackData->EquippedItems;

                    if (!data.second.Interrupts)
                        data.second.Interrupts = fallbackData->Interrupts;

                    if (data.second.Labels.empty())
                        data.second.Labels = fallbackData->Labels;

                    if (!data.second.Levels)
                        data.second.Levels = fallbackData->Levels;

                    if (!data.second.Misc)
                        data.second.Misc = fallbackData->Misc;

                    for (std::size_t i = 0; i < fallbackData->Powers.size(); ++i)
                        if (!data.second.Powers[i])
                            data.second.Powers[i] = fallbackData->Powers[i];

                    if (!data.second.Reagents)
                        data.second.Reagents = fallbackData->Reagents;

                    if (data.second.ReagentsCurrency.empty())
                        data.second.ReagentsCurrency = fallbackData->ReagentsCurrency;

                    if (!data.second.Scaling)
                        data.second.Scaling = fallbackData->Scaling;

                    if (!data.second.Shapeshift)
                        data.second.Shapeshift = fallbackData->Shapeshift;

                    if (!data.second.TargetRestrictions)
                        data.second.TargetRestrictions = fallbackData->TargetRestrictions;

                    if (!data.second.Totems)
                        data.second.Totems = fallbackData->Totems;

                    // visuals fall back only to first difficulty that defines any visual
                    // they do not stack all difficulties in fallback chain
                    if (data.second.Visuals.empty())
                        data.second.Visuals = fallbackData->Visuals;
                }

                difficultyEntry = sDifficultyStore.LookupEntry(difficultyEntry->FallbackDifficultyID);
            } while (difficultyEntry);
        }

        mSpellInfoMap.emplace(spellNameEntry, data.first.second, data.second);
    }

    TC_LOG_INFO("server.loading", ">> Loaded SpellInfo store in {} ms", GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::UnloadSpellInfoStore()
{
    mSpellInfoMap.clear();
    mServersideSpellNames.clear();
}

void SpellMgr::UnloadSpellInfoImplicitTargetConditionLists()
{
    for (SpellInfo const& spellInfo : mSpellInfoMap)
        const_cast<SpellInfo&>(spellInfo)._UnloadImplicitTargetConditionLists();
}

void SpellMgr::LoadSpellInfoServerside()
{
    uint32 oldMSTime = getMSTime();

    std::unordered_map<std::pair<uint32, Difficulty>, std::vector<SpellEffectEntry>> spellEffects;

    QueryResult effectsResult = WorldDatabase.Query("SELECT SpellID, Effect1, Effect2, Effect3, EffectDieSides1, EffectDieSides2, EffectDieSides3, "
                                                    "EffectRealPointsPerLevel1, EffectRealPointsPerLevel2, EffectRealPointsPerLevel3, "
                                                    "EffectBasePoints1, EffectBasePoints2, EffectBasePoints3, "
                                                    "EffectMechanic1, EffectMechanic2, EffectMechanic3, "
                                                    "EffectImplicitTargetA1, EffectImplicitTargetA2, EffectImplicitTargetA3, "
                                                    "EffectImplicitTargetB1, EffectImplicitTargetB2, EffectImplicitTargetB3, "
                                                    "EffectRadiusIndex1, EffectRadiusIndex2, EffectRadiusIndex3, "
                                                    "EffectApplyAuraName1, EffectApplyAuraName2, EffectApplyAuraName3, "
                                                    "EffectAuraPeriod1, EffectAuraPeriod2, EffectAuraPeriod3, "
                                                    "EffectAmplitude1, EffectAmplitude2, EffectAmplitude3, "
                                                    "EffectItemType1, EffectItemType2, EffectItemType3, "
                                                    "EffectMiscValue1, EffectMiscValue2, EffectMiscValue3, "
                                                    "EffectMiscValueB1, EffectMiscValueB2, EffectMiscValueB3, "
                                                    "EffectTriggerSpell1, EffectTriggerSpell2, EffectTriggerSpell3, "
                                                    "EffectSpellClassMaskA1, EffectSpellClassMaskA2, EffectSpellClassMaskA3, "
                                                    "EffectSpellClassMaskB1, EffectSpellClassMaskB2, EffectSpellClassMaskB3, "
                                                    "EffectSpellClassMaskC1, EffectSpellClassMaskC2, EffectSpellClassMaskC3, "
                                                    "DmgMultiplier1, DmgMultiplier2, DmgMultiplier3"
                                                    " FROM spell_dbc_effect");
    if (effectsResult)
    {
        do
        {
            Field* fields = effectsResult->Fetch();
            uint32 spellId = fields[0].GetUInt32();
            Difficulty difficulty = Difficulty(DIFFICULTY_NONE); // alistar: TODO add db column

            std::array<uint32, 3> _Effect   { fields[1].GetUInt32(), fields[2].GetUInt32(), fields[3].GetUInt32() };
            std::array<uint32, 3> _DieSides { fields[4].GetUInt32(), fields[5].GetUInt32(), fields[6].GetUInt32() };
            std::array<float, 3> _RealPointsPerLevel { fields[7].GetFloat(), fields[8].GetFloat(), fields[9].GetFloat() };
            std::array<uint32, 3> _EffectBasePoints { fields[10].GetUInt32(), fields[11].GetUInt32(), fields[12].GetUInt32() };
            std::array<uint32, 3> _EffectMechanic { fields[13].GetUInt32(), fields[14].GetUInt32(), fields[15].GetUInt32() };
            std::array<uint32, 3> _EffectImplicitTargetA { fields[16].GetUInt32(), fields[17].GetUInt32(), fields[18].GetUInt32() };
            std::array<uint32, 3> _EffectImplicitTargetB { fields[19].GetUInt32(), fields[20].GetUInt32(), fields[21].GetUInt32() };
            std::array<uint32, 3> _EffectRadiusIndex { fields[22].GetUInt32(), fields[23].GetUInt32(), fields[24].GetUInt32() };
            std::array<uint32, 3> _EffectApplyAuraName { fields[25].GetUInt32(), fields[26].GetUInt32(), fields[27].GetUInt32() };
            std::array<uint32, 3> _EffectAuraPeriod { fields[28].GetUInt32(), fields[29].GetUInt32(), fields[30].GetUInt32() };
            std::array<float, 3> _EffectAmplitude { fields[31].GetFloat(), fields[32].GetFloat(), fields[33].GetFloat() };
            std::array<uint32, 3> _EffectItemType { fields[34].GetUInt32(), fields[35].GetUInt32(), fields[36].GetUInt32() };
            std::array<uint32, 3> _EffectMiscValueA { fields[37].GetUInt32(), fields[38].GetUInt32(), fields[39].GetUInt32() };
            std::array<uint32, 3> _EffectMiscValueB { fields[40].GetUInt32(), fields[41].GetUInt32(), fields[42].GetUInt32() };
            std::array<uint32, 3> _EffectTriggerSpell { fields[43].GetUInt32(), fields[44].GetUInt32(), fields[45].GetUInt32() };
            //std::array<uint32, 3> _EffectSpellClassMaskA { fields[46].GetUInt32(), fields[47].GetUInt32(), fields[48].GetUInt32() };
            //std::array<uint32, 3> _EffectSpellClassMaskB { fields[49].GetUInt32(), fields[50].GetUInt32(), fields[51].GetUInt32() };
            //std::array<uint32, 3> _EffectSpellClassMaskC { fields[52].GetUInt32(), fields[53].GetUInt32(), fields[54].GetUInt32() };
            std::array<float, 3> _DmgMultiplier { fields[55].GetFloat(), fields[56].GetFloat(), fields[57].GetFloat() };
            
            for (uint8 i = 0; i != 3; ++i)
            {
                SpellEffectEntry effect { };
                effect.EffectIndex = i;
                effect.Effect = _Effect[i];
                effect.EffectAura = _EffectApplyAuraName[i];
                effect.EffectAmplitude = _EffectAmplitude[i];
                effect.EffectAttributes = 0; // alistar: TODO add db column
                effect.EffectAuraPeriod = _EffectAuraPeriod[i];
                effect.EffectBonusCoefficient = 0.0f; // alistar: TODO add db column
                effect.EffectChainAmplitude = 0.0f; // alistar: TODO add db column
                effect.EffectChainTargets = 0; // alistar: TODO add db column
                effect.EffectItemType = _EffectItemType[i];
                effect.EffectMechanic = _EffectMechanic[i];
                effect.EffectPointsPerResource = 0.0f; // alistar: TODO add db column
                effect.EffectPosFacing = 0.0f; // alistar: TODO add db column
                effect.EffectRealPointsPerLevel = _RealPointsPerLevel[i];
                effect.EffectTriggerSpell = _EffectTriggerSpell[i];
                effect.BonusCoefficientFromAP = 0.0f;  // alistar: TODO add db column
                effect.PvpMultiplier = 0.0f; // alistar: TODO add db column
                effect.Coefficient = 0.0f; // alistar: TODO add db column
                effect.Variance = 0.0f; // alistar: TODO add db column
                effect.ResourceCoefficient = 0.0f; // alistar: TODO add db column
                effect.GroupSizeBasePointsCoefficient = 0.0f; // alistar: TODO add db column
                effect.EffectBasePoints = _EffectBasePoints[i];
                effect.EffectMiscValue[0] = _EffectMiscValueA[i];
                effect.EffectMiscValue[1] = _EffectMiscValueB[i];

                // TODO: there's a 3rd radius in 3.3.5a..
                effect.EffectRadiusIndex[0] = _EffectRadiusIndex[i];
                effect.EffectRadiusIndex[1] = _EffectRadiusIndex[i];

                effect.EffectSpellClassMask = flag128(fields[46].GetUInt32(), fields[47].GetUInt32(), fields[48].GetUInt32(), fields[49].GetUInt32()); // alistar: this is most likely wrong..

                // TODO: there's a 3rd implicit target in 3.3.5a..
                effect.ImplicitTarget[0] = _EffectImplicitTargetA[i];
                effect.ImplicitTarget[1] = _EffectImplicitTargetB[i];

                auto existingSpellBounds = _GetSpellInfo(spellId);
                if (existingSpellBounds.begin() != existingSpellBounds.end())
                {
                    TC_LOG_ERROR("sql.sql", "Serverside spell {} difficulty {} effext index {} references a regular spell loaded from file. Adding serverside effects to existing spells is not allowed.",
                                 spellId, uint32(difficulty), effect.EffectIndex);
                    continue;
                }

                if (difficulty != DIFFICULTY_NONE && !sDifficultyStore.HasRecord(difficulty))
                {
                    TC_LOG_ERROR("sql.sql", "Serverside spell {} effect index {} references non-existing difficulty {}, skipped",
                                 spellId, effect.EffectIndex, uint32(difficulty));
                    continue;
                }

                if (effect.EffectIndex >= MAX_SPELL_EFFECTS)
                {
                    TC_LOG_ERROR("sql.sql", "Serverside spell {} difficulty {} has more than {} effects, effect at index {} skipped",
                                 spellId, uint32(difficulty), MAX_SPELL_EFFECTS, effect.EffectIndex);
                    continue;
                }

                if (effect.Effect >= TOTAL_SPELL_EFFECTS)
                {
                    TC_LOG_ERROR("sql.sql", "Serverside spell {} difficulty {} has invalid effect type {} at index {}, skipped",
                                 spellId, uint32(difficulty), effect.Effect, effect.EffectIndex);
                    continue;
                }

                if (effect.EffectAura >= int32(TOTAL_AURAS))
                {
                    TC_LOG_ERROR("sql.sql", "Serverside spell {} difficulty {} has invalid aura type {} at index {}, skipped",
                                 spellId, uint32(difficulty), effect.EffectAura, effect.EffectIndex);
                    continue;
                }

                if (effect.ImplicitTarget[0] >= TOTAL_SPELL_TARGETS)
                {
                    TC_LOG_ERROR("sql.sql", "Serverside spell {} difficulty {} has invalid targetA type {} at index {}, skipped",
                                 spellId, uint32(difficulty), effect.ImplicitTarget[0], effect.EffectIndex);
                    continue;
                }

                if (effect.ImplicitTarget[1] >= TOTAL_SPELL_TARGETS)
                {
                    TC_LOG_ERROR("sql.sql", "Serverside spell {} difficulty {} has invalid targetB type {} at index {}, skipped",
                                 spellId, uint32(difficulty), effect.ImplicitTarget[1], effect.EffectIndex);
                    continue;
                }

                if (effect.EffectRadiusIndex[0] && !sSpellRadiusStore.HasRecord(effect.EffectRadiusIndex[0]))
                {
                    TC_LOG_ERROR("sql.sql", "Serverside spell {} difficulty {} has invalid radius id {} at index {}, set to 0",
                                 spellId, uint32(difficulty), effect.EffectRadiusIndex[0], effect.EffectIndex);
                }

                if (effect.EffectRadiusIndex[1] && !sSpellRadiusStore.HasRecord(effect.EffectRadiusIndex[1]))
                {
                    TC_LOG_ERROR("sql.sql", "Serverside spell {} difficulty {} has invalid max radius id {} at index {}, set to 0",
                                 spellId, uint32(difficulty), effect.EffectRadiusIndex[1], effect.EffectIndex);
                }

                spellEffects[{ spellId, difficulty }].push_back(std::move(effect));
            }

            /*SpellEffectEntry effect{ };
            effect.EffectIndex = fields[1].GetInt32();
            effect.Effect = fields[3].GetInt32();
            effect.EffectAura = fields[4].GetInt16();
            effect.EffectAmplitude = fields[5].GetFloat();
            effect.EffectAttributes = fields[6].GetInt32();
            effect.EffectAuraPeriod = fields[7].GetInt32();
            effect.EffectBonusCoefficient = fields[8].GetFloat();
            effect.EffectChainAmplitude = fields[9].GetFloat();
            effect.EffectChainTargets = fields[10].GetInt32();
            effect.EffectItemType = fields[11].GetInt32();
            effect.EffectMechanic = Mechanics(fields[12].GetInt32());
            effect.EffectPointsPerResource = fields[13].GetFloat();
            effect.EffectPosFacing = fields[14].GetFloat();
            effect.EffectRealPointsPerLevel = fields[15].GetFloat();
            effect.EffectTriggerSpell = fields[16].GetInt32();
            effect.BonusCoefficientFromAP = fields[17].GetFloat();
            effect.PvpMultiplier = fields[18].GetFloat();
            effect.Coefficient = fields[19].GetFloat();
            effect.Variance = fields[20].GetFloat();
            effect.ResourceCoefficient = fields[21].GetFloat();
            effect.GroupSizeBasePointsCoefficient = fields[22].GetFloat();
            effect.EffectBasePoints = fields[23].GetFloat();
            effect.EffectMiscValue[0] = fields[24].GetInt32();
            effect.EffectMiscValue[1] = fields[25].GetInt32();
            effect.EffectRadiusIndex[0] = fields[26].GetUInt32();
            effect.EffectRadiusIndex[1] = fields[27].GetUInt32();
            effect.EffectSpellClassMask = flag128(fields[28].GetInt32(), fields[29].GetInt32(), fields[30].GetInt32(), fields[31].GetInt32());
            effect.ImplicitTarget[0] = fields[32].GetInt16();
            effect.ImplicitTarget[1] = fields[33].GetInt16();*/

        } while (effectsResult->NextRow());
    }

    //                                                     0   1             2           3       4         5           6             7              8
    QueryResult spellsResult = WorldDatabase.Query("SELECT Id, DifficultyID, CategoryId, Dispel, Mechanic, Attributes, AttributesEx, AttributesEx2, AttributesEx3, "
    //   9              10             11             12             13             14             15              16              17              18
        "AttributesEx4, AttributesEx5, AttributesEx6, AttributesEx7, AttributesEx8, AttributesEx9, AttributesEx10, AttributesEx11, AttributesEx12, AttributesEx13, "
    //   19              20       21          22       23                  24                  25                 26               27
        "AttributesEx14, Stances, StancesNot, Targets, TargetCreatureType, RequiresSpellFocus, FacingCasterFlags, CasterAuraState, TargetAuraState, "
    //   28                      29                      30               31               32                      33
        "ExcludeCasterAuraState, ExcludeTargetAuraState, CasterAuraSpell, TargetAuraSpell, ExcludeCasterAuraSpell, ExcludeTargetAuraSpell, "
    //   34              35              36                     37                     38
        "CasterAuraType, TargetAuraType, ExcludeCasterAuraType, ExcludeTargetAuraType, CastingTimeIndex, "
    //   39            40                    41                     42                 43              44                   45
        "RecoveryTime, CategoryRecoveryTime, StartRecoveryCategory, StartRecoveryTime, InterruptFlags, AuraInterruptFlags1, AuraInterruptFlags2, "
    //   46                      47                      48         49          50          51           52            53           54        55         56
        "ChannelInterruptFlags1, ChannelInterruptFlags2, ProcFlags, ProcFlags2, ProcChance, ProcCharges, ProcCooldown, ProcBasePPM, MaxLevel, BaseLevel, SpellLevel, "
    //   57             58          59     60           61           62                 63                        64                             65
        "DurationIndex, RangeIndex, Speed, LaunchDelay, StackAmount, EquippedItemClass, EquippedItemSubClassMask, EquippedItemInventoryTypeMask, ContentTuningId, "
    //   66         67         68         69              70                  71               72                 73                 74                 75
        "SpellName, ConeAngle, ConeWidth, MaxTargetLevel, MaxAffectedTargets, SpellFamilyName, SpellFamilyFlags1, SpellFamilyFlags2, SpellFamilyFlags3, SpellFamilyFlags4, "
    //   76        77              78           79          80
        "DmgClass, PreventionType, AreaGroupId, SchoolMask, ChargeCategoryId FROM serverside_spell");
    if (spellsResult)
    {
        mServersideSpellNames.reserve(spellsResult->GetRowCount());

        do
        {
            Field* fields = spellsResult->Fetch();
            uint32 spellId = fields[0].GetUInt32();
            Difficulty difficulty = Difficulty(fields[1].GetUInt32());
            if (sSpellNameStore.HasRecord(spellId))
            {
                TC_LOG_ERROR("sql.sql", "Serverside spell {} difficulty {} is already loaded from file. Overriding existing spells is not allowed.",
                    spellId, uint32(difficulty));
                continue;
            }

            mServersideSpellNames.emplace_back(spellId, fields[66].GetString());

            SpellInfo& spellInfo = const_cast<SpellInfo&>(*mSpellInfoMap.emplace(&mServersideSpellNames.back().Name, difficulty, spellEffects[{ spellId, difficulty }]).first);
            spellInfo.CategoryId = fields[2].GetUInt32();
            spellInfo.Dispel = fields[3].GetUInt32();
            spellInfo.Mechanic = fields[4].GetUInt32();
            spellInfo.Attributes = fields[5].GetUInt32();
            spellInfo.AttributesEx = fields[6].GetUInt32();
            spellInfo.AttributesEx2 = fields[7].GetUInt32();
            spellInfo.AttributesEx3 = fields[8].GetUInt32();
            spellInfo.AttributesEx4 = fields[9].GetUInt32();
            spellInfo.AttributesEx5 = fields[10].GetUInt32();
            spellInfo.AttributesEx6 = fields[11].GetUInt32();
            spellInfo.AttributesEx7 = fields[12].GetUInt32();
            spellInfo.AttributesEx8 = fields[13].GetUInt32();
            spellInfo.AttributesEx9 = fields[14].GetUInt32();
            spellInfo.AttributesEx10 = fields[15].GetUInt32();
            spellInfo.AttributesEx11 = fields[16].GetUInt32();
            spellInfo.AttributesEx12 = fields[17].GetUInt32();
            spellInfo.AttributesEx13 = fields[18].GetUInt32();
            spellInfo.AttributesEx14 = fields[19].GetUInt32();
            spellInfo.Stances = fields[20].GetUInt64();
            spellInfo.StancesNot = fields[21].GetUInt64();
            spellInfo.Targets = fields[22].GetUInt32();
            spellInfo.TargetCreatureType = fields[23].GetUInt32();
            spellInfo.RequiresSpellFocus = fields[24].GetUInt32();
            spellInfo.FacingCasterFlags = fields[25].GetUInt32();
            spellInfo.CasterAuraState = fields[26].GetUInt32();
            spellInfo.TargetAuraState = fields[27].GetUInt32();
            spellInfo.ExcludeCasterAuraState = fields[28].GetUInt32();
            spellInfo.ExcludeTargetAuraState = fields[29].GetUInt32();
            spellInfo.CasterAuraSpell = fields[30].GetUInt32();
            spellInfo.TargetAuraSpell = fields[31].GetUInt32();
            spellInfo.ExcludeCasterAuraSpell = fields[32].GetUInt32();
            spellInfo.ExcludeTargetAuraSpell = fields[33].GetUInt32();
            spellInfo.CasterAuraType = AuraType(fields[34].GetInt32());
            spellInfo.TargetAuraType = AuraType(fields[35].GetInt32());
            spellInfo.ExcludeCasterAuraType = AuraType(fields[36].GetInt32());
            spellInfo.ExcludeTargetAuraType = AuraType(fields[37].GetInt32());
            spellInfo.CastTimeEntry = sSpellCastTimesStore.LookupEntry(fields[38].GetUInt32());
            spellInfo.RecoveryTime = fields[39].GetUInt32();
            spellInfo.CategoryRecoveryTime = fields[40].GetUInt32();
            spellInfo.StartRecoveryCategory = fields[41].GetUInt32();
            spellInfo.StartRecoveryTime = fields[42].GetUInt32();
            spellInfo.InterruptFlags = SpellInterruptFlags(fields[43].GetUInt32());
            spellInfo.AuraInterruptFlags = SpellAuraInterruptFlags(fields[44].GetUInt32());
            spellInfo.AuraInterruptFlags2 = SpellAuraInterruptFlags2(fields[45].GetUInt32());
            spellInfo.ChannelInterruptFlags = SpellAuraInterruptFlags(fields[46].GetUInt32());
            spellInfo.ChannelInterruptFlags2 = SpellAuraInterruptFlags2(fields[47].GetUInt32());
            spellInfo.ProcFlags[0] = fields[48].GetUInt32();
            spellInfo.ProcFlags[1] = fields[49].GetUInt32();
            spellInfo.ProcChance = fields[50].GetUInt32();
            spellInfo.ProcCharges = fields[51].GetUInt32();
            spellInfo.ProcCooldown = fields[52].GetUInt32();
            spellInfo.ProcBasePPM = fields[53].GetFloat();
            spellInfo.MaxLevel = fields[54].GetUInt32();
            spellInfo.BaseLevel = fields[55].GetUInt32();
            spellInfo.SpellLevel = fields[56].GetUInt32();
            spellInfo.DurationEntry = sSpellDurationStore.LookupEntry(fields[57].GetUInt32());
            spellInfo.RangeEntry = sSpellRangeStore.LookupEntry(fields[58].GetUInt32());
            spellInfo.Speed = fields[59].GetFloat();
            spellInfo.LaunchDelay = fields[60].GetFloat();
            spellInfo.StackAmount = fields[61].GetUInt32();
            spellInfo.EquippedItemClass = fields[62].GetInt32();
            spellInfo.EquippedItemSubClassMask = fields[63].GetInt32();
            spellInfo.EquippedItemInventoryTypeMask = fields[64].GetInt32();
            spellInfo.ContentTuningId = fields[65].GetUInt32();
            spellInfo.ConeAngle = fields[67].GetFloat();
            spellInfo.Width = fields[68].GetFloat();
            spellInfo.MaxTargetLevel = fields[69].GetUInt32();
            spellInfo.MaxAffectedTargets = fields[70].GetUInt32();
            spellInfo.SpellFamilyName = fields[71].GetUInt32();
            spellInfo.SpellFamilyFlags = flag128(fields[72].GetUInt32(), fields[73].GetUInt32(), fields[74].GetUInt32(), fields[75].GetUInt32());
            spellInfo.DmgClass = fields[76].GetUInt32();
            spellInfo.PreventionType = fields[77].GetUInt32();
            spellInfo.RequiredAreasID = fields[78].GetInt32();
            spellInfo.SchoolMask = fields[79].GetUInt32();
            spellInfo.ChargeCategoryId = fields[80].GetUInt32();

        } while (spellsResult->NextRow());
    }

    TC_LOG_INFO("server.loading", ">> Loaded {} serverside spells {} ms", mServersideSpellNames.size(), GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellInfoCustomAttributes()
{
    uint32 oldMSTime = getMSTime();
    uint32 oldMSTime2 = oldMSTime;

    QueryResult result = WorldDatabase.Query("SELECT entry, attributes FROM spell_custom_attr");

    if (!result)
        TC_LOG_INFO("server.loading", ">> Loaded 0 spell custom attributes from DB. DB table `spell_custom_attr` is empty.");
    else
    {
        uint32 count = 0;
        do
        {
            Field* fields = result->Fetch();

            uint32 spellId = fields[0].GetUInt32();
            uint32 attributes = fields[1].GetUInt32();

            auto spells = _GetSpellInfo(spellId);
            if (spells.begin() == spells.end())
            {
                TC_LOG_ERROR("sql.sql", "Table `spell_custom_attr` has wrong spell (entry: {}), ignored.", spellId);
                continue;
            }

            for (SpellInfo const& spellInfo : spells)
            {
                if ((attributes & SPELL_ATTR0_CU_NEGATIVE) != 0)
                {
                    for (SpellEffectInfo const& spellEffectInfo : spellInfo.GetEffects())
                    {
                        if (spellEffectInfo.IsEffect())
                            continue;

                        if ((attributes & (SPELL_ATTR0_CU_NEGATIVE_EFF0 << spellEffectInfo.EffectIndex)) != 0)
                        {
                            TC_LOG_ERROR("sql.sql", "Table `spell_custom_attr` has attribute SPELL_ATTR0_CU_NEGATIVE_EFF{} for spell {} with no EFFECT_{}", uint32(spellEffectInfo.EffectIndex), spellId, uint32(spellEffectInfo.EffectIndex));
                            continue;
                        }

                        // alistar: some spells in spell_custom_attr use this flag
                        const_cast<SpellInfo&>(spellInfo).NegativeEffects[spellEffectInfo.EffectIndex] = true;
                    }
                }

                const_cast<SpellInfo&>(spellInfo).AttributesCu |= attributes;
            }

            ++count;
        } while (result->NextRow());

        TC_LOG_INFO("server.loading", ">> Loaded {} spell custom attributes from DB in {} ms", count, GetMSTimeDiffToNow(oldMSTime2));
    }

    std::set<uint32> talentSpells;
    for (uint32 i = 0; i < sTalentStore.GetNumRows(); ++i)
        if (TalentEntry const* talentInfo = sTalentStore.LookupEntry(i))
            for (uint32 spellRank : talentInfo->SpellRank)
                talentSpells.insert(spellRank);

    for (SpellInfo const& spellInfo : mSpellInfoMap)
    {
        SpellInfo* spellInfoMutable = const_cast<SpellInfo*>(&spellInfo);
        for (SpellEffectInfo const& spellEffectInfo : spellInfoMutable->GetEffects())
        {
            // all bleed effects and spells ignore armor
            if (spellInfo.GetEffectMechanicMask(spellEffectInfo.EffectIndex) & (UI64LIT(1) << MECHANIC_BLEED))
                spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_IGNORE_ARMOR;

            switch (spellEffectInfo.ApplyAuraName)
            {
                case SPELL_AURA_MOD_POSSESS:
                case SPELL_AURA_MOD_CONFUSE:
                case SPELL_AURA_MOD_CHARM:
                case SPELL_AURA_AOE_CHARM:
                case SPELL_AURA_MOD_FEAR:
                case SPELL_AURA_MOD_STUN:
                    spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_AURA_CC;
                    break;
                default:
                    break;
            }

            switch (spellEffectInfo.ApplyAuraName)
            {
                case SPELL_AURA_CONVERT_RUNE:   // Can't be saved - aura handler relies on calculated amount and changes it
                case SPELL_AURA_OPEN_STABLE:    // No point in saving this, since the stable dialog can't be open on aura load anyway.
                // Auras that require both caster & target to be in world cannot be saved
                case SPELL_AURA_CONTROL_VEHICLE:
                case SPELL_AURA_BIND_SIGHT:
                case SPELL_AURA_MOD_POSSESS:
                case SPELL_AURA_MOD_POSSESS_PET:
                case SPELL_AURA_MOD_CHARM:
                case SPELL_AURA_AOE_CHARM:
                // Controlled by Battleground
                case SPELL_AURA_BATTLEGROUND_PLAYER_POSITION:
                case SPELL_AURA_BATTLEGROUND_PLAYER_POSITION_FACTIONAL:
                    spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_AURA_CANNOT_BE_SAVED;
                    break;
                default:
                    break;
            }

            switch (spellEffectInfo.Effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_HEALTH_LEECH:
                case SPELL_EFFECT_HEAL:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_POWER_BURN:
                case SPELL_EFFECT_HEAL_MECHANICAL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                case SPELL_EFFECT_HEAL_PCT:
                case SPELL_EFFECT_DAMAGE_FROM_MAX_HEALTH_PCT:
                    spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_CAN_CRIT;
                    break;
                default:
                    break;
            }

            switch (spellEffectInfo.Effect)
            {
                case SPELL_EFFECT_SCHOOL_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE:
                case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                case SPELL_EFFECT_HEAL:
                    spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_DIRECT_DAMAGE;
                    break;
                case SPELL_EFFECT_POWER_DRAIN:
                case SPELL_EFFECT_POWER_BURN:
                case SPELL_EFFECT_HEAL_MAX_HEALTH:
                case SPELL_EFFECT_HEALTH_LEECH:
                case SPELL_EFFECT_HEAL_PCT:
                case SPELL_EFFECT_ENERGIZE_PCT:
                case SPELL_EFFECT_ENERGIZE:
                case SPELL_EFFECT_HEAL_MECHANICAL:
                    spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_NO_INITIAL_THREAT;
                    break;
                case SPELL_EFFECT_CHARGE:
                case SPELL_EFFECT_CHARGE_DEST:
                case SPELL_EFFECT_JUMP:
                case SPELL_EFFECT_JUMP_DEST:
                case SPELL_EFFECT_LEAP_BACK:
                    spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_CHARGE;
                    break;
                case SPELL_EFFECT_PICKPOCKET:
                    spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_PICKPOCKET;
                    break;
                case SPELL_EFFECT_ENCHANT_ITEM:
                case SPELL_EFFECT_ENCHANT_ITEM_TEMPORARY:
                case SPELL_EFFECT_ENCHANT_ITEM_PRISMATIC:
                case SPELL_EFFECT_ENCHANT_HELD_ITEM:
                {
                    // only enchanting profession enchantments procs can stack
                    if (IsPartOfSkillLine(SKILL_ENCHANTING, spellInfo.Id))
                    {
                        uint32 enchantId = spellEffectInfo.MiscValue;
                        SpellItemEnchantmentEntry const* enchant = sSpellItemEnchantmentStore.LookupEntry(enchantId);
                        if (!enchant)
                            break;

                        for (uint8 s = 0; s < MAX_ITEM_ENCHANTMENT_EFFECTS; ++s)
                        {
                            if (enchant->Effect[s] != ITEM_ENCHANTMENT_TYPE_COMBAT_SPELL)
                                continue;

                            for (SpellInfo const& procInfo : _GetSpellInfo(enchant->EffectArg[s]))
                            {
                                // if proced directly from enchantment, not via proc aura
                                // NOTE: Enchant Weapon - Blade Ward also has proc aura spell and is proced directly
                                // however its not expected to stack so this check is good
                                if (procInfo.HasAura(SPELL_AURA_PROC_TRIGGER_SPELL))
                                    continue;

                                const_cast<SpellInfo&>(procInfo).AttributesCu |= SPELL_ATTR0_CU_ENCHANT_PROC;
                            }
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // spells ignoring hit result should not be binary
        if (!spellInfoMutable->HasAttribute(SPELL_ATTR3_ALWAYS_HIT))
        {
            bool setFlag = false;
            for (SpellEffectInfo const& spellEffectInfo : spellInfoMutable->GetEffects())
            {
                if (spellEffectInfo.IsEffect())
                {
                    switch (spellEffectInfo.Effect)
                    {
                        case SPELL_EFFECT_SCHOOL_DAMAGE:
                        case SPELL_EFFECT_WEAPON_DAMAGE:
                        case SPELL_EFFECT_WEAPON_DAMAGE_NOSCHOOL:
                        case SPELL_EFFECT_NORMALIZED_WEAPON_DMG:
                        case SPELL_EFFECT_WEAPON_PERCENT_DAMAGE:
                        case SPELL_EFFECT_TRIGGER_SPELL:
                        case SPELL_EFFECT_TRIGGER_SPELL_WITH_VALUE:
                            break;
                        case SPELL_EFFECT_PERSISTENT_AREA_AURA:
                        case SPELL_EFFECT_APPLY_AURA:
                        case SPELL_EFFECT_APPLY_AREA_AURA_PARTY:
                        case SPELL_EFFECT_APPLY_AREA_AURA_RAID:
                        case SPELL_EFFECT_APPLY_AREA_AURA_FRIEND:
                        case SPELL_EFFECT_APPLY_AREA_AURA_ENEMY:
                        case SPELL_EFFECT_APPLY_AREA_AURA_PET:
                        case SPELL_EFFECT_APPLY_AREA_AURA_OWNER:
                        case SPELL_EFFECT_APPLY_AURA_ON_PET:
                        case SPELL_EFFECT_APPLY_AREA_AURA_SUMMONS:
                        case SPELL_EFFECT_APPLY_AREA_AURA_PARTY_NONRANDOM:
                            if (spellEffectInfo.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE ||
                                spellEffectInfo.ApplyAuraName == SPELL_AURA_PERIODIC_DAMAGE_PERCENT ||
                                spellEffectInfo.ApplyAuraName == SPELL_AURA_DUMMY ||
                                spellEffectInfo.ApplyAuraName == SPELL_AURA_PERIODIC_LEECH ||
                                spellEffectInfo.ApplyAuraName == SPELL_AURA_PERIODIC_HEALTH_FUNNEL ||
                                spellEffectInfo.ApplyAuraName == SPELL_AURA_PERIODIC_DUMMY)
                                break;
                            [[fallthrough]];
                        default:
                        {
                            // No value and not interrupt cast or crowd control without SPELL_ATTR0_UNAFFECTED_BY_INVULNERABILITY flag
                            if (!spellEffectInfo.CalcValue() && !((spellEffectInfo.Effect == SPELL_EFFECT_INTERRUPT_CAST || spellInfoMutable->HasAttribute(SPELL_ATTR0_CU_AURA_CC)) && !spellInfoMutable->HasAttribute(SPELL_ATTR0_NO_IMMUNITIES)))
                                break;

                            // Sindragosa Frost Breath
                            if (spellInfoMutable->Id == 69649 || spellInfoMutable->Id == 71056 || spellInfoMutable->Id == 71057 || spellInfoMutable->Id == 71058 || spellInfoMutable->Id == 73061 || spellInfoMutable->Id == 73062 || spellInfoMutable->Id == 73063 || spellInfoMutable->Id == 73064)
                                break;

                            // Frostbolt
                            if (spellInfoMutable->SpellFamilyName == SPELLFAMILY_MAGE && (spellInfoMutable->SpellFamilyFlags[0] & 0x20))
                                break;

                            // Frost Fever
                            if (spellInfoMutable->Id == 55095)
                                break;

                            // Haunt
                            if (spellInfoMutable->SpellFamilyName == SPELLFAMILY_WARLOCK && (spellInfoMutable->SpellFamilyFlags[1] & 0x40000))
                                break;

                            setFlag = true;
                            break;
                        }
                    }

                    if (setFlag)
                    {
                        spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_BINARY_SPELL;
                        break;
                    }
                }
            }
        }

        // Remove normal school mask to properly calculate damage
        if ((spellInfoMutable->SchoolMask & SPELL_SCHOOL_MASK_NORMAL) && (spellInfoMutable->SchoolMask & SPELL_SCHOOL_MASK_MAGIC))
        {
            spellInfoMutable->SchoolMask &= ~SPELL_SCHOOL_MASK_NORMAL;
            spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_SCHOOLMASK_NORMAL_WITH_MAGIC;
        }

        spellInfoMutable->_InitializeSpellPositivity();

        if (talentSpells.count(spellInfoMutable->Id))
            spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_IS_TALENT;

        if (G3D::fuzzyNe(spellInfoMutable->Width, 0.0f))
            spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_CONE_LINE;

        if (spellInfoMutable->GetSpellVisual() == 3879)
            spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_CONE_BACK;

        switch (spellInfoMutable->SpellFamilyName)
        {
        case SPELLFAMILY_WARRIOR:
            // Shout / Piercing Howl
            if (spellInfoMutable->SpellFamilyFlags[0] & 0x20000/* || spellInfo->SpellFamilyFlags[1] & 0x20*/)
                spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_AURA_CC;
            break;
        case SPELLFAMILY_DRUID:
            // Roar
            if (spellInfoMutable->SpellFamilyFlags[0] & 0x8)
                spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_AURA_CC;
            break;
        case SPELLFAMILY_GENERIC:
            // Stoneclaw Totem effect
            if (spellInfoMutable->Id == 5729)
                spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_AURA_CC;
            break;
        default:
            break;
        }

        spellInfoMutable->_InitializeExplicitTargetMask();

        if (spellInfoMutable->Speed > 0.0f)
        {
            auto visualNeedsAmmo = [](SpellXSpellVisualEntry const* spellXspellVisual)
            {
                SpellVisualEntry const* spellVisual = sSpellVisualStore.LookupEntry(spellXspellVisual->SpellVisualID);
                if (!spellVisual)
                    return false;

                std::vector<SpellVisualMissileEntry const*> const* spellVisualMissiles = sDB2Manager.GetSpellVisualMissiles(spellVisual->SpellVisualMissileSetID);
                if (!spellVisualMissiles)
                    return false;

                for (SpellVisualMissileEntry const* spellVisualMissile : *spellVisualMissiles)
                {
                    SpellVisualEffectNameEntry const* spellVisualEffectName = sSpellVisualEffectNameStore.LookupEntry(spellVisualMissile->SpellVisualEffectNameID);
                    if (!spellVisualEffectName)
                        continue;

                    SpellVisualEffectNameType type = SpellVisualEffectNameType(spellVisualEffectName->Type);
                    if (type == SpellVisualEffectNameType::UnitAmmoBasic || type == SpellVisualEffectNameType::UnitAmmoPreferred)
                        return true;
                }

                return false;
            };

            for (SpellXSpellVisualEntry const* spellXspellVisual : spellInfoMutable->_visuals)
            {
                if (visualNeedsAmmo(spellXspellVisual))
                {
                    spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_NEEDS_AMMO_DATA;
                    break;
                }
            }
        }

        // Saving to DB happens before removing from world - skip saving these auras
        if (spellInfoMutable->HasAuraInterruptFlag(SpellAuraInterruptFlags::LeaveWorld))
            spellInfoMutable->AttributesCu |= SPELL_ATTR0_CU_AURA_CANNOT_BE_SAVED;
    }

    // addition for binary spells, omit spells triggering other spells
    for (SpellInfo const& spellInfo : mSpellInfoMap)
    {
        SpellInfo* spellInfoMutable = const_cast<SpellInfo*>(&spellInfo);
        if (!spellInfoMutable->HasAttribute(SPELL_ATTR0_CU_BINARY_SPELL))
        {
            bool allNonBinary = true;
            bool overrideAttr = false;
            for (SpellEffectInfo const& spellEffectInfo : spellInfoMutable->GetEffects())
            {
                if (spellEffectInfo.IsAura() && spellEffectInfo.TriggerSpell)
                {
                    switch (spellEffectInfo.ApplyAuraName)
                    {
                        case SPELL_AURA_PERIODIC_TRIGGER_SPELL:
                        case SPELL_AURA_PERIODIC_TRIGGER_SPELL_FROM_CLIENT:
                        case SPELL_AURA_PERIODIC_TRIGGER_SPELL_WITH_VALUE:
                            if (SpellInfo const* triggerSpell = sSpellMgr->GetSpellInfo(spellEffectInfo.TriggerSpell, DIFFICULTY_NONE))
                            {
                                overrideAttr = true;
                                if (triggerSpell->HasAttribute(SPELL_ATTR0_CU_BINARY_SPELL))
                                    allNonBinary = false;
                            }
                            break;
                        default:
                            break;
                    }
                }
            }

            if (overrideAttr && allNonBinary)
                spellInfoMutable->AttributesCu &= ~SPELL_ATTR0_CU_BINARY_SPELL;
        }

        // remove attribute from spells that can't crit
        if (spellInfo.HasAttribute(SPELL_ATTR0_CU_CAN_CRIT))
            if (spellInfo.HasAttribute(SPELL_ATTR2_CANT_CRIT))
                spellInfoMutable->AttributesCu &= ~SPELL_ATTR0_CU_CAN_CRIT;

    }

    // add custom attribute to liquid auras
    for (LiquidTypeEntry const* liquid : sLiquidTypeStore)
    {
        if (liquid->SpellID)
            for (SpellInfo const& spellInfo : _GetSpellInfo(liquid->SpellID))
                const_cast<SpellInfo&>(spellInfo).AttributesCu |= SPELL_ATTR0_CU_AURA_CANNOT_BE_SAVED;
    }

    TC_LOG_INFO("server.loading", ">> Loaded SpellInfo custom attributes in {} ms", GetMSTimeDiffToNow(oldMSTime));
}

inline void ApplySpellFix(std::initializer_list<uint32> spellIds, void(*fix)(SpellInfo*))
{
    for (uint32 spellId : spellIds)
    {
        auto range = _GetSpellInfo(spellId);
        if (range.begin() == range.end())
        {
            TC_LOG_ERROR("server.loading", "Spell info correction specified for non-existing spell {}", spellId);
            continue;
        }

        for (SpellInfo const& spellInfo : range)
            fix(&const_cast<SpellInfo&>(spellInfo));
    }
}

inline void ApplySpellEffectFix(SpellInfo* spellInfo, SpellEffIndex effectIndex, void(*fix)(SpellEffectInfo*))
{
    if (spellInfo->GetEffects().size() <= effectIndex)
    {
        TC_LOG_ERROR("server.loading", "Spell effect info correction specified for non-existing effect {} of spell {}", uint32(effectIndex), spellInfo->Id);
        return;
    }

    fix(const_cast<SpellEffectInfo*>(&spellInfo->GetEffect(effectIndex)));
}

void SpellMgr::LoadSpellInfoCorrections()
{
    uint32 oldMSTime = getMSTime();

    // Some spells have no ApplyAuraPeriod set
    {
        ApplySpellFix({
            6727,  // Poison Mushroom
            7288,  // Immolate Cumulative (TEST) (Rank 1)
            7291,  // Food (TEST)
            7331,  // Healing Aura (TEST) (Rank 1)
            /*
            30400, // Nether Beam - Perseverance
                Blizzlike to have it disabled? DBC says:
                "This is currently turned off to increase performance. Enable this to make it fire more frequently."
            */
            34589, // Dangerous Water
            52562, // Arthas Zombie Catcher
            57550, // Tirion Aggro
            65755
        }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_0).ApplyAuraPeriod = 1 * IN_MILLISECONDS;
        });

        ApplySpellFix({
            24707, // Food
            26263, // Dim Sum
            29055, // Refreshing Red Apple
            37504  // Karazhan - Chess NPC AI, action timer
        }, [](SpellInfo* spellInfo)
        {
            // first effect has correct ApplyAuraPeriod
            spellInfo->_GetEffect(EFFECT_1).ApplyAuraPeriod = spellInfo->GetEffect(EFFECT_0).ApplyAuraPeriod;
        });

        // Vomit
        ApplySpellFix({ 43327 }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_1).ApplyAuraPeriod = 1 * IN_MILLISECONDS;
        });

        // Strider Presence
        ApplySpellFix({ 4312 }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_0).ApplyAuraPeriod = 1 * IN_MILLISECONDS;
            spellInfo->_GetEffect(EFFECT_1).ApplyAuraPeriod = 1 * IN_MILLISECONDS;
        });

        // Food
        ApplySpellFix({ 64345 }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_0).ApplyAuraPeriod = 1 * IN_MILLISECONDS;
            spellInfo->_GetEffect(EFFECT_2).ApplyAuraPeriod = 1 * IN_MILLISECONDS;
        });
    }

    // specific code for cases with no trigger spell provided in field
    {
        // Brood Affliction: Bronze
        ApplySpellFix({ 23170 }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_0).TriggerSpell = 23171;
        });

        // Feed Captured Animal
        ApplySpellFix({ 29917 }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_0).TriggerSpell = 29916;
        });

        // Remote Toy
        ApplySpellFix({ 37027 }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_0).TriggerSpell = 37029;
        });

        // Eye of Grillok
        ApplySpellFix({ 38495 }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_0).TriggerSpell = 38530;
        });

        // Tear of Azzinoth Summon Channel - it's not really supposed to do anything, and this only prevents the console spam
        ApplySpellFix({ 39857 }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_0).TriggerSpell = 39856;
        });

        // Personalized Weather
        ApplySpellFix({ 46736 }, [](SpellInfo* spellInfo)
        {
            spellInfo->_GetEffect(EFFECT_1).TriggerSpell = 46737;
        });
    }

    // this one is here because we have no SP bonus for dmgclass none spell
    // but this one should since it's DBC data
    ApplySpellFix({
        52042, // Healing Stream Totem
    }, [](SpellInfo* spellInfo)
    {
        // We need more spells to find a general way (if there is any)
        spellInfo->DmgClass = SPELL_DAMAGE_CLASS_MAGIC;
    });

    // Spell Reflection
    ApplySpellFix({ 57643 }, [](SpellInfo* spellInfo)
    {
        spellInfo->EquippedItemClass = -1;
    });

    ApplySpellFix({
        63026, // Force Cast (HACK: Target shouldn't be changed)
        63137  // Force Cast (HACK: Target shouldn't be changed; summon position should be untied from spell destination)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_DEST_DB);
    });

    // Immolate
    ApplySpellFix({
        348,
        707,
        1094,
        2941,
        11665,
        11667,
        11668,
        25309,
        27215,
        47810,
        47811
        }, [](SpellInfo* spellInfo)
    {
        // copy SP scaling data from direct damage to DoT
        spellInfo->_GetEffect(EFFECT_0).BonusCoefficient = spellInfo->GetEffect(EFFECT_1).BonusCoefficient;
    });

    // Detect Undead
    ApplySpellFix({ 11389 }, [](SpellInfo* spellInfo)
    {
        const auto& spellPower = spellInfo->PowerCosts[0];

        const_cast<int8&>(spellPower->PowerType) = POWER_MANA;
        const_cast<int32&>(spellPower->ManaCost) = 0;
        const_cast<int32&>(spellPower->ManaPerSecond) = 0;
    });

    // Drink! (Brewfest)
    ApplySpellFix({ 42436 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_TARGET_ANY);
    });

    // Warsong Gulch Anti-Stall Debuffs
    ApplySpellFix({
        46392, // Focused Assault
        46393, // Brutal Assault
    }, [](SpellInfo* spellInfo)
    {
        // due to discrepancies between ranks
        spellInfo->Attributes |= SPELL_ATTR0_NO_IMMUNITIES;
    });

    // Summon Skeletons
    ApplySpellFix({ 52611, 52612 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).MiscValueB = 64;
    });

    // Battlegear of Eternal Justice
    ApplySpellFix({
        26135, // Battlegear of Eternal Justice
        37557  // Mark of Light
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->SpellFamilyFlags = flag128();
    });

    ApplySpellFix({
        40244, // Simon Game Visual
        40245, // Simon Game Visual
        40246, // Simon Game Visual
        40247, // Simon Game Visual
        42835  // Spout, remove damage effect, only anim is needed
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).Effect = SPELL_EFFECT_NONE;
    });

    ApplySpellFix({
        63665, // Charge (Argent Tournament emote on riders)
        31298, // Sleep (needs target selection script)
        51904, // Summon Ghouls On Scarlet Crusade (this should use conditions table, script for this spell needs to be fixed)
        2895,  // Wrath of Air Totem rank 1 (Aura)
        68933, // Wrath of Air Totem rank 2 (Aura)
        29200, // Purify Helboar Meat
        10872, // Abolish Disease Effect
        3137   // Abolish Poison Effect
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
        spellInfo->_GetEffect(EFFECT_0).TargetB = SpellImplicitTargetInfo();
    });

    ApplySpellFix({
        56690, // Thrust Spear
        60586, // Mighty Spear Thrust
        60776, // Claw Swipe
        60881, // Fatal Strike
        60864  // Jaws of Death
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx4 |= SPELL_ATTR4_IGNORE_DAMAGE_TAKEN_MODIFIERS;
    });

    // Missile Barrage - alistar: TODO
    /*ApplySpellFix({ 44401 }, [](SpellInfo* spellInfo)
    {
        // should be consumed before Clearcasting
        spellInfo->Priority = 100;
    });*/

    // Howl of Azgalor
    ApplySpellFix({ 31344 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetARadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_100_YARDS); // 100yards instead of 50000?!
    });

    ApplySpellFix({
        42818, // Headless Horseman - Wisp Flight Port
        42821, // Headless Horseman - Wisp Flight Missile
        17678  // Despawn Spectral Combatants
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(6); // 100 yards
    });

    // They Must Burn Bomb Aura (self)
    ApplySpellFix({ 36350 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TriggerSpell = 36325; // They Must Burn Bomb Drop (DND)
    });

    ApplySpellFix({
        61407, // Energize Cores
        62136, // Energize Cores
        54069, // Energize Cores
        56251  // Energize Cores
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_SRC_AREA_ENTRY);
    });

    ApplySpellFix({
        50785, // Energize Cores
        59372  // Energize Cores
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_SRC_AREA_ENEMY);
    });

    // Mana Shield (rank 2)
    ApplySpellFix({ 8494 }, [](SpellInfo* spellInfo)
    {
        // because of bug in dbc
        spellInfo->ProcChance = 0;
    });

    // Maelstrom Weapon
    ApplySpellFix({
        51528, // (Rank 1)
        51529, // (Rank 2)
        51530, // (Rank 3)
        51531, // (Rank 4)
        51532  // (Rank 5)
    }, [](SpellInfo* spellInfo)
    {
        // due to discrepancies between ranks
        spellInfo->EquippedItemSubClassMask = 0x0000FC33;
        spellInfo->AttributesEx3 |= SPELL_ATTR3_CAN_PROC_FROM_PROCS;
    });

    ApplySpellFix({
        20335, // Heart of the Crusader
        20336,
        20337,
        53228, // Rapid Killing (Rank 1)
        53232, // Rapid Killing (Rank 2)
        63320  // Glyph of Life Tap
    }, [](SpellInfo* spellInfo)
    {
        // Entries were not updated after spell effect change, we have to do that manually :/
        spellInfo->AttributesEx3 |= SPELL_ATTR3_CAN_PROC_FROM_PROCS;
    });

    ApplySpellFix({
        51627, // Turn the Tables (Rank 1)
        51628, // Turn the Tables (Rank 2)
        51629  // Turn the Tables (Rank 3)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_DOT_STACKING_RULE;
    });

    ApplySpellFix({
        52910, // Turn the Tables
        52914, // Turn the Tables
        52915  // Turn the Tables
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
    });

    // Magic Absorption
    ApplySpellFix({
        29441, // (Rank 1)
        29444  // (Rank 2)
    }, [](SpellInfo* spellInfo)
    {
        // Caused off by 1 calculation (ie 79 resistance at level 80)
        spellInfo->SpellLevel = 0;
    });

    // Execute
    ApplySpellFix({
        5308,  // (Rank 1)
        20658, // (Rank 2)
        20660, // (Rank 3)
        20661, // (Rank 4)
        20662, // (Rank 5)
        25234, // (Rank 6)
        25236, // (Rank 7)
        47470, // (Rank 8)
        47471  // (Rank 9)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_SUPPRESS_CASTER_PROCS;
    });

    // Improved Spell Reflection - aoe aura
    ApplySpellFix({ 59725 }, [](SpellInfo* spellInfo)
    {
        // Target entry seems to be wrong for this spell :/
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER_AREA_PARTY);
        spellInfo->_GetEffect(EFFECT_0).TargetARadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_20_YARDS);
    });

    ApplySpellFix({
        44978, // Wild Magic
        45001, // Wild Magic
        45002, // Wild Magic
        45004, // Wild Magic
        45006, // Wild Magic
        45010, // Wild Magic
        31347, // Doom
        41635, // Prayer of Mending
        44869, // Spectral Blast
        45027, // Revitalize
        45976, // Muru Portal Channel
        39365, // Thundering Storm
        41071, // Raise Dead (HACK)
        52124, // Sky Darkener Assault
        42442, // Vengeance Landing Cannonfire
        45863, // Cosmetic - Incinerate to Random Target
        25425, // Shoot
        45761, // Shoot
        42611, // Shoot
        61588, // Blazing Harpoon
        52479, // Gift of the Harvester
        48246, // Ball of Flame
        36327, // Shoot Arcane Explosion Arrow
        55479, // Force Obedience
        28560, // Summon Blizzard (Sapphiron)
        53096, // Quetz'lun's Judgment
        70743, // AoD Special
        70614, // AoD Special - Vegard
        4020,  // Safirdrang's Chill
        52438, // Summon Skittering Swarmer (Force Cast)
        52449, // Summon Skittering Infector (Force Cast)
        53609, // Summon Anub'ar Assassin (Force Cast)
        53457, // Summon Impale Trigger (AoE)
        45907, // Torch Target Picker
        52953, // Torch
        58121, // Torch
        43109, // Throw Torch
        58552, // Return to Orgrimmar
        58533, // Return to Stormwind
        21855, // Challenge Flag
        38762, // Force of Neltharaku
        51122, // Fierce Lightning Stike
        71848, // Toxic Wasteling Find Target
        36146, // Chains of Naberius
        33711, // Murmur's Touch
        38794  // Murmur's Touch
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 1;
    });

    ApplySpellFix({
        36384, // Skartax Purple Beam
        47731  // Critter
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 2;
    });

    ApplySpellFix({
        41376, // Spite
        39992, // Needle Spine
        29576, // Multi-Shot
        40816, // Saber Lash
        37790, // Spread Shot
        46771, // Flame Sear
        45248, // Shadow Blades
        41303, // Soul Drain
        54172, // Divine Storm (heal)
        29213, // Curse of the Plaguebringer - Noth
        28542, // Life Drain - Sapphiron
        66588, // Flaming Spear
        54171  // Divine Storm
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 3;
    });

    ApplySpellFix({
        38310, // Multi-Shot
        53385  // Divine Storm (Damage)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 4;
    });

    ApplySpellFix({
        42005, // Bloodboil
        38296, // Spitfire Totem
        37676, // Insidious Whisper
        46008, // Negative Energy
        45641, // Fire Bloom
        55665, // Life Drain - Sapphiron (H)
        28796, // Poison Bolt Volly - Faerlina
        37135  // Domination
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 5;
    });

    ApplySpellFix({
        40827, // Sinful Beam
        40859, // Sinister Beam
        40860, // Vile Beam
        40861, // Wicked Beam
        54098, // Poison Bolt Volly - Faerlina (H)
        54835  // Curse of the Plaguebringer - Noth (H)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 10;
    });

    ApplySpellFix({
        50312  // Unholy Frenzy
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 15;
    });

    // Lock and Load (Rank 1)
    ApplySpellFix({ 56342 }, [](SpellInfo* spellInfo)
    {
        // @workaround: Delete dummy effect from rank 1
        // effect apply aura has NO_TARGET but core still applies it to caster (same as above)
        spellInfo->_GetEffect(EFFECT_2).Effect = SPELL_EFFECT_NONE;
    });

    // Roar of Sacrifice
    ApplySpellFix({ 53480 }, [](SpellInfo* spellInfo)
    {
        // missing spell effect 2 data, taken from 4.3.4
        spellInfo->_GetEffect(EFFECT_1).Effect = SPELL_EFFECT_APPLY_AURA;
        spellInfo->_GetEffect(EFFECT_1).ApplyAuraName = SPELL_AURA_DUMMY;
        spellInfo->_GetEffect(EFFECT_1).MiscValue = 127;
        spellInfo->_GetEffect(EFFECT_1).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_TARGET_ALLY);
    });

    // Fingers of Frost
    ApplySpellFix({ 44544 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).SpellClassMask = flag128(685904631, 1151048, 0);
    });

    // Magic Suppression - DK
    ApplySpellFix({ 49224, 49610, 49611 }, [](SpellInfo* spellInfo)
    {
        spellInfo->ProcCharges = 0;
    });

    // Death and Decay
    ApplySpellFix({ 52212 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx6 |= SPELL_ATTR6_IGNORE_PHASE_SHIFT;
    });

    // Oscillation Field
    ApplySpellFix({ 37408 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_DOT_STACKING_RULE;
    });

    // Everlasting Affliction
    ApplySpellFix({ 47201, 47202, 47203, 47204, 47205 }, [](SpellInfo* spellInfo)
    {
        // add corruption to affected spells
        spellInfo->_GetEffect(EFFECT_1).SpellClassMask[0] |= 2;
    });

    // Renewed Hope
    ApplySpellFix({
        57470, // (Rank 1)
        57472  // (Rank 2)
    }, [](SpellInfo* spellInfo)
    {
        // should also affect Flash Heal
        spellInfo->_GetEffect(EFFECT_0).SpellClassMask[0] |= 0x800;
    });

    // Crafty's Ultra-Advanced Proto-Typical Shortening Blaster
    ApplySpellFix({ 51912 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).ApplyAuraPeriod = 3000;
    });

    // Desecration Arm - 36 instead of 37 - typo? :/
    ApplySpellFix({ 29809 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetARadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_7_YARDS);
    });

    // In sniff caster hits multiple targets
    ApplySpellFix({
        73725, // [DND] Test Cheer
        73835, // [DND] Test Salute
        73836  // [DND] Test Roar
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetBRadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_50_YARDS); // 50yd
    });

    // In sniff caster hits multiple targets
    ApplySpellFix({
        73837, // [DND] Test Dance
        73886  // [DND] Test Stop Dance
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetBRadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_150_YARDS); // 150yd
    });

    // Master Shapeshifter: missing stance data for forms other than bear - bear version has correct data
    // To prevent aura staying on target after talent unlearned
    ApplySpellFix({ 48420 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Stances = UI64LIT(1) << (FORM_CAT_FORM - 1);
    });

    ApplySpellFix({ 48421 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Stances = UI64LIT(1) << (FORM_MOONKIN_FORM - 1);
    });

    ApplySpellFix({ 48422 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Stances = UI64LIT(1) << (FORM_TREE_OF_LIFE - 1);
    });

    // Improved Shadowform (Rank 1)
    ApplySpellFix({ 47569 }, [](SpellInfo* spellInfo)
    {
        // with this spell atrribute aura can be stacked several times
        spellInfo->Attributes &= ~SPELL_ATTR0_NOT_SHAPESHIFTED;
    });

    // Hymn of Hope
    ApplySpellFix({ 64904 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_1).ApplyAuraName = SPELL_AURA_MOD_INCREASE_ENERGY_PERCENT;
    });

    // Improved Stings (Rank 2)
    ApplySpellFix({ 19465 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_2).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
    });

    // Nether Portal - Perseverence
    ApplySpellFix({ 30421 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_2).BasePoints += 30000;
    });

    // Natural shapeshifter
    ApplySpellFix({ 16834, 16835 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(21);
    });

    // Ebon Plague
    ApplySpellFix({ 65142 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 &= ~SPELL_ATTR3_DOT_STACKING_RULE;
    });

    // Ebon Plague
    ApplySpellFix({ 51735, 51734, 51726 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_DOT_STACKING_RULE;
        spellInfo->SpellFamilyFlags[2] = 0x10;
        spellInfo->_GetEffect(EFFECT_1).ApplyAuraName = SPELL_AURA_MOD_DAMAGE_PERCENT_TAKEN;
    });

    // Parasitic Shadowfiend Passive
    ApplySpellFix({ 41913 }, [](SpellInfo* spellInfo)
    {
        // proc debuff, and summon infinite fiends
        spellInfo->_GetEffect(EFFECT_0).ApplyAuraName = SPELL_AURA_DUMMY;
    });

    ApplySpellFix({
        27892, // To Anchor 1
        27928, // To Anchor 1
        27935, // To Anchor 1
        27915, // Anchor to Skulls
        27931, // Anchor to Skulls
        27937, // Anchor to Skulls
        16177, // Ancestral Fortitude (Rank 1)
        16236, // Ancestral Fortitude (Rank 2)
        16237, // Ancestral Fortitude (Rank 3)
        47930, // Grace
        45145, // Snake Trap Effect (Rank 1)
        13812, // Explosive Trap Effect (Rank 1)
        14314, // Explosive Trap Effect (Rank 2)
        14315, // Explosive Trap Effect (Rank 3)
        27026, // Explosive Trap Effect (Rank 4)
        49064, // Explosive Trap Effect (Rank 5)
        49065, // Explosive Trap Effect (Rank 6)
        43446, // Explosive Trap Effect (Hexlord Malacrass)
        50661, // Weakened Resolve
        68979, // Unleashed Souls
        48714, // Compelled
        7853   // The Art of Being a Water Terror: Force Cast on Player
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(13);
    });

    // Wrath of the Plaguebringer
    ApplySpellFix({ 29214, 54836 }, [](SpellInfo* spellInfo)
    {
        // target allys instead of enemies, target A is src_caster, spells with effect like that have ally target
        // this is the only known exception, probably just wrong data
        spellInfo->_GetEffect(EFFECT_0).TargetB = SpellImplicitTargetInfo(TARGET_UNIT_SRC_AREA_ALLY);
        spellInfo->_GetEffect(EFFECT_1).TargetB = SpellImplicitTargetInfo(TARGET_UNIT_SRC_AREA_ALLY);
    });

    // Wind Shear
    ApplySpellFix({ 57994 }, [](SpellInfo* spellInfo)
    {
        // improper data for EFFECT_1 in 3.3.5 DBC, but is correct in 4.x
        spellInfo->_GetEffect(EFFECT_1).Effect = SPELL_EFFECT_MODIFY_THREAT_PERCENT;
        spellInfo->_GetEffect(EFFECT_1).BasePoints = -6; // -5%
    });

    ApplySpellFix({
        50526, // Wandering Plague
        15290  // Vampiric Embrace
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR2_NO_INITIAL_THREAT;
    });

    // Vampiric Touch (dispel effect)
    ApplySpellFix({ 64085 }, [](SpellInfo* spellInfo)
    {
        // copy from similar effect of Unstable Affliction (31117)
        spellInfo->AttributesEx4 |= SPELL_ATTR4_IGNORE_DAMAGE_TAKEN_MODIFIERS;
        spellInfo->AttributesEx6 |= SPELL_ATTR6_IGNORE_CASTER_DAMAGE_MODIFIERS;
    });

    // Improved Devouring Plague
    ApplySpellFix({ 63675 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_IGNORE_CASTER_MODIFIERS;
    });

    // Deep Wounds
    ApplySpellFix({ 12721 }, [](SpellInfo* spellInfo)
    {
        // shouldnt ignore resillience or damage taken auras because its damage is not based off a spell.
        spellInfo->AttributesEx4 &= ~SPELL_ATTR4_IGNORE_DAMAGE_TAKEN_MODIFIERS;
    });

    // Tremor Totem (instant pulse)
    ApplySpellFix({ 8145 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx2 |= SPELL_ATTR2_IGNORE_LINE_OF_SIGHT;
        spellInfo->AttributesEx5 |= SPELL_ATTR5_EXTRA_INITIAL_PERIOD;
    });

    // Earthbind Totem (instant pulse)
    ApplySpellFix({ 6474 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx5 |= SPELL_ATTR5_EXTRA_INITIAL_PERIOD;
    });

    // Flametongue Totem (Aura)
    ApplySpellFix({
        52109, // rank 1
        52110, // rank 2
        52111, // rank 3
        52112, // rank 4
        52113, // rank 5
        58651, // rank 6
        58654, // rank 7
        58655  // rank 8
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
        spellInfo->_GetEffect(EFFECT_1).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
        spellInfo->_GetEffect(EFFECT_0).TargetB = SpellImplicitTargetInfo();
        spellInfo->_GetEffect(EFFECT_1).TargetB = SpellImplicitTargetInfo();
    });

    // Marked for Death
    ApplySpellFix({
        53241, // (Rank 1)
        53243, // (Rank 2)
        53244, // (Rank 3)
        53245, // (Rank 4)
        53246  // (Rank 5)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).SpellClassMask = flag128(0x00067801, 0x10820001, 0x00000801);
    });

    ApplySpellFix({
        70728, // Exploit Weakness (needs target selection script)
        70840  // Devious Minds (needs target selection script)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
        spellInfo->_GetEffect(EFFECT_0).TargetB = SpellImplicitTargetInfo(TARGET_UNIT_PET);
    });

    // Culling The Herd (needs target selection script)
    ApplySpellFix({ 70893 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
        spellInfo->_GetEffect(EFFECT_0).TargetB = SpellImplicitTargetInfo(TARGET_UNIT_MASTER);
    });

    // Sigil of the Frozen Conscience
    ApplySpellFix({ 54800 }, [](SpellInfo* spellInfo)
    {
        // change class mask to custom extended flags of Icy Touch
        // this is done because another spell also uses the same SpellFamilyFlags as Icy Touch
        // SpellFamilyFlags[0] & 0x00000040 in SPELLFAMILY_DEATHKNIGHT is currently unused (3.3.5a)
        // this needs research on modifier applying rules, does not seem to be in Attributes fields
        spellInfo->_GetEffect(EFFECT_0).SpellClassMask = flag128(0x00000040, 0x00000000, 0x00000000);
    });

    // Idol of the Flourishing Life
    ApplySpellFix({ 64949 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).SpellClassMask = flag128(0x00000000, 0x02000000, 0x00000000);
        spellInfo->_GetEffect(EFFECT_0).ApplyAuraName = SPELL_AURA_ADD_FLAT_MODIFIER;
    });

    ApplySpellFix({
        34231, // Libram of the Lightbringer
        60792, // Libram of Tolerance
        64956  // Libram of the Resolute
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).SpellClassMask = flag128(0x80000000, 0x00000000, 0x00000000);
        spellInfo->_GetEffect(EFFECT_0).ApplyAuraName = SPELL_AURA_ADD_FLAT_MODIFIER;
    });

    ApplySpellFix({
        28851, // Libram of Light
        28853, // Libram of Divinity
        32403  // Blessed Book of Nagrand
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).SpellClassMask = flag128(0x40000000, 0x00000000, 0x00000000);
        spellInfo->_GetEffect(EFFECT_0).ApplyAuraName = SPELL_AURA_ADD_FLAT_MODIFIER;
    });

    // Ride Carpet
    ApplySpellFix({ 45602 }, [](SpellInfo* spellInfo)
    {
        // force seat 0, vehicle doesn't have the required seat flags for "no seat specified (-1)"
        spellInfo->_GetEffect(EFFECT_0).BasePoints = 0;
    });

    ApplySpellFix({
        64745, // Item - Death Knight T8 Tank 4P Bonus
        64936  // Item - Warrior T8 Protection 4P Bonus
    }, [](SpellInfo* spellInfo)
    {
        // 100% chance of procc'ing, not -10% (chance calculated in PrepareTriggersExecutedOnHit)
        spellInfo->_GetEffect(EFFECT_0).BasePoints = 100;
    });

    // Entangling Roots -- Nature's Grasp Proc
    ApplySpellFix({
        19970, // (Rank 6)
        19971, // (Rank 5)
        19972, // (Rank 4)
        19973, // (Rank 3)
        19974, // (Rank 2)
        19975, // (Rank 1)
        27010, // (Rank 7)
        53313  // (Rank 8)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->CastTimeEntry = sSpellCastTimesStore.LookupEntry(1);
    });

    // Easter Lay Noblegarden Egg Aura
    ApplySpellFix({ 61719 }, [](SpellInfo* spellInfo)
    {
        // Interrupt flags copied from aura which this aura is linked with
        spellInfo->AuraInterruptFlags = SpellAuraInterruptFlags::HostileActionReceived | SpellAuraInterruptFlags::Damage;
    });

    // Death Knight T10 Tank 2P Bonus
    ApplySpellFix({ 70650 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).ApplyAuraName = SPELL_AURA_ADD_PCT_MODIFIER;
    });

    ApplySpellFix({
        6789,  // Warlock - Death Coil (Rank 1)
        17925, // Warlock - Death Coil (Rank 2)
        17926, // Warlock - Death Coil (Rank 3)
        27223, // Warlock - Death Coil (Rank 4)
        47859, // Warlock - Death Coil (Rank 5)
        47860, // Warlock - Death Coil (Rank 6)
        71838, // Drain Life - Bryntroll Normal
        71839  // Drain Life - Bryntroll Heroic
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx2 |= SPELL_ATTR2_CANT_CRIT;
    });

    ApplySpellFix({
        51597, // Summon Scourged Captive
        56606, // Ride Jokkum
        61791  // Ride Vehicle (Yogg-Saron)
    }, [](SpellInfo* spellInfo)
    {
        /// @todo: remove this when basepoints of all Ride Vehicle auras are calculated correctly
        spellInfo->_GetEffect(EFFECT_0).BasePoints = 1;
    });

    // Summon Scourged Captive
    ApplySpellFix({ 51597 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).DieSides = 0;
    });

    // Black Magic
    ApplySpellFix({ 59630 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Attributes |= SPELL_ATTR0_PASSIVE;
    });

    ApplySpellFix({
        17364, // Stormstrike
        48278, // Paralyze
        53651  // Light's Beacon
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_DOT_STACKING_RULE;
    });

    ApplySpellFix({
        51798, // Brewfest - Relay Race - Intro - Quest Complete
        47134  // Quest Complete
    }, [](SpellInfo* spellInfo)
    {
        //! HACK: This spell break quest complete for alliance and on retail not used
        spellInfo->_GetEffect(EFFECT_0).Effect = SPELL_EFFECT_NONE;
    });

    ApplySpellFix({
        47476, // Deathknight - Strangulate
        15487, // Priest - Silence
        5211,  // Druid - Bash  - R1
        6798,  // Druid - Bash  - R2
        8983   // Druid - Bash  - R3
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx7 |= SPELL_ATTR7_CAN_CAUSE_INTERRUPT;
    });

    // Guardian Spirit
    ApplySpellFix({ 47788 }, [](SpellInfo* spellInfo)
    {
        spellInfo->ExcludeTargetAuraSpell = 72232; // Weakened Spirit
    });

    ApplySpellFix({
        15538, // Gout of Flame
        42490, // Energized!
        42492, // Cast Energized
        43115  // Plague Vial
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx |= SPELL_ATTR1_NO_THREAT;
    });

    ApplySpellFix({
        46842, // Flame Ring
        46836  // Flame Patch
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo();
    });

    // Test Ribbon Pole Channel
    ApplySpellFix({ 29726 }, [](SpellInfo* spellInfo)
    {
        spellInfo->ChannelInterruptFlags &= ~SpellAuraInterruptFlags::Action;
    });

    ApplySpellFix({
        42767, // Sic'em
        43092  // Stop the Ascension!: Halfdan's Soul Destruction
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_NEARBY_ENTRY);
    });

    // Polymorph (Six Demon Bag)
    ApplySpellFix({ 14621 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(4); // Medium Range
    });

    // Concussive Barrage
    ApplySpellFix({ 35101 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(155); // Hunter Range (Long)
    });

    ApplySpellFix({
        44327, // Trained Rock Falcon/Hawk Hunting
        44408  // Trained Rock Falcon/Hawk Hunting
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->Speed = 0.f;
    });

    ApplySpellFix({
        51675,  // Rogue - Unfair Advantage (Rank 1)
        51677   // Rogue - Unfair Advantage (Rank 2)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(2); // 5 yards
    });

    ApplySpellFix({
        55741, // Desecration (Rank 1)
        68766, // Desecration (Rank 2)
        57842  // Killing Spree (Off hand damage)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(2); // Melee Range
    });

    // Safeguard
    ApplySpellFix({
        46946, // (Rank 1)
        46947  // (Rank 2)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(34); // Twenty-Five yards
    });

    // Summon Corpse Scarabs
    ApplySpellFix({ 28864, 29105 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetARadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_10_YARDS);
    });

    ApplySpellFix({
        37851, // Tag Greater Felfire Diemetradon
        37918  // Arcano-pince
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RecoveryTime = 3000;
    });

    // Jormungar Strike
    ApplySpellFix({ 56513 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RecoveryTime = 2000;
    });

    ApplySpellFix({
        54997, // Cast Net (tooltip says 10s but sniffs say 6s)
        56524  // Acid Breath
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RecoveryTime = 6000;
    });

    ApplySpellFix({
        47911, // EMP
        48620, // Wing Buffet
        51752  // Stampy's Stompy-Stomp
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RecoveryTime = 10000;
    });

    ApplySpellFix({
        37727, // Touch of Darkness
        54996  // Ice Slick (tooltip says 20s but sniffs say 12s)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RecoveryTime = 12000;
    });

    // Signal Helmet to Attack
    ApplySpellFix({ 51748 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RecoveryTime = 15000;
    });

    ApplySpellFix({
        51756, // Charge
        37919, //Arcano-dismantle
        37917  //Arcano-Cloak
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->RecoveryTime = 20000;
    });

    // Summon Frigid Bones
    ApplySpellFix({ 53525 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(4); // 2 minutes
    });

    // Dark Conclave Ritualist Channel
    ApplySpellFix({ 38469 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(6);  // 100yd
    });

    //
    // VIOLET HOLD SPELLS
    //
    // Water Globule (Ichoron)
    ApplySpellFix({ 54258, 54264, 54265, 54266, 54267 }, [](SpellInfo* spellInfo)
    {
        // in 3.3.5 there is only one radius in dbc which is 0 yards in this
        // use max radius from 4.3.4
        spellInfo->_GetEffect(EFFECT_0).TargetARadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_25_YARDS);
    });
    // ENDOF VIOLET HOLD

    //
    // ULDUAR SPELLS
    //
    // Pursued (Flame Leviathan)
    ApplySpellFix({ 62374 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetARadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_50000_YARDS);   // 50000yd
    });

    // Focused Eyebeam Summon Trigger (Kologarn)
    ApplySpellFix({ 63342 }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 1;
    });

    ApplySpellFix({
        62716, // Growth of Nature (Freya)
        65584, // Growth of Nature (Freya)
        64381  // Strength of the Pack (Auriaya)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_DOT_STACKING_RULE;
    });

    ApplySpellFix({
        63018, // Searing Light (XT-002)
        65121, // Searing Light (25m) (XT-002)
        63024, // Gravity Bomb (XT-002)
        64234  // Gravity Bomb (25m) (XT-002)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 1;
    });

    ApplySpellFix({
        64386, // Terrifying Screech (Auriaya)
        64389, // Sentinel Blast (Auriaya)
        64678  // Sentinel Blast (Auriaya)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(28); // 5 seconds, wrong DBC data?
    });

    // Summon Swarming Guardian (Auriaya)
    ApplySpellFix({ 64397 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(137); // 8y, Based in BFA effect radius
    });

    // Potent Pheromones (Freya)
    ApplySpellFix({ 64321 }, [](SpellInfo* spellInfo)
    {
        // spell should dispel area aura, but doesn't have the attribute
        // may be db data bug, or blizz may keep reapplying area auras every update with checking immunity
        // that will be clear if we get more spells with problem like this
        spellInfo->AttributesEx |= SPELL_ATTR1_IMMUNITY_PURGES_EFFECT;
    });

    // Blizzard (Thorim)
    ApplySpellFix({ 62576, 62602 }, [](SpellInfo* spellInfo)
    {
        // DBC data is wrong for EFFECT_0, it's a different dynobject target than EFFECT_1
        // Both effects should be shared by the same DynObject
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_DEST_CASTER_LEFT);
    });

    // Spinning Up (Mimiron)
    ApplySpellFix({ 63414 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetB = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
        spellInfo->ChannelInterruptFlags = SpellAuraInterruptFlags::None;
    });

    // Rocket Strike (Mimiron)
    ApplySpellFix({ 63036 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Speed = 0;
    });

    // Magnetic Field (Mimiron)
    ApplySpellFix({ 64668 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Mechanic = MECHANIC_NONE;
    });

    // Empowering Shadows (Yogg-Saron)
    ApplySpellFix({ 64468, 64486 }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 3;  // same for both modes?
    });

    // Cosmic Smash (Algalon the Observer)
    ApplySpellFix({ 62301 }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 1;
    });

    // Cosmic Smash (Algalon the Observer)
    ApplySpellFix({ 64598 }, [](SpellInfo* spellInfo)
    {
        spellInfo->MaxAffectedTargets = 3;
    });

    // Cosmic Smash (Algalon the Observer)
    ApplySpellFix({ 62293 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetB = SpellImplicitTargetInfo(TARGET_DEST_CASTER);
    });

    // Cosmic Smash (Algalon the Observer)
    ApplySpellFix({ 62311, 64596 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(6);  // 100yd
    });

    ApplySpellFix({
        64014, // Expedition Base Camp Teleport
        64024, // Conservatory Teleport
        64025, // Halls of Invention Teleport
        64028, // Colossal Forge Teleport
        64029, // Shattered Walkway Teleport
        64030, // Antechamber Teleport
        64031, // Scrapyard Teleport
        64032, // Formation Grounds Teleport
        65042  // Prison of Yogg-Saron Teleport
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_DEST_DB);
    });
    // ENDOF ULDUAR SPELLS

    //
    // TRIAL OF THE CRUSADER SPELLS
    //
    // Infernal Eruption
    ApplySpellFix({ 66258, 67901 }, [](SpellInfo* spellInfo)
    {
        // increase duration from 15 to 18 seconds because caster is already
        // unsummoned when spell missile hits the ground so nothing happen in result
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(85);
    });
    // ENDOF TRIAL OF THE CRUSADER SPELLS

    //
    // ICECROWN CITADEL SPELLS
    //
    ApplySpellFix({
        // THESE SPELLS ARE WORKING CORRECTLY EVEN WITHOUT THIS HACK
        // THE ONLY REASON ITS HERE IS THAT CURRENT GRID SYSTEM
        // DOES NOT ALLOW FAR OBJECT SELECTION (dist > 333)
        70781, // Light's Hammer Teleport
        70856, // Oratory of the Damned Teleport
        70857, // Rampart of Skulls Teleport
        70858, // Deathbringer's Rise Teleport
        70859, // Upper Spire Teleport
        70860, // Frozen Throne Teleport
        70861  // Sindragosa's Lair Teleport
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_DEST_DB);
    });

    // Shadow's Fate
    ApplySpellFix({ 71169 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_DOT_STACKING_RULE;
    });

    // Lock Players and Tap Chest
    ApplySpellFix({ 72347 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 &= ~SPELL_ATTR2_NO_INITIAL_THREAT;
    });

    ApplySpellFix({
        72378, // Blood Nova (Deathbringer Saurfang)
        73058, // Blood Nova (Deathbringer Saurfang)
        72769  // Scent of Blood (Deathbringer Saurfang)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetBRadiusEntry = spellInfo->_GetEffect(EFFECT_1).TargetBRadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_200_YARDS);
    });

    // Scent of Blood (Deathbringer Saurfang)
    ApplySpellFix({ 72771 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_1).TargetBRadiusEntry = sSpellRadiusStore.LookupEntry(EFFECT_RADIUS_200_YARDS);
    });

    // Resistant Skin (Deathbringer Saurfang adds)
    ApplySpellFix({ 72723 }, [](SpellInfo* spellInfo)
    {
        // this spell initially granted Shadow damage immunity, however it was removed but the data was left in client
        spellInfo->_GetEffect(EFFECT_2).Effect = SPELL_EFFECT_NONE;
    });

    // Coldflame Jets (Traps after Saurfang)
    ApplySpellFix({ 70460 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(1); // 10 seconds
    });

    ApplySpellFix({
        71412, // Green Ooze Summon (Professor Putricide)
        71415  // Orange Ooze Summon (Professor Putricide)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_TARGET_ANY);
    });

    // Ooze flood
    ApplySpellFix({ 69783, 69797, 69799, 69802 }, [](SpellInfo* spellInfo)
    {
        // Those spells are cast on creatures with same entry as caster while they have TARGET_UNIT_NEARBY_ENTRY.
        spellInfo->AttributesEx |= SPELL_ATTR1_EXCLUDE_CASTER;
    });

    // Awaken Plagued Zombies
    ApplySpellFix({ 71159 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(21);
    });

    // Volatile Ooze Beam Protection (Professor Putricide)
    ApplySpellFix({ 70530 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).Effect = SPELL_EFFECT_APPLY_AURA; // for an unknown reason this was SPELL_EFFECT_APPLY_AREA_AURA_RAID
    });

    // Mutated Strength (Professor Putricide)
    ApplySpellFix({ 71604, 72673, 72674, 72675 }, [](SpellInfo* spellInfo)
    {
        // THIS IS HERE BECAUSE COOLDOWN ON CREATURE PROCS WERE NOT IMPLEMENTED WHEN THE SCRIPT WAS WRITTEN
        spellInfo->_GetEffect(EFFECT_1).Effect = SPELL_EFFECT_NONE;
    });

    // Unbound Plague (Professor Putricide) (needs target selection script)
    ApplySpellFix({ 70911, 72854, 72855, 72856 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetB = SpellImplicitTargetInfo(TARGET_UNIT_TARGET_ENEMY);
    });

    // Empowered Flare (Blood Prince Council)
    ApplySpellFix({ 71708, 72785, 72786, 72787 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_IGNORE_CASTER_MODIFIERS;
    });

    // Swarming Shadows
    ApplySpellFix({ 71266, 72890 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RequiredAreasID = 0; // originally, these require area 4522, which is... outside of Icecrown Citadel
    });

    // Corruption
    ApplySpellFix({ 70602 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_DOT_STACKING_RULE;
    });

    // Column of Frost (visual marker)
    ApplySpellFix({ 70715 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(32); // 6 seconds (missing)
    });

    // Mana Void (periodic aura)
    ApplySpellFix({ 71085 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(9); // 30 seconds (missing)
    });

    // Summon Suppressor (needs target selection script)
    ApplySpellFix({ 70936 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_TARGET_ANY);
        spellInfo->_GetEffect(EFFECT_0).TargetB = SpellImplicitTargetInfo();
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(157); // 90yd
    });

    // Sindragosa's Fury
    ApplySpellFix({ 70598 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_DEST_DEST);
    });

    // Frost Bomb
    ApplySpellFix({ 69846 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Speed = 0.0f;    // This spell's summon happens instantly
    });

    // Chilled to the Bone
    ApplySpellFix({ 70106 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_IGNORE_CASTER_MODIFIERS;
        spellInfo->AttributesEx6 |= SPELL_ATTR6_IGNORE_CASTER_DAMAGE_MODIFIERS;
    });

    // Ice Lock
    ApplySpellFix({ 71614 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Mechanic = MECHANIC_STUN;
    });

    // Defile
    ApplySpellFix({ 72762 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(559); // 53 seconds
    });

    // Defile
    ApplySpellFix({ 72743 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(22); // 45 seconds
    });

    // Val'kyr Target Search
    ApplySpellFix({ 69030 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Attributes |= SPELL_ATTR0_NO_IMMUNITIES;
    });

    // Raging Spirit Visual
    ApplySpellFix({ 69198 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(13); // 50000yd
    });

    // Harvest Soul
    ApplySpellFix({ 73655 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_IGNORE_CASTER_MODIFIERS;
    });

    // Summon Shadow Trap
    ApplySpellFix({ 73540 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(3); // 60 seconds
    });

    // Shadow Trap (visual)
    ApplySpellFix({ 73530 }, [](SpellInfo* spellInfo)
    {
        spellInfo->DurationEntry = sSpellDurationStore.LookupEntry(27); // 3 seconds
    });
    // ENDOF ICECROWN CITADEL SPELLS

    //
    // RUBY SANCTUM SPELLS
    //
    // Twilight Mending
    ApplySpellFix({ 75509 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx6 |= SPELL_ATTR6_IGNORE_PHASE_SHIFT;
        spellInfo->AttributesEx2 |= SPELL_ATTR2_IGNORE_LINE_OF_SIGHT;
    });

    // Combustion and Consumption Heroic versions lacks radius data
    ApplySpellFix({ 75875 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_0).Mechanic = MECHANIC_NONE;
        spellInfo->_GetEffect(EFFECT_1).Mechanic = MECHANIC_SNARE;
    });
    // ENDOF RUBY SANCTUM SPELLS

    //
    // EYE OF ETERNITY SPELLS
    //
    ApplySpellFix({
        // All spells below work even without these changes. The LOS attribute is due to problem
        // from collision between maps & gos with active destroyed state.
        57473, // Arcane Storm bonus explicit visual spell
        57431, // Summon Static Field
        56091, // Flame Spike (Wyrmrest Skytalon)
        56092, // Engulf in Flames (Wyrmrest Skytalon)
        57090, // Revivify (Wyrmrest Skytalon)
        57143  // Life Burst (Wyrmrest Skytalon)
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx2 |= SPELL_ATTR2_IGNORE_LINE_OF_SIGHT;
    });

    // Arcane Barrage (cast by players and NONMELEEDAMAGELOG with caster Scion of Eternity (original caster)).
    ApplySpellFix({ 63934 }, [](SpellInfo* spellInfo)
    {
        // This would never crit on retail and it has attribute for SPELL_ATTR3_NO_DONE_BONUS because is handled from player,
        // until someone figures how to make scions not critting without hack and without making them main casters this should stay here.
        spellInfo->AttributesEx2 |= SPELL_ATTR2_CANT_CRIT;
    });
    // ENDOF EYE OF ETERNITY SPELLS

    //
    // OCULUS SPELLS
    //
    ApplySpellFix({
        // The spells below are here because their effect 1 is giving warning due to
        // triggered spell not found in any dbc and is missing from encounter source* of data.
        // Even judged as clientside these spells can't be guessed for* now.
        49462, // Call Ruby Drake
        49461, // Call Amber Drake
        49345  // Call Emerald Drake
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_1).Effect = SPELL_EFFECT_NONE;
    });
    // ENDOF OCULUS SPELLS

    // Introspection
    ApplySpellFix({ 40055, 40165, 40166, 40167 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Attributes |= SPELL_ATTR0_AURA_IS_DEBUFF;
    });

    // Chains of Ice
    ApplySpellFix({ 45524 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_2).TargetA = SpellImplicitTargetInfo();
    });

    // Minor Fortitude - alistar TODO
    /*ApplySpellFix({ 2378 }, [](SpellInfo* spellInfo)
    {
        spellInfo->ManaCost = 0;
        spellInfo->ManaPerSecond = 0;
    });*/

    // Threatening Gaze
    ApplySpellFix({ 24314 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AuraInterruptFlags |= SpellAuraInterruptFlags::Action | SpellAuraInterruptFlags::Moving | SpellAuraInterruptFlags::Anim;
    });

    //
    // ISLE OF CONQUEST SPELLS
    //
    // Teleport
    ApplySpellFix({ 66551 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(13); // 50000yd
    });
    // ENDOF ISLE OF CONQUEST SPELLS

    // Aura of Fear
    ApplySpellFix({ 40453 }, [](SpellInfo* spellInfo)
    {
        // Bad DBC data? Copying 25820 here due to spell description
        // either is a periodic with chance on tick, or a proc

        spellInfo->_GetEffect(EFFECT_0).ApplyAuraName = SPELL_AURA_PROC_TRIGGER_SPELL;
        spellInfo->_GetEffect(EFFECT_0).ApplyAuraPeriod = 0;
        spellInfo->ProcChance = 10;
    });

    // Survey Sinkholes
    ApplySpellFix({ 45853 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(5); // 40 yards
    });

    ApplySpellFix({
        41485, // Deadly Poison - Black Temple
        41487  // Envenom - Black Temple
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx6 |= SPELL_ATTR6_IGNORE_PHASE_SHIFT;
    });

    ApplySpellFix({
        // Proc attribute correction
        // Remove procflags from test/debug/deprecated spells to avoid DB Errors
        2479,  // Honorless Target
        3232,  // Gouge Stun Test
        3409,  // Crippling Poison
        4312,  // Strider Presence
        5707,  // Lifestone Regeneration
        5760,  // Mind-numbing Poison
        6727,  // Poison Mushroom
        6940,  // Hand of Sacrifice (handled remove in split hook)
        6984,  // Frost Shot (Rank 2)
        7164,  // Defensive Stance
        7288,  // Immolate Cumulative (TEST) (Rank 1)
        7291,  // Food (TEST)
        7331,  // Healing Aura (TEST) (Rank 1)
        7366,  // Berserker Stance
        7824,  // Blacksmithing Skill +10
        12551, // Frost Shot
        13218, // Wound Poison (Rank 1)
        13222, // Wound Poison II (Rank 2)
        13223, // Wound Poison III (Rank 3)
        13224, // Wound Poison IV (Rank 4)
        14795, // Venomhide Poison
        16610, // Razorhide
        18099, // Chill Nova
        18499, // Berserker Rage (extra rage implemented in Unit::RewardRage)
        18802, // Frost Shot
        20000, // Alexander's Test Periodic Aura
        21163, // Polished Armor (Rank 1)
        22818, // Mol'dar's Moxie
        22820, // Slip'kik's Savvy
        23333, // Warsong Flag
        23335, // Silverwing Flag
        25160, // Sand Storm
        27189, // Wound Poison V (Rank 5)
        28313, // Aura of Fear
        28726, // Nightmare Seed
        28754, // Fury of the Ashbringer
        30802, // Unleashed Rage (Rank 1)
        31481, // Lung Burst
        32430, // Battle Standard
        32431, // Battle Standard
        32447, // Travel Form
        33370, // Spell Haste
        33807, // Abacus of Violent Odds
        33891, // Tree of Life (Shapeshift)
        34132, // Gladiator's Totem of the Third Wind
        34135, // Libram of Justice
        34666, // Tamed Pet Passive 08 (DND)
        34667, // Tamed Pet Passive 09 (DND)
        34775, // Dragonspine Flurry
        34889, // Fire Breath (Rank 1)
        34976, // Netherstorm Flag
        35131, // Bladestorm
        35244, // Choking Vines
        35323, // Fire Breath (Rank 2)
        35336, // Energizing Spores
        36148, // Chill Nova
        36613, // Aspect of the Spirit Hunter
        36786, // Soul Chill
        37174, // Perceived Weakness
        37482, // Exploited Weakness
        37526, // Battle Rush
        37588, // Dive
        37985, // Fire Breath
        38317, // Forgotten Knowledge
        38843, // Soul Chill
        39015, // Atrophic Blow
        40396, // Fel Infusion
        40603, // Taunt Gurtogg
        40803, // Ron's Test Buff
        40879, // Prismatic Shield (no longer used since patch 2.2/adaptive prismatic shield)
        41341, // Balance of Power (implemented by hooking absorb)
        41435, // The Twin Blades of Azzinoth
        42369, // Merciless Libram of Justice
        42371, // Merciless Gladiator's Totem of the Third Wind
        42636, // Birmingham Tools Test 3
        43727, // Vengeful Libram of Justice
        43729, // Vengeful Gladiator's Totem of the Third Wind
        43817, // Focused Assault
        44305, // You're a ...! (Effects2)
        44586, // Prayer of Mending (unknown, unused aura type)
        45384, // Birmingham Tools Test 4
        45433, // Birmingham Tools Test 5
        46093, // Brutal Libram of Justice
        46099, // Brutal Gladiator's Totem of the Third Wind
        46705, // Honorless Target
        49145, // Spell Deflection (Rank 1) (implemented by hooking absorb)
        49883, // Flames
        50365, // Improved Blood Presence (Rank 1)
        50371, // Improved Blood Presence (Rank 2)
        50462, // Anti-Magic Zone (implemented by hooking absorb)

        50498, // Savage Rend (Rank 1) - proc from Savage Rend moved from attack itself to autolearn aura 50871
        53578, // Savage Rend (Rank 2)
        53579, // Savage Rend (Rank 3)
        53580, // Savage Rend (Rank 4)
        53581, // Savage Rend (Rank 5)
        53582, // Savage Rend (Rank 6)

        50655, // Frost Cut
        50995, // Empowered Blood Presence (Rank 1)
        51809, // First Aid
        53032, // Flurry of Claws
        55482, // Fire Breath (Rank 3)
        55483, // Fire Breath (Rank 4)
        55484, // Fire Breath (Rank 5)
        55485, // Fire Breath (Rank 6)
        57974, // Wound Poison VI (Rank 6)
        57975, // Wound Poison VII (Rank 7)
        60062, // Essence of Life
        60302, // Meteorite Whetstone
        60437, // Grim Toll
        60492, // Embrace of the Spider
        62142, // Improved Chains of Ice (Rank 3)
        63024, // Gravity Bomb
        64205, // Divine Sacrifice (handled remove in split hook)
        64772, // Comet's Trail
        65004, // Alacrity of the Elements
        65019, // Mjolnir Runestone
        65024, // Implosion

        66334, // Mistress' Kiss - currently not used in script, need implement?
        67905, // Mistress' Kiss
        67906, // Mistress' Kiss
        67907, // Mistress' Kiss

        71003, // Vegard's Touch

        72151, // Frenzied Bloodthirst - currently not used in script, need implement?
        72648, // Frenzied Bloodthirst
        72649, // Frenzied Bloodthirst
        72650, // Frenzied Bloodthirst

        72559, // Birmingham Tools Test 3
        72560, // Birmingham Tools Test 3
        72561, // Birmingham Tools Test 5
        72980  // Shadow Resonance
    }, [](SpellInfo* spellInfo)
    {
        spellInfo->ProcFlags = std::array<int32, 2>{};
    });

    // Baron Rivendare (Stratholme) - Unholy Aura
    ApplySpellFix({ 17466, 17467 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR2_NO_INITIAL_THREAT;
    });

    // Spore - Spore Visual
    ApplySpellFix({ 42525 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AttributesEx3 |= SPELL_ATTR3_ALLOW_AURA_WHILE_DEAD;
        spellInfo->AttributesEx2 |= SPELL_ATTR2_ALLOW_DEAD_TARGET;
    });

    // Death's Embrace
    ApplySpellFix({ 47198, 47199, 47200 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_1).SpellClassMask[0] |= 0x00004000; // Drain soul
    });

    // Soul Sickness (Forge of Souls)
    ApplySpellFix({ 69131 }, [](SpellInfo* spellInfo)
    {
        spellInfo->_GetEffect(EFFECT_1).ApplyAuraName = SPELL_AURA_MOD_DECREASE_SPEED;
    });

    // Headless Horseman Climax - Return Head (Hallow End)
    // Headless Horseman Climax - Body Regen (confuse only - removed on death)
    // Headless Horseman Climax - Head Is Dead
    ApplySpellFix({ 42401, 43105, 42428 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Attributes |= SPELL_ATTR0_NO_IMMUNITIES;
    });

    // Sacred Cleansing
    ApplySpellFix({ 53659 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(5); // 40yd
    });

    // Seals of the Pure should affect Seal of Righteousness
    ApplySpellFix({ 20224, 20225, 20330, 20331, 20332 }, [](SpellInfo* spellInfo)
    {
        ApplySpellEffectFix(spellInfo, EFFECT_0, [](SpellEffectInfo* spellEffectInfo)
        {
            spellEffectInfo->SpellClassMask[1] |= 0x20000000;
        });
    });

    // Holiday - Midsummer, Ribbon Pole Periodic Visual
    ApplySpellFix({ 45406 }, [](SpellInfo* spellInfo)
    {
        spellInfo->RangeEntry = sSpellRangeStore.LookupEntry(7); // 10yd
        spellInfo->_GetEffect(EFFECT_0).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_TARGET_ANY);
        spellInfo->AuraInterruptFlags |= (SpellAuraInterruptFlags::Mount | SpellAuraInterruptFlags::Damage | SpellAuraInterruptFlags::Interacting);
    });

    // Bind
    ApplySpellFix({ 3286 }, [](SpellInfo* spellInfo)
    {
        // Alistar for some reason blizzard created a custom spell to create a hearthstone when settings bind location
        ApplySpellEffectFix(spellInfo, EFFECT_1, [](SpellEffectInfo* spellEffectInfo)
        {
            spellEffectInfo->Effect = SPELL_EFFECT_CREATE_ITEM;
            spellEffectInfo->TriggerSpell = 0;
            spellEffectInfo->ItemType = 6948; // Hearthstone
        });
    });

    // Demonic Circle - Teleport casterAuraSpell
    ApplySpellFix({ 62388 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AuraInterruptFlags |= (SpellAuraInterruptFlags::LeaveWorld | SpellAuraInterruptFlags::EnterWorld);
    });

    // Drain soul
    ApplySpellFix({ 1120, 8288, 8289, 11675, 27217, 47855 }, [](SpellInfo* spellInfo)
    {
        spellInfo->ProcCharges = 0;
    });

    // Flare - Make the effect instant
    ApplySpellFix({ 1543 }, [](SpellInfo* spellInfo)
    {
        spellInfo->Speed = 0;
    });

    // Ashenvale Outrunner Sneak
    // Stealth
    ApplySpellFix({ 20540, 32199 }, [](SpellInfo* spellInfo)
    {
        spellInfo->AuraInterruptFlags |= (SpellAuraInterruptFlags::Attacking | SpellAuraInterruptFlags::Action);
    });

    for (SpellInfo const& s : mSpellInfoMap)
    {
        SpellInfo* spellInfo = &const_cast<SpellInfo&>(s);
        if (!spellInfo)
            continue;

        // Fix range for trajectory triggered spell
        for (SpellEffectInfo const& spellEffectInfo : spellInfo->GetEffects())
        {
            if (spellEffectInfo.IsEffect() && (spellEffectInfo.TargetA.GetTarget() == TARGET_DEST_TRAJ || spellEffectInfo.TargetB.GetTarget() == TARGET_DEST_TRAJ))
            {
                // Get triggered spell if any
                for (SpellInfo const& spellInfoTrigger : _GetSpellInfo(spellEffectInfo.TriggerSpell))
                {
                    float maxRangeMain = spellInfo->GetMaxRange();
                    float maxRangeTrigger = spellInfoTrigger.GetMaxRange();

                    // check if triggered spell has enough max range to cover trajectory
                    if (maxRangeTrigger < maxRangeMain)
                        const_cast<SpellInfo&>(spellInfoTrigger).RangeEntry = spellInfo->RangeEntry;
                }
            }

            switch (spellEffectInfo.Effect)
            {
                case SPELL_EFFECT_CHARGE:
                case SPELL_EFFECT_CHARGE_DEST:
                case SPELL_EFFECT_JUMP:
                case SPELL_EFFECT_JUMP_DEST:
                case SPELL_EFFECT_LEAP_BACK:
                    if (!spellInfo->Speed && !spellInfo->SpellFamilyName && !spellInfo->HasAttribute(SPELL_ATTR9_SPECIAL_DELAY_CALCULATION))
                        spellInfo->Speed = SPEED_CHARGE;
                    break;
                case SPELL_EFFECT_APPLY_AURA:
                    // special aura updates each 30 seconds
                    if (spellEffectInfo.ApplyAuraName == SPELL_AURA_MOD_ATTACK_POWER_OF_ARMOR)
                        const_cast<SpellEffectInfo&>(spellEffectInfo).ApplyAuraPeriod = 30 * IN_MILLISECONDS;
                    break;
                default:
                    break;
            }

            if (spellEffectInfo.TargetA.GetSelectionCategory() == TARGET_SELECT_CATEGORY_CONE || spellEffectInfo.TargetB.GetSelectionCategory() == TARGET_SELECT_CATEGORY_CONE)
                if (G3D::fuzzyEq(spellInfo->ConeAngle, 0.f))
                    spellInfo->ConeAngle = 90.f;

            // @TODO 340 not sure if we need this
            // Passive talent auras cannot target pets
            //if (spellInfo->IsPassive() && sDB2Manager.GetTalentSpellCost(spellInfo->Id))
            //    if (spellEffectInfo.TargetA.GetTarget() == TARGET_UNIT_PET)
            //        spellEffectInfo.TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);

            // Area auras may not target area (they're self cast)
            if (spellEffectInfo.IsAreaAuraEffect() && spellEffectInfo.IsTargetingArea())
            {
                const_cast<SpellEffectInfo&>(spellEffectInfo).TargetA = SpellImplicitTargetInfo(TARGET_UNIT_CASTER);
                const_cast<SpellEffectInfo&>(spellEffectInfo).TargetB = SpellImplicitTargetInfo(0);
            }
        }

        // disable proc for magnet auras, they're handled differently
        if (spellInfo->HasAura(SPELL_AURA_SPELL_MAGNET))
            spellInfo->ProcFlags = std::array<int32, 2>{};

        // due to the way spell system works, unit would change orientation in Spell::_cast
        if (spellInfo->HasAura(SPELL_AURA_CONTROL_VEHICLE))
            spellInfo->AttributesEx5 |= SPELL_ATTR5_AI_DOESNT_FACE_TARGET;

        if (spellInfo->ActiveIconFileDataId == 135754)  // flight
            spellInfo->Attributes |= SPELL_ATTR0_PASSIVE;

        if (spellInfo->IsSingleTarget() && !spellInfo->MaxAffectedTargets)
            spellInfo->MaxAffectedTargets = 1;

        switch (spellInfo->SpellFamilyName)
        {
            case SPELLFAMILY_DEATHKNIGHT:
                // Icy Touch - extend FamilyFlags (unused value) for Sigil of the Frozen Conscience to use
                if (spellInfo->IconFileDataId == 237526 && spellInfo->SpellFamilyFlags[0] & 0x2)
                    spellInfo->SpellFamilyFlags[0] |= 0x40;
                break;
        }
    }

    if (SummonPropertiesEntry* properties = const_cast<SummonPropertiesEntry*>(sSummonPropertiesStore.LookupEntry(121)))
        properties->Title = AsUnderlyingType(SummonTitle::Totem);
    if (SummonPropertiesEntry* properties = const_cast<SummonPropertiesEntry*>(sSummonPropertiesStore.LookupEntry(647))) // 52893
        properties->Title = AsUnderlyingType(SummonTitle::Totem);
    if (SummonPropertiesEntry* properties = const_cast<SummonPropertiesEntry*>(sSummonPropertiesStore.LookupEntry(628))) // Hungry Plaguehound
        properties->Control = SUMMON_CATEGORY_PET;

    if (LockEntry* entry = const_cast<LockEntry*>(sLockStore.LookupEntry(36))) // 3366 Opening, allows to open without proper key
        entry->Type[2] = LOCK_KEY_NONE;


    TC_LOG_INFO("server.loading", ">> Loaded SpellInfo corrections in {} ms", GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellInfoSpellSpecificAndAuraState()
{
    uint32 oldMSTime = getMSTime();

    for (SpellInfo const& spellInfo : mSpellInfoMap)
    {
        // AuraState depends on SpellSpecific
        const_cast<SpellInfo&>(spellInfo)._LoadSpellSpecific();
        const_cast<SpellInfo&>(spellInfo)._LoadAuraState();
    }

    TC_LOG_INFO("server.loading", ">> Loaded SpellInfo SpellSpecific and AuraState in {} ms", GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellInfoDiminishing()
{
    uint32 oldMSTime = getMSTime();

    for (SpellInfo const& spellInfo : mSpellInfoMap)
        const_cast<SpellInfo&>(spellInfo)._LoadSpellDiminishInfo();

    TC_LOG_INFO("server.loading", ">> Loaded SpellInfo diminishing infos in {} ms", GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadSpellInfoImmunities()
{
    uint32 oldMSTime = getMSTime();

    for (SpellInfo const& spellInfo : mSpellInfoMap)
        const_cast<SpellInfo&>(spellInfo)._LoadImmunityInfo();

    TC_LOG_INFO("server.loading", ">> Loaded SpellInfo immunity infos in {} ms", GetMSTimeDiffToNow(oldMSTime));
}

void SpellMgr::LoadPetFamilySpellsStore()
{
    std::unordered_map<uint32, SpellLevelsEntry const*> levelsBySpell;
    for (SpellLevelsEntry const* levels : sSpellLevelsStore)
        if (!levels->DifficultyID)
            levelsBySpell[levels->SpellID] = levels;

    for (SkillLineAbilityEntry const* skillLine : sSkillLineAbilityStore)
    {
        SpellInfo const* spellInfo = GetSpellInfo(skillLine->Spell, DIFFICULTY_NONE);
        if (!spellInfo)
            continue;

        auto levels = levelsBySpell.find(skillLine->Spell);
        if (levels != levelsBySpell.end() && levels->second->SpellLevel)
            continue;

        if (spellInfo->IsPassive())
        {
            for (CreatureFamilyEntry const* cFamily : sCreatureFamilyStore)
            {
                if (skillLine->SkillLine != cFamily->SkillLine[0] && skillLine->SkillLine != cFamily->SkillLine[1])
                    continue;

                if (skillLine->AcquireMethod != SKILL_LINE_ABILITY_LEARNED_ON_SKILL_LEARN)
                    continue;

                sPetFamilySpellsStore[cFamily->ID].insert(spellInfo->Id);
            }
        }
    }
}
