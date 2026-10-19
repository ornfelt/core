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

#include "WanderNodeMgr.h"
#include "Policies/SingletonImp.h"
#include "Database/DatabaseEnv.h"
#include "Database/DBCEnums.h"
#include "GridMap.h"
#include "Log.h"
#include "Map.h"
#include "ObjectMgr.h"
#include "Util.h"
#include "World.h"
#include "Utilities/Random.h"

#include <algorithm>
#include <map>

INSTANTIATE_SINGLETON_1(WanderNodeMgr);

// A node more than this far above the ground / water level has no floor (vmaps are not loaded yet at startup, so
// nodes below the terrain, in caves or Undercity, are fine and only this direction is checked).
#define WANDER_NODE_MAX_HEIGHT_ABOVE_GROUND 50.0f
// Size of the level bands in the load summary.
#define WANDER_NODE_SUMMARY_LEVEL_BAND 10

bool WanderNode::IsWalkableFor(Team team) const
{
    if (HasFlag(WANDER_NODE_FLAG_IGNORES_FACTION))
        return true;

    switch (team)
    {
        case ALLIANCE:
            return !HasFlag(WANDER_NODE_FLAG_HORDE_ONLY);
        case HORDE:
            return !HasFlag(WANDER_NODE_FLAG_ALLIANCE_ONLY);
        default:
            return !HasFlag(WANDER_NODE_FLAG_FACTION_ONLY);
    }
}

bool WanderNode::IsStartPointFor(Team team) const
{
    if (!HasFlag(WANDER_NODE_FLAG_SPAWN) || HasFlag(WANDER_NODE_FLAG_NOT_A_START_POINT))
        return false;

    // Start zones are never shared: the node must belong to the bot's faction.
    if (maxLevel <= WANDER_NODE_START_ZONE_MAX_LEVEL)
        return HasFlag(team == ALLIANCE ? WANDER_NODE_FLAG_ALLIANCE_ONLY : WANDER_NODE_FLAG_HORDE_ONLY);

    switch (team)
    {
        case ALLIANCE:
            return !HasFlag(WANDER_NODE_FLAG_HORDE_ONLY);
        case HORDE:
            return !HasFlag(WANDER_NODE_FLAG_ALLIANCE_ONLY);
        default:
            return !HasFlag(WANDER_NODE_FLAG_FACTION_ONLY);
    }
}

struct WanderNodeLoadStats
{
    uint32 badMap = 0;
    uint32 noTerrain = 0;
    uint32 aboveGround = 0;
    uint32 flagsFixed = 0;
    uint32 levelsFixed = 0;
    uint32 areasRemapped = 0;
    uint32 capitalFlags = 0;
    uint32 linksDropped = 0;
    uint32 noLinks = 0;
};

void WanderNodeMgr::Load()
{
    m_nodes.clear();
    m_spawnNodes.clear();

    if (!sWorld.getConfig(CONFIG_BOOL_WANDER_BOT_ENABLE))
    {
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Wander bots are disabled (WanderBot.Enable = 0), wander nodes not loaded.");
        return;
    }

    std::unique_ptr<QueryResult> result(WorldDatabase.Query(
        "SELECT `id`, `name`, `map_id`, `zone_id`, `area_id`, `min_level`, `max_level`, `min_wait_time`, `max_wait_time`, "
        "`proximity`, `flags`, `position_x`, `position_y`, `position_z`, `orientation`, `links` FROM `wander_bot_nodes`"));
    if (!result)
    {
        sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `wander_bot_nodes` is empty or missing. Wander bots will not spawn.");
        return;
    }

    uint32 const maxLevel = sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL);
    WanderNodeLoadStats stats;
    std::unordered_map<uint32, std::string> linkStrings;

    do
    {
        Field* fields = result->Fetch();
        WanderNode node;
        node.id = fields[0].GetUInt32();
        node.name = fields[1].GetCppString();
        node.mapId = fields[2].GetUInt32();
        node.zoneId = fields[3].GetUInt32();
        node.areaId = fields[4].GetUInt32();
        uint32 minLevel = fields[5].GetUInt32();
        uint32 nodeMaxLevel = fields[6].GetUInt32();
        node.minWaitTime = fields[7].GetUInt32();
        node.maxWaitTime = fields[8].GetUInt32();
        node.proximity = fields[9].GetFloat();
        node.flags = fields[10].GetUInt32();
        node.x = fields[11].GetFloat();
        node.y = fields[12].GetFloat();
        node.z = fields[13].GetFloat();
        node.o = fields[14].GetFloat();

        MapEntry const* mapEntry = sMapStorage.LookupEntry<MapEntry>(node.mapId);
        if (!mapEntry || !mapEntry->IsContinent())
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `wander_bot_nodes`: node %u (%s) is on map %u, which is not a continent. Skipped.", node.id, node.name.c_str(), node.mapId);
            ++stats.badMap;
            continue;
        }

        TerrainInfo const* terrain = sTerrainMgr.LoadTerrain(node.mapId);
        float const groundZ = terrain->GetWaterOrGroundLevel(node.x, node.y, node.z);
        if (groundZ <= INVALID_HEIGHT)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `wander_bot_nodes`: node %u (%s) has no terrain at (%g, %g) on map %u. Skipped.", node.id, node.name.c_str(), node.x, node.y, node.mapId);
            ++stats.noTerrain;
            continue;
        }
        if (node.z - groundZ > WANDER_NODE_MAX_HEIGHT_ABOVE_GROUND)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `wander_bot_nodes`: node %u (%s) is %g yards above the ground. Skipped.", node.id, node.name.c_str(), node.z - groundZ);
            ++stats.aboveGround;
            continue;
        }

        if (node.flags & ~WANDER_NODE_FLAGS_ALL)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_DETAIL, "Table `wander_bot_nodes`: node %u (%s) has unknown flags 0x%X. Removed.", node.id, node.name.c_str(), node.flags & ~WANDER_NODE_FLAGS_ALL);
            node.flags &= WANDER_NODE_FLAGS_ALL;
            ++stats.flagsFixed;
        }
        if ((node.flags & WANDER_NODE_FLAG_FACTION_ONLY) == WANDER_NODE_FLAG_FACTION_ONLY)
        {
            sLog.Out(LOG_DBERROR, LOG_LVL_DETAIL, "Table `wander_bot_nodes`: node %u (%s) is both Alliance and Horde only. Both flags removed.", node.id, node.name.c_str());
            node.flags &= ~WANDER_NODE_FLAG_FACTION_ONLY;
            ++stats.flagsFixed;
        }

        if (!minLevel || minLevel > maxLevel || !nodeMaxLevel || nodeMaxLevel > maxLevel || minLevel > nodeMaxLevel)
        {
            minLevel = std::max<uint32>(1, std::min(minLevel, maxLevel));
            nodeMaxLevel = (nodeMaxLevel && nodeMaxLevel <= maxLevel) ? nodeMaxLevel : maxLevel;
            minLevel = std::min(minLevel, nodeMaxLevel);
            ++stats.levelsFixed;
        }
        node.minLevel = minLevel;
        node.maxLevel = nodeMaxLevel;
        if (node.minWaitTime > node.maxWaitTime)
            std::swap(node.minWaitTime, node.maxWaitTime);

        // Nodes made on later clients can use area ids 1.12 lacks: take the area from the terrain instead.
        AreaEntry const* area = AreaEntry::GetById(node.areaId);
        if (!area || area->MapId != node.mapId)
        {
            uint32 zoneId, areaId;
            sTerrainMgr.GetZoneAndAreaId(zoneId, areaId, node.mapId, node.x, node.y, node.z);
            sLog.Out(LOG_DBERROR, LOG_LVL_DETAIL, "Table `wander_bot_nodes`: node %u (%s) area %u unknown, using zone %u area %u from the terrain.", node.id, node.name.c_str(), node.areaId, zoneId, areaId);
            node.zoneId = zoneId;
            node.areaId = areaId;
            ++stats.areasRemapped;
        }

        // Never into the enemy faction's capitals.
        if (AreaEntry const* zone = AreaEntry::GetById(node.zoneId))
        {
            if (zone->Flags & AREA_FLAG_CAPITAL)
            {
                uint32 capitalFlag = WANDER_NODE_FLAG_NONE;
                if (zone->Team == AREATEAM_ALLY)
                    capitalFlag = WANDER_NODE_FLAG_ALLIANCE_ONLY;
                else if (zone->Team == AREATEAM_HORDE)
                    capitalFlag = WANDER_NODE_FLAG_HORDE_ONLY;

                if (capitalFlag && ((node.flags & (WANDER_NODE_FLAG_FACTION_ONLY | WANDER_NODE_FLAG_IGNORES_FACTION)) != capitalFlag))
                {
                    node.flags = (node.flags & ~(WANDER_NODE_FLAG_FACTION_ONLY | WANDER_NODE_FLAG_IGNORES_FACTION)) | capitalFlag;
                    ++stats.capitalFlags;
                }
            }
        }

        linkStrings[node.id] = fields[15].GetCppString();
        m_nodes[node.id] = std::move(node);
    }
    while (result->NextRow());

    // Links point into m_nodes, which is not changed after this point.
    for (auto& itr : m_nodes)
    {
        WanderNode& node = itr.second;
        for (auto const& token : StrSplit(linkStrings[node.id], " "))
        {
            if (token.empty())
                continue;

            uint32 const linkId = uint32(atoi(token.c_str()));
            auto const linked = m_nodes.find(linkId);
            if (linkId == node.id || linked == m_nodes.end() || linked->second.mapId != node.mapId ||
                std::find(node.links.begin(), node.links.end(), &linked->second) != node.links.end())
            {
                sLog.Out(LOG_DBERROR, LOG_LVL_DETAIL, "Table `wander_bot_nodes`: node %u (%s) has an invalid link to %u. Removed.", node.id, node.name.c_str(), linkId);
                ++stats.linksDropped;
                continue;
            }
            node.links.push_back(&linked->second);
        }

        if (node.links.empty())
            ++stats.noLinks;
        if (node.HasFlag(WANDER_NODE_FLAG_SPAWN))
            m_spawnNodes.push_back(&node);
    }

    // Summary: nodes per map and level band, and the levels a team has no spawn node for.
    std::map<uint32, uint32> nodesPerMap;
    std::map<uint32, uint32> nodesPerBand;
    for (auto const& itr : m_nodes)
    {
        ++nodesPerMap[itr.second.mapId];
        ++nodesPerBand[(itr.second.minLevel - 1) / WANDER_NODE_SUMMARY_LEVEL_BAND];
    }

    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, ">> Loaded %u wander nodes, %u spawn nodes.", uint32(m_nodes.size()), uint32(m_spawnNodes.size()));
    for (auto const& itr : nodesPerMap)
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "   map %u: %u nodes", itr.first, itr.second);
    for (auto const& itr : nodesPerBand)
    {
        uint32 const bandMin = itr.first * WANDER_NODE_SUMMARY_LEVEL_BAND + 1;
        sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "   min level %u-%u: %u nodes", bandMin, bandMin + WANDER_NODE_SUMMARY_LEVEL_BAND - 1, itr.second);
    }
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "   skipped: %u not on a continent, %u without terrain, %u above the ground", stats.badMap, stats.noTerrain, stats.aboveGround);
    sLog.Out(LOG_BASIC, LOG_LVL_MINIMAL, "   fixed: %u flags, %u levels, %u areas from the terrain, %u capital faction flags; %u links dropped, %u nodes without links",
        stats.flagsFixed, stats.levelsFixed, stats.areasRemapped, stats.capitalFlags, stats.linksDropped, stats.noLinks);

    for (Team team : { ALLIANCE, HORDE })
    {
        std::string missing;
        for (uint32 level = 1; level <= maxLevel; ++level)
        {
            if (!SelectSpawnNode(team, level))
                missing += " " + std::to_string(level);
        }
        if (!missing.empty())
            sLog.Out(LOG_DBERROR, LOG_LVL_ERROR, "Table `wander_bot_nodes`: no %s spawn node for levels:%s", team == ALLIANCE ? "Alliance" : "Horde", missing.c_str());
    }
}

WanderNode const* WanderNodeMgr::GetNode(uint32 id) const
{
    auto const itr = m_nodes.find(id);
    return itr != m_nodes.end() ? &itr->second : nullptr;
}

bool WanderNode::HasLinkFor(Team team, uint32 level) const
{
    return std::any_of(links.begin(), links.end(), [team, level](WanderNode const* link)
    {
        return link->IsWalkableFor(team) && link->FitsLevel(level);
    });
}

WanderNode const* WanderNodeMgr::SelectSpawnNode(Team team, uint32 level) const
{
    std::vector<WanderNode const*> candidates;
    for (WanderNode const* node : m_spawnNodes)
    {
        // A spawn node leading only to other level ranges (a 1-60 town gate into a start zone) would strand the bot.
        if (node->IsStartPointFor(team) && node->FitsLevel(level) && node->HasLinkFor(team, level))
            candidates.push_back(node);
    }

    if (candidates.empty())
        return nullptr;

    return SelectRandomContainerElement(candidates);
}

WanderNode const* WanderNodeMgr::SelectNextNode(WanderNode const* current, WanderNode const* previous, Team team, uint32 level) const
{
    std::vector<WanderNode const*> candidates;
    for (WanderNode const* link : current->links)
    {
        if (link->IsWalkableFor(team) && link->FitsLevel(level))
            candidates.push_back(link);
    }

    // Do not turn back unless it is the only way or the node allows it.
    if (candidates.size() > 1 && previous && !current->HasFlag(WANDER_NODE_FLAG_CAN_BACKTRACK_FROM))
        candidates.erase(std::remove(candidates.begin(), candidates.end(), previous), candidates.end());

    if (candidates.empty())
        return nullptr;

    return SelectRandomContainerElement(candidates);
}

WanderNode const* WanderNodeMgr::FindNearestNode(uint32 mapId, float x, float y, Team team, uint32 level, float maxDist, WanderNode const* pExcept) const
{
    WanderNode const* nearest = nullptr;
    float nearestDistSq = maxDist * maxDist;
    for (auto const& itr : m_nodes)
    {
        WanderNode const& node = itr.second;
        if (&node == pExcept || node.mapId != mapId || !node.IsWalkableFor(team) || !node.FitsLevel(level))
            continue;

        float const distSq = node.GetDistance2dSq(x, y);
        if (distSq < nearestDistSq)
        {
            nearest = &node;
            nearestDistSq = distSq;
        }
    }
    return nearest;
}

void WanderNodeMgr::GetNodesInRange(uint32 mapId, float x, float y, float range, std::vector<WanderNode const*>& nodes) const
{
    for (auto const& itr : m_nodes)
    {
        if (itr.second.mapId == mapId && itr.second.GetDistance2dSq(x, y) <= range * range)
            nodes.push_back(&itr.second);
    }
}
