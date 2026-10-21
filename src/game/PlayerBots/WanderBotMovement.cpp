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
#include "Log.h"
#include "MotionMaster.h"
#include "Utilities/Random.h"

void WanderBotAI::ResetMovementChecks()
{
    m_stuckCheckTimer = WB_STUCK_CHECK_INTERVAL;
    m_stuckChecks = 0;
    m_lastCheckX = me->GetPositionX();
    m_lastCheckY = me->GetPositionY();
}

bool WanderBotAI::CheckStuck()
{
    if (m_stuckCheckTimer > WB_UPDATE_INTERVAL)
    {
        m_stuckCheckTimer -= WB_UPDATE_INTERVAL;
        return false;
    }
    m_stuckCheckTimer = WB_STUCK_CHECK_INTERVAL;

    float const dx = me->GetPositionX() - m_lastCheckX;
    float const dy = me->GetPositionY() - m_lastCheckY;
    m_lastCheckX = me->GetPositionX();
    m_lastCheckY = me->GetPositionY();

    if (dx * dx + dy * dy >= WB_STUCK_MIN_PROGRESS * WB_STUCK_MIN_PROGRESS)
    {
        m_stuckChecks = 0;
        return false;
    }

    return ++m_stuckChecks >= WB_MAX_STUCK_CHECKS;
}

bool WanderBotAI::SelectNextNode()
{
    Team const team = me->GetTeam();
    uint32 const level = me->GetLevel();

    WanderNode const* pNext = sWanderNodeMgr.SelectNextNode(m_node, m_previousNode, team, level);

    // No fitting link (a dead end, or the route leaves the bot's level range): the nearest fitting node.
    if (!pNext)
        pNext = sWanderNodeMgr.FindNearestNode(m_node->mapId, m_node->x, m_node->y, team, level, WB_MAX_WALK_DISTANCE, m_node);

    if (!pNext)
        return false;

    m_previousNode = m_node;
    m_node = pNext;
    m_arrived = false;
    ResetMovementChecks();
    return true;
}

void WanderBotAI::TeleportToSpawnNode()
{
    WanderNode const* pNode = sWanderNodeMgr.SelectSpawnNode(me->GetTeam(), me->GetLevel());
    if (!pNode)
    {
        sLog.Out(LOG_BASIC, LOG_LVL_ERROR, "[WanderBot] %s (level %u) has no spawn node to go to and is removed.", me->GetName(), me->GetLevel());
        botEntry->requestRemoval = true;
        return;
    }

    if (sPlayerBotMgr.IsDebugEnabled())
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[WanderBot] %s (level %u) teleported from map %u (%g, %g) to node %u '%s'.",
            me->GetName(), me->GetLevel(), me->GetMapId(), me->GetPositionX(), me->GetPositionY(), pNode->id, pNode->name.c_str());

    StopMoving();
    m_previousNode = nullptr;
    m_node = pNode;
    m_arrived = true;
    m_pauseTimer = 0;
    m_turnedBack = false;

    if (pNode->mapId == me->GetMapId())
        me->NearTeleportTo(pNode->x, pNode->y, pNode->z, pNode->o);
    else
        me->TeleportTo(pNode->mapId, pNode->x, pNode->y, pNode->z, pNode->o);
}

void WanderBotAI::MoveToCurrentNode()
{
    float x = m_node->x;
    float y = m_node->y;
    float z = m_node->z;
    if (m_node->proximity > 0.0f)
        me->GetRandomPoint(m_node->x, m_node->y, m_node->z, m_node->proximity, x, y, z);

    // Two linked nodes that both ignore pathing are joined by a straight move.
    uint32 options = MOVE_PATHFINDING | MOVE_EXCLUDE_STEEP_SLOPES;
    if (m_previousNode && m_previousNode->HasFlag(WANDER_NODE_FLAG_IGNORES_PATHING) &&
        m_node->HasFlag(WANDER_NODE_FLAG_IGNORES_PATHING))
        options = MOVE_NONE;

    me->GetMotionMaster()->MovePoint(WB_MOVE_POINT_ID, x, y, z, options);
}

void WanderBotAI::UpdateWanderMovement()
{
    if (me->HasUnitState(UNIT_STATE_CAN_NOT_MOVE))
        return;

    // No node (just revived): go on from the nearest one that fits.
    if (!m_node)
    {
        m_node = sWanderNodeMgr.FindNearestNode(me->GetMapId(), me->GetPositionX(), me->GetPositionY(), me->GetTeam(), me->GetLevel(), WB_MAX_WALK_DISTANCE);
        if (!m_node)
        {
            TeleportToSpawnNode();
            return;
        }
        m_arrived = false;
        m_turnedBack = false;
        ResetMovementChecks();
    }

    if (m_arrived)
    {
        if (m_pauseTimer > WB_UPDATE_INTERVAL)
        {
            m_pauseTimer -= WB_UPDATE_INTERVAL;
            return;
        }

        if (!SelectNextNode())
        {
            TeleportToSpawnNode();
            return;
        }
    }

    if (CheckStuck())
    {
        StopMoving();
        // Turn back to the previous node once; stuck again, skip the move with a teleport onto the node.
        if (m_previousNode && !m_turnedBack)
        {
            std::swap(m_node, m_previousNode);
            m_turnedBack = true;
            ResetMovementChecks();
        }
        else
        {
            if (sPlayerBotMgr.IsDebugEnabled())
                sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "[WanderBot] %s stuck at map %u (%g, %g, %g), teleported to node %u.",
                    me->GetName(), me->GetMapId(), me->GetPositionX(), me->GetPositionY(), me->GetPositionZ(), m_node->id);
            me->NearTeleportTo(m_node->x, m_node->y, m_node->z, m_node->o);
            OnNodeReached();
            return;
        }
    }

    // Already walking to the node.
    if (me->GetMotionMaster()->GetCurrentMovementGeneratorType() == POINT_MOTION_TYPE)
        return;

    // A new node, or the move was interrupted (fight, rest, mount): (re)start it, mounted for long ways.
    if (me->GetDistance2d(m_node->x, m_node->y) > WB_MOUNT_DISTANCE && UseMount())
        return;

    MoveToCurrentNode();
}

void WanderBotAI::MovementInform(uint32 movementType, uint32 data)
{
    if (movementType != POINT_MOTION_TYPE || data != WB_MOVE_POINT_ID || !m_node || m_arrived)
        return;

    // A move cut short (no full path) is not an arrival: the stuck check deals with it.
    if (me->GetDistance2d(m_node->x, m_node->y) > m_node->proximity + WB_ARRIVE_DISTANCE)
        return;

    OnNodeReached();
}

void WanderBotAI::OnNodeReached()
{
    m_arrived = true;
    m_turnedBack = false;
    m_pauseTimer = urand(m_node->minWaitTime, m_node->maxWaitTime);

    if (sPlayerBotMgr.IsDebugEnabled())
        sLog.Out(LOG_BASIC, LOG_LVL_BASIC, "[WanderBot] %s (level %u) reached node %u '%s' (levels %u-%u).",
            me->GetName(), me->GetLevel(), m_node->id, m_node->name.c_str(), m_node->minLevel, m_node->maxLevel);
}
