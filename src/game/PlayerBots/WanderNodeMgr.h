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

#ifndef MANGOS_WANDER_NODE_MGR_H
#define MANGOS_WANDER_NODE_MGR_H

#include "Common.h"
#include "SharedDefines.h"
#include "Policies/Singleton.h"

#include <string>
#include <unordered_map>
#include <vector>

// Flags of the wander_bot_nodes table. The bit values match AzerothCore-NPCBots' BotWPFlags, the source of the data.
enum WanderNodeFlags
{
    WANDER_NODE_FLAG_NONE               = 0x00000,
    WANDER_NODE_FLAG_SPAWN              = 0x00001, // bots can spawn / be teleported here
    WANDER_NODE_FLAG_ALLIANCE_ONLY      = 0x00002, // Horde bots never move here, with SPAWN an Alliance spawn point
    WANDER_NODE_FLAG_HORDE_ONLY         = 0x00004, // Alliance bots never move here, with SPAWN a Horde spawn point
    WANDER_NODE_FLAG_CAN_BACKTRACK_FROM = 0x00008, // the previous node may be picked even if other links exist
    WANDER_NODE_FLAG_IGNORES_FACTION    = 0x00010, // faction flags are ignored when walking (not spawning) here
    WANDER_NODE_FLAG_IGNORES_PATHING    = 0x00020, // no path is generated between two nodes with this flag
    WANDER_NODE_FLAG_JUMP_BEGIN         = 0x10000, // the move from this node to a JUMP_END node is a forced jump
    WANDER_NODE_FLAG_JUMP_END           = 0x20000,
    WANDER_NODE_FLAG_MILL_RADIUS        = 0x40000, // the mill radius at the node is reduced to interaction distance
    WANDER_NODE_FLAG_NOT_A_START_POINT  = 0x80000, // never a spawn / teleport target, only walked through

    WANDER_NODE_FLAG_FACTION_ONLY       = WANDER_NODE_FLAG_ALLIANCE_ONLY | WANDER_NODE_FLAG_HORDE_ONLY,
    WANDER_NODE_FLAGS_ALL               = WANDER_NODE_FLAG_SPAWN | WANDER_NODE_FLAG_FACTION_ONLY |
                                          WANDER_NODE_FLAG_CAN_BACKTRACK_FROM | WANDER_NODE_FLAG_IGNORES_FACTION |
                                          WANDER_NODE_FLAG_IGNORES_PATHING | WANDER_NODE_FLAG_JUMP_BEGIN |
                                          WANDER_NODE_FLAG_JUMP_END | WANDER_NODE_FLAG_MILL_RADIUS |
                                          WANDER_NODE_FLAG_NOT_A_START_POINT,
};

// A node fits a bot of level L when L + margin >= min level and L <= max level (as NPCBots).
#define WANDER_NODE_LEVEL_MARGIN 2
// Spawning / teleporting on start zone nodes (max level up to this) needs the bot's own faction flag.
#define WANDER_NODE_START_ZONE_MAX_LEVEL 10

struct WanderNode
{
    uint32 id = 0;
    std::string name;
    uint32 mapId = 0;
    uint32 zoneId = 0;
    uint32 areaId = 0;
    uint8 minLevel = 0;
    uint8 maxLevel = 0;
    uint32 minWaitTime = 0;
    uint32 maxWaitTime = 0;
    float proximity = 0.0f;
    uint32 flags = 0;
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float o = 0.0f;
    std::vector<WanderNode const*> links;

    bool HasFlag(uint32 flag) const { return (flags & flag) != 0; }
    bool FitsLevel(uint32 level) const { return level + WANDER_NODE_LEVEL_MARGIN >= minLevel && level <= maxLevel; }
    // A bot of this team may walk here.
    bool IsWalkableFor(Team team) const;
    // A bot of this team may spawn or be teleported here.
    bool IsStartPointFor(Team team) const;
    // A bot of this team and level can walk on from here.
    bool HasLinkFor(Team team, uint32 level) const;
    float GetDistance2dSq(float px, float py) const { return (x - px) * (x - px) + (y - py) * (y - py); }
};

class WanderNodeMgr
{
    public:
        void Load();

        bool IsLoaded() const { return !m_nodes.empty(); }
        uint32 GetNodeCount() const { return m_nodes.size(); }
        uint32 GetSpawnNodeCount() const { return m_spawnNodes.size(); }
        WanderNode const* GetNode(uint32 id) const;

        // A random spawn node that fits the team and level and has a link that does, or nullptr.
        WanderNode const* SelectSpawnNode(Team team, uint32 level) const;
        // A random link of the current node that fits the team and level. The previous node is only picked when
        // it is the only fitting link or the current node allows backtracking. nullptr when no link fits.
        WanderNode const* SelectNextNode(WanderNode const* current, WanderNode const* previous, Team team, uint32 level) const;
        // The nearest node on the map, within maxDist, that the team may walk to and that fits the level, other than
        // pExcept, or nullptr.
        WanderNode const* FindNearestNode(uint32 mapId, float x, float y, Team team, uint32 level, float maxDist, WanderNode const* pExcept = nullptr) const;
        // All nodes on the map within range (for GM commands).
        void GetNodesInRange(uint32 mapId, float x, float y, float range, std::vector<WanderNode const*>& nodes) const;

    private:
        std::unordered_map<uint32, WanderNode> m_nodes;
        std::vector<WanderNode const*> m_spawnNodes;
};

#define sWanderNodeMgr MaNGOS::Singleton<WanderNodeMgr>::Instance()

#endif
