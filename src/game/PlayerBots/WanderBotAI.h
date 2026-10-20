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

#ifndef MANGOS_WanderBotAI_H
#define MANGOS_WanderBotAI_H

#include "BattleBotAI.h"

struct WanderNode;

// How often the wandering bot AI runs (ms).
#define WB_UPDATE_INTERVAL 1000
// Point id of the moves between wander nodes, to tell them apart in MovementInform.
#define WB_MOVE_POINT_ID 1
// A move to a node ends this close to it (plus the node's proximity) to count as an arrival.
#define WB_ARRIVE_DISTANCE 5.0f
// Distance to the next node from which a bot that can ride mounts up first.
#define WB_MOUNT_DISTANCE 60.0f
// A bot with no fitting link walks to the nearest fitting node within this range, else it is teleported.
#define WB_MAX_WALK_DISTANCE 800.0f
// Stuck handling: less than WB_STUCK_MIN_PROGRESS yards moved in WB_STUCK_CHECK_INTERVAL ms is a stuck check,
// WB_MAX_STUCK_CHECKS in a row abandon the current move.
#define WB_STUCK_CHECK_INTERVAL 5000
#define WB_STUCK_MIN_PROGRESS 3.0f
#define WB_MAX_STUCK_CHECKS 3
// Death: the corpse is released after WB_RELEASE_DELAY ms, the ghost revived at the graveyard after WB_REVIVE_DELAY ms.
#define WB_RELEASE_DELAY 5000
#define WB_REVIVE_DELAY 10000
#define WB_REVIVE_HEALTH 0.5f
// Targets: the scan radius for new targets and the most a target may be above / below the bot.
#define WB_AGGRO_RANGE 25.0f
#define WB_MAX_TARGET_Z_DIFF 10.0f
#define WB_MAX_LEVEL_ABOVE 3
#define WB_MAX_LEVEL_BELOW 4
// Elites only when the bot is at least this many levels above them, and down to WB_MAX_ELITE_LEVEL_BELOW below.
#define WB_MIN_ELITE_LEVEL_LEAD 3
#define WB_MAX_ELITE_LEVEL_BELOW 8
#define WB_MAX_PLAYER_LEVEL_ABOVE 12
#define WB_MAX_PLAYER_LEVEL_BELOW 9
// Below this health / mana (percent) the bot rests (eats / drinks to full) and pulls no new target.
#define WB_REST_HEALTH 85.0f
#define WB_REST_MANA 60.0f
// A target is dropped once it is this far from where the bot engaged it, unless it is attacking the bot.
#define WB_LEASH_DISTANCE 60.0f
// No player is attacked with a guard this close to it (neutral towns: the guards would kill the bot).
#define WB_GUARD_CHECK_RANGE 50.0f
// A bot revived this close to a hostile guard, or to a hostile creature more than WB_MAX_LEVEL_ABOVE levels above it
// (a graveyard by an enemy town), is moved to a spawn node: it would be killed again before getting away.
#define WB_REVIVE_DANGER_RANGE 50.0f

// A temporary bot of random race, class, level, talents and gear that roams the world between wander nodes that
// fit its level. Derived from BattleBotAI for its class rotations, eating / drinking and mounts; the battleground
// parts of BattleBotAI stay inert because the bot never queues.
class WanderBotAI : public BattleBotAI
{
public:
    WanderBotAI(uint8 race, uint8 class_, uint8 level, uint32 mapId, uint32 instanceId, WanderNode const* spawnNode);

    void UpdateAI(uint32 const diff) override;
    void MovementInform(uint32 movementType, uint32 data = 0) override;

    WanderNode const* GetCurrentNode() const { return m_node; }

    uint32 GetRandomGearLevelDifference() const override;

private:
    void Initialize();
    void LearnTalentsAndSpells();
    void LearnClassQuestSpells();
    void UpdateDeath();
    void OnRevived();
    void RememberEnemy();
    std::string DescribeLastEnemy() const;
    bool NeedsRest() const;

    // Movement (WanderBotMovement.cpp)
    void UpdateWanderMovement();
    void MoveToCurrentNode();
    void OnNodeReached();
    bool SelectNextNode();
    void TeleportToSpawnNode();
    void ResetMovementChecks();
    bool CheckStuck();

    // Targets (WanderBotTargeting.cpp)
    Unit* SelectAttackTarget(Unit* pExcept = nullptr) const;
    bool IsValidNewTarget(Unit const* pTarget) const;
    bool IsGuardNear(Unit const* pTarget) const;
    bool IsInDanger() const;
    bool StartAttack(Unit* pVictim);
    bool CheckLeash();

    WanderNode const* m_node = nullptr;         // the node walked to, or paused at once m_arrived
    WanderNode const* m_previousNode = nullptr; // the node reached before m_node
    bool m_arrived = false;
    uint32 m_pauseTimer = 0;
    uint32 m_deathTimer = 0;
    // What the bot was fighting at its last update in combat, for the death log.
    ObjectGuid m_lastEnemyGuid;
    uint32 m_lastAttackerCount = 0;

    uint32 m_stuckCheckTimer = 0;
    uint32 m_stuckChecks = 0;
    bool m_turnedBack = false; // the current move already turned back once after being stuck
    float m_lastCheckX = 0.0f;
    float m_lastCheckY = 0.0f;

    float m_leashX = 0.0f;
    float m_leashY = 0.0f;
    float m_leashZ = 0.0f;
};

#endif
