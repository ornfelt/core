/*
* This program is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation; either version 2 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include "WanderBotAI.h"
#include "WanderNodeMgr.h"
#include "PlayerBotMgr.h"
#include "Player.h"
#include "Creature.h"
#include "Pet.h"
#include "Map.h"
#include "Log.h"
#include "MotionMaster.h"
#include "ObjectMgr.h"
#include "QuestDef.h"
#include "SpellMgr.h"
#include "World.h"
#include "Utilities/Random.h"

WanderBotAI::WanderBotAI(uint8 race, uint8 class_, uint8 level, uint32 mapId, uint32 instanceId, WanderNode const* spawnNode)
    : BattleBotAI(race, class_, level, mapId, instanceId, spawnNode->x, spawnNode->y, spawnNode->z, spawnNode->o, 0, true),
      m_node(spawnNode), m_arrived(true)
{
}

uint32 WanderBotAI::GetRandomGearLevelDifference() const
{
    return sWorld.getConfig(CONFIG_UINT32_WANDER_BOT_RANDOM_GEAR_LEVEL_DIFFERENCE);
}

void WanderBotAI::LearnTalentsAndSpells()
{
    // A premade spec made for exactly this level, else random talents and all trainer spells.
    for (auto const& itr : sObjectMgr.GetPlayerPremadeSpecTemplates())
    {
        if (itr.second.requiredClass == me->GetClass() &&
            itr.second.level == me->GetLevel())
        {
            LearnPremadeSpecForClass();
            LearnClassQuestSpells();
            return;
        }
    }

    LearnRandomTalentsAndAllSpells();
    LearnClassQuestSpells();
}

void WanderBotAI::LearnClassQuestSpells()
{
    // Spells players get from class quests (warlock demons, druid forms, ...), which trainers do not teach.
    for (auto const& itr : sObjectMgr.GetQuestTemplates())
    {
        Quest const* pQuest = itr.second.get();
        if (!pQuest->IsActive() || !(pQuest->GetRequiredClasses() & me->GetClassMask()) ||
            (pQuest->GetRequiredRaces() && !(pQuest->GetRequiredRaces() & me->GetRaceMask())) ||
            pQuest->GetMinLevel() > me->GetLevel())
            continue;

        for (uint32 rewardSpellId : { pQuest->GetRewSpellCast(), pQuest->GetRewSpell() })
        {
            SpellEntry const* pRewardSpell = rewardSpellId ? sSpellMgr.GetSpellEntry(rewardSpellId) : nullptr;
            if (!pRewardSpell)
                continue;

            for (uint32 i = 0; i < MAX_EFFECT_INDEX; ++i)
            {
                uint32 const spellId = pRewardSpell->EffectTriggerSpell[i];
                if (pRewardSpell->Effect[i] == SPELL_EFFECT_LEARN_SPELL && spellId && !me->HasSpell(spellId))
                    me->LearnSpell(spellId, false);
            }
        }
    }
}

void WanderBotAI::Initialize()
{
    if (m_level && m_level != me->GetLevel())
    {
        me->GiveLevel(m_level);
        me->InitTalentForLevel();
        me->SetUInt32Value(PLAYER_XP, 0);
    }

    LearnTalentsAndSpells();

    if (m_role == ROLE_INVALID)
        AutoAssignRole();

    AutoEquipGear(sWorld.getConfig(CONFIG_UINT32_WANDER_BOT_AUTO_EQUIP));
    ResetSpellData();
    PopulateSpellData();
    AddAllSpellReagents();
    me->UpdateSkillsToMaxSkillsForLevel();
    // Wandering bots never level up.
    me->SetPersonalXpRate(0.0f);
    me->RemoveFlag(UNIT_FIELD_FLAGS, UNIT_FLAG_SPAWNING);
    SummonPetIfNeeded();
    me->SetHealthPercent(100.0f);
    me->SetPowerPercent(me->GetPowerType(), 100.0f);

    if (urand(0, 1))
    {
        me->ToggleFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_HELM);
        me->ToggleFlag(PLAYER_FLAGS, PLAYER_FLAGS_HIDE_CLOAK);
    }

    uint32 newzone, newarea;
    me->GetZoneAndAreaId(newzone, newarea);
    me->UpdateZone(newzone, newarea);

    m_initialized = true;

    if (sPlayerBotMgr.IsDebugEnabled())
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[WanderBot] %s spawned: level %u, race %u, class %u, %s, map %u, zone %u, node %u '%s' (levels %u-%u)",
            me->GetName(), me->GetLevel(), me->GetRace(), me->GetClass(), me->GetTeam() == ALLIANCE ? "Alliance" : "Horde",
            me->GetMapId(), newzone, m_node->id, m_node->name.c_str(), m_node->minLevel, m_node->maxLevel);
}

void WanderBotAI::UpdateDeath()
{
    if (!m_wasDead)
    {
        m_wasDead = true;
        OnJustDied();
        m_deathTimer = WB_RELEASE_DELAY;
        if (sPlayerBotMgr.IsDebugEnabled())
            sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[WanderBot] %s (level %u, class %u) died at map %u (%g, %g), zone %u, fighting %s, %u attackers.",
                me->GetName(), me->GetLevel(), me->GetClass(), me->GetMapId(), me->GetPositionX(), me->GetPositionY(), me->GetZoneId(),
                DescribeLastEnemy().c_str(), m_lastAttackerCount);
        m_lastEnemyGuid.Clear();
        m_lastAttackerCount = 0;
        return;
    }

    if (m_deathTimer > WB_UPDATE_INTERVAL)
    {
        m_deathTimer -= WB_UPDATE_INTERVAL;
        return;
    }

    // Release the spirit, then come back to life at the graveyard.
    if (me->GetDeathState() == CORPSE)
    {
        me->BuildPlayerRepop();
        me->RepopAtGraveyard();
        m_deathTimer = WB_REVIVE_DELAY;
    }
    else if (me->GetDeathState() == DEAD)
    {
        me->ResurrectPlayer(WB_REVIVE_HEALTH);
        me->SpawnCorpseBones();
    }
}

void WanderBotAI::OnRevived()
{
    SummonPetIfNeeded();

    // The graveyard can be far from the route: go on from the nearest fitting node.
    m_node = nullptr;
    m_previousNode = nullptr;
    m_arrived = false;

    // A graveyard next to an enemy town or strong monsters: the bot would be killed again before it got away.
    if (IsInDanger())
        TeleportToSpawnNode();
}

void WanderBotAI::RememberEnemy()
{
    if (!me->IsInCombat())
    {
        m_lastEnemyGuid.Clear();
        m_lastAttackerCount = 0;
        return;
    }

    Unit* pEnemy = me->GetVictim();
    if (!pEnemy && !me->GetAttackers().empty())
        pEnemy = *me->GetAttackers().begin();
    if (pEnemy)
        m_lastEnemyGuid = pEnemy->GetObjectGuid();
    m_lastAttackerCount = me->GetAttackers().size();
}

std::string WanderBotAI::DescribeLastEnemy() const
{
    if (m_lastEnemyGuid.IsEmpty())
        return "nothing";

    Unit* pEnemy = me->GetMap()->GetUnit(m_lastEnemyGuid);
    if (!pEnemy)
        return m_lastEnemyGuid.GetString();

    std::string description = pEnemy->GetName();
    description += " (level " + std::to_string(pEnemy->GetLevel());
    if (Player* pPlayer = pEnemy->ToPlayer())
        description += dynamic_cast<WanderBotAI*>(pPlayer->AI()) ? ", wandering bot" : ", player";
    else if (Creature const* pCreature = pEnemy->ToCreature())
        description += ", entry " + std::to_string(pCreature->GetEntry()) + (pCreature->IsElite() ? ", elite" : "");
    return description + ")";
}

bool WanderBotAI::NeedsRest() const
{
    return me->GetHealthPercent() < WB_REST_HEALTH ||
          (me->GetPowerType() == POWER_MANA && me->GetPowerPercent(POWER_MANA) < WB_REST_MANA);
}

void WanderBotAI::UpdateAI(uint32 const diff)
{
    m_updateTimer.Update(diff);
    if (m_updateTimer.Passed())
        m_updateTimer.Reset(WB_UPDATE_INTERVAL);
    else
        return;

    if (!me->IsInWorld() || me->IsBeingTeleported())
        return;

    if (!m_initialized)
    {
        Initialize();
        return;
    }

    if (me->IsDead())
    {
        UpdateDeath();
        return;
    }

    if (m_wasDead)
    {
        m_wasDead = false;
        OnRevived();
        return;
    }

    RememberEnemy();

    if (me->HasUnitState(UNIT_STATE_CAN_NOT_REACT_OR_LOST_CONTROL))
    {
        BreakCrowdControlEffects();
        return;
    }

    if (me->GetCurrentSpell(CURRENT_AUTOREPEAT_SPELL))
    {
        // Stop auto shot if no target.
        if (!me->GetVictim())
            me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
        else if (me->GetClass() == CLASS_HUNTER)
        {
            if (me->GetCombatDistance(me->GetVictim()) < 8.0f)
                me->InterruptSpell(CURRENT_AUTOREPEAT_SPELL, true);
            else
                UpdateInCombatAI_Hunter();
        }

        return;
    }

    if (me->IsNonMeleeSpellCasted(false, false, true))
        return;

    if (me->GetTargetGuid() == me->GetObjectGuid())
        me->ClearTarget();

    if (CheckLeash())
        return;

    Unit* pVictim = me->GetVictim();

    // Stop chasing a target that entered stealth.
    if (pVictim && !pVictim->IsVisibleForOrDetect(me, me, false))
    {
        me->AttackStop();
        me->ClearTarget();
        me->StopMoving();
        if (pVictim = SelectAttackTarget(pVictim))
            StartAttack(pVictim);
        return;
    }

    // Rest when low, and once resting until full.
    if (!me->IsInCombat() && (NeedsRest() || me->GetStandState() == UNIT_STAND_STATE_SIT))
    {
        if (DrinkAndEat())
            return;
    }

    if (me->GetStandState() != UNIT_STAND_STATE_STAND)
        me->SetStandState(UNIT_STAND_STATE_STAND);

    if (me->GetSheath() == SHEATH_STATE_UNARMED && !me->IsMounted())
        me->SetSheath(SHEATH_STATE_MELEE);

    if (!me->IsInCombat())
    {
        if (CheckForUnreachableTarget())
            return;

        // BattleBotAI calls a hunter's pet only at spawn and revive: bring back one lost in a fight.
        if (me->GetClass() == CLASS_HUNTER && (!me->GetPet() || !me->GetPet()->IsAlive()))
            SummonPetIfNeeded();

        UpdateOutOfCombatAI();

        if (m_isBuffing)
            return;

        // Can enter combat from UpdateOutOfCombatAI().
        if (me->IsInCombat())
            return;

        if (me->IsNonMeleeSpellCasted())
            return;

        if (!pVictim || !IsValidHostileTarget(pVictim))
        {
            if (pVictim = SelectAttackTarget(pVictim))
            {
                StartAttack(pVictim);
                return;
            }

            UpdateWanderMovement();
        }
        return;
    }

    if (!pVictim || !IsValidHostileTarget(pVictim) ||
        !pVictim->IsWithinDist(me, VISIBILITY_DISTANCE_SMALL))
    {
        if (Unit* pNewVictim = SelectAttackTarget(pVictim))
        {
            if (pVictim != pNewVictim)
            {
                StartAttack(pNewVictim);
                return;
            }
        }

        if (me->GetVictim() &&
           (me != me->GetVictim()->GetVictim()))
        {
            me->AttackStop(false);
            if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == CHASE_MOTION_TYPE)
                StopMoving();
            return;
        }
    }
    else
    {
        if (!me->HasInArc(pVictim, 2 * M_PI_F / 3) && !me->IsMoving())
        {
            me->SetInFront(pVictim);
            me->SendMovementPacket(MSG_MOVE_SET_FACING, false);
        }

        if (!me->HasUnitState(UNIT_STATE_MELEE_ATTACKING) &&
           (m_role != ROLE_HEALER) &&
            IsValidHostileTarget(pVictim) &&
            AttackStart(pVictim))
            return;
    }

    if (me->IsInCombat())
        UpdateInCombatAI();
}
