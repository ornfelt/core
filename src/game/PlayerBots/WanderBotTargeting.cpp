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
#include "Player.h"
#include "Creature.h"
#include "Pet.h"
#include "World.h"
#include "GridNotifiers.h"
#include "GridNotifiersImpl.h"
#include "CellImpl.h"

#include <list>

namespace
{
    class WanderBotGuardCheck
    {
        public:
            WanderBotGuardCheck(WorldObject const* pObject, float range) : m_pObject(pObject), m_range(range) {}
            WorldObject const& GetFocusObject() const { return *m_pObject; }
            bool operator()(Creature* pCreature) const
            {
                return pCreature->IsGuard() && pCreature->IsAlive() && m_pObject->IsWithinDistInMap(pCreature, m_range);
            }
        private:
            WorldObject const* m_pObject;
            float m_range;
    };
}

bool WanderBotAI::IsGuardNear(Unit const* pTarget) const
{
    Creature* pGuard = nullptr;
    WanderBotGuardCheck check(pTarget, WB_GUARD_CHECK_RANGE);
    MaNGOS::CreatureSearcher<WanderBotGuardCheck> searcher(pGuard, check);
    Cell::VisitGridObjects(pTarget, searcher, WB_GUARD_CHECK_RANGE);
    return pGuard != nullptr;
}

bool WanderBotAI::StartAttack(Unit* pVictim)
{
    if (!AttackStart(pVictim))
        return false;

    // The leash is measured from where the fight started.
    m_leashX = me->GetPositionX();
    m_leashY = me->GetPositionY();
    m_leashZ = me->GetPositionZ();
    return true;
}

bool WanderBotAI::CheckLeash()
{
    Unit* pVictim = me->GetVictim();
    if (!pVictim || pVictim->GetVictim() == me)
        return false;

    if (me->GetDistance(m_leashX, m_leashY, m_leashZ) <= WB_LEASH_DISTANCE)
        return false;

    // Too far from the route chasing something that does not fight back: give up and go back to wandering.
    me->AttackStop(false);
    me->ClearTarget();
    StopMoving();
    return true;
}

bool WanderBotAI::IsValidNewTarget(Unit const* pTarget) const
{
    if (!IsValidHostileTarget(pTarget))
        return false;

    uint32 const level = me->GetLevel();
    uint32 const targetLevel = pTarget->GetLevel();

    // Players, their pets and totems: only on PvP realms. On normal realms the bot only fights back.
    if (Player const* pOwner = pTarget->GetCharmerOrOwnerPlayerOrPlayerItself())
    {
        if (!sWorld.IsPvPRealm() || pOwner->GetTeam() == me->GetTeam())
            return false;

        if (targetLevel > level + WB_MAX_PLAYER_LEVEL_ABOVE ||
            targetLevel + WB_MAX_PLAYER_LEVEL_BELOW < level)
            return false;

        return !IsGuardNear(pTarget);
    }

    Creature const* pCreature = pTarget->ToCreature();
    if (!pCreature)
        return false;

    // Hostile monsters only: no critters, civilians, guards or bosses.
    if (!me->IsHostileTo(pCreature) ||
        pCreature->GetCreatureType() == CREATURE_TYPE_CRITTER ||
        pCreature->IsCivilian() ||
        pCreature->IsGuard() ||
        pCreature->IsWorldBoss())
        return false;

    // Do not steal monsters someone else is fighting.
    if (Unit* pCreatureVictim = pCreature->GetVictim())
    {
        if (pCreatureVictim != me && pCreatureVictim != me->GetPet())
            return false;
    }

    if (pCreature->IsElite())
        return targetLevel + WB_MIN_ELITE_LEVEL_LEAD <= level &&
               targetLevel + WB_MAX_ELITE_LEVEL_BELOW >= level;

    return targetLevel <= level + WB_MAX_LEVEL_ABOVE &&
           targetLevel + WB_MAX_LEVEL_BELOW >= level;
}

Unit* WanderBotAI::SelectAttackTarget(Unit* pExcept) const
{
    // 1. The nearest unit we are in combat with.
    Unit* pTarget = nullptr;
    float targetDistance = VISIBILITY_DISTANCE_NORMAL;
    for (HostileReference* pReference = me->GetHostileRefManager().getFirst(); pReference; pReference = pReference->next())
    {
        Unit* pAttacker = pReference->getSourceUnit();
        if (!pAttacker || pAttacker == pExcept || !IsValidHostileTarget(pAttacker))
            continue;

        float const distance = me->GetDistance(pAttacker);
        if (distance < targetDistance)
        {
            pTarget = pAttacker;
            targetDistance = distance;
        }
    }

    if (pTarget)
        return pTarget;

    // 2. Whatever attacks our pet.
    if (Pet* pPet = me->GetPet())
    {
        if (Unit* pAttacker = pPet->GetAttackerForHelper())
        {
            if (pAttacker != pExcept && IsValidHostileTarget(pAttacker))
                return pAttacker;
        }
    }

    // 3. A new target nearby, only when rested.
    if (NeedsRest())
        return nullptr;

    std::list<Unit*> enemies;
    me->GetEnemyListInRadiusAround(me, WB_AGGRO_RANGE, enemies);
    targetDistance = WB_AGGRO_RANGE;
    for (Unit* pEnemy : enemies)
    {
        if (pEnemy == pExcept || !IsValidNewTarget(pEnemy))
            continue;

        float const distance = me->GetDistance(pEnemy);
        if (distance >= targetDistance)
            continue;

        if (me->GetDistanceZ(pEnemy) > WB_MAX_TARGET_Z_DIFF)
            continue;

        if (!me->IsWithinLOSInMap(pEnemy))
            continue;

        pTarget = pEnemy;
        targetDistance = distance;
    }

    return pTarget;
}
