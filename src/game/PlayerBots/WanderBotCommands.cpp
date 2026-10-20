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

#include "Chat.h"
#include "Database/DBCStores.h"
#include "Language.h"
#include "Map.h"
#include "Player.h"
#include "PlayerBotMgr.h"
#include "Util.h"
#include "WanderBotAI.h"
#include "WanderNodeMgr.h"
#include "World.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

// .wanderbot add adds at most this many bots at once.
#define WANDER_BOT_COMMAND_MAX_ADD 50
// .wanderbot nodes: the default and the largest search radius.
#define WANDER_BOT_COMMAND_NODES_RANGE 50.0f
#define WANDER_BOT_COMMAND_NODES_MAX_RANGE 500.0f
// .wanderbot list prints at most this many bots.
#define WANDER_BOT_COMMAND_MAX_LIST 50

namespace
{
    // A .wanderbot list filter: a level, a level range, or a text in the bot's name, race, class, faction or zone.
    struct WanderBotListFilter
    {
        uint32 minLevel = 0;
        uint32 maxLevel = 0;
        std::string text; // lower case

        explicit WanderBotListFilter(char const* args)
        {
            std::string filter = args ? args : "";
            filter.erase(0, filter.find_first_not_of(' '));
            filter.erase(filter.find_last_not_of(' ') + 1);

            char rest = 0;
            if (std::sscanf(filter.c_str(), "%u-%u%c", &minLevel, &maxLevel, &rest) == 2)
                return;
            if (std::sscanf(filter.c_str(), "%u%c", &minLevel, &rest) == 1)
            {
                maxLevel = minLevel;
                return;
            }

            minLevel = maxLevel = 0;
            text = filter;
            strToLower(text);
        }

        static bool Contains(std::string value, std::string const& lowerText)
        {
            strToLower(value);
            return value.find(lowerText) != std::string::npos;
        }

        bool Matches(Player const* pBot, char const* race, char const* class_, char const* faction, char const* zone) const
        {
            if (maxLevel)
                return pBot->GetLevel() >= minLevel && pBot->GetLevel() <= maxLevel;

            return text.empty() || Contains(pBot->GetName(), text) || Contains(race, text) ||
                   Contains(class_, text) || Contains(faction, text) || Contains(zone, text);
        }
    };
}

bool ChatHandler::HandleWanderBotAddCommand(char* args)
{
    if (!sWanderNodeMgr.IsLoaded())
    {
        SendSysMessage("No wander nodes are loaded (WanderBot.Enable = 0?).");
        SetSentErrorMessage(true);
        return false;
    }

    uint32 level = 0;
    uint32 count = 1;
    ExtractOptUInt32(&args, level, 0);
    ExtractOptUInt32(&args, count, 1);

    if (level > sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL) || !count || count > WANDER_BOT_COMMAND_MAX_ADD)
    {
        PSendSysMessage("Expected a level between 0 (random) and %u, and a count between 1 and %u.",
            sWorld.getConfig(CONFIG_UINT32_MAX_PLAYER_LEVEL), WANDER_BOT_COMMAND_MAX_ADD);
        SetSentErrorMessage(true);
        return false;
    }

    uint32 const added = sPlayerBotMgr.AddWanderBots(level, count);
    PSendSysMessage("%u wandering bots added.", added);
    return true;
}

bool ChatHandler::HandleWanderBotRemoveCommand(char* args)
{
    Player* pTarget = GetSelectedPlayer();
    if (!pTarget)
    {
        SendSysMessage(LANG_NO_CHAR_SELECTED);
        SetSentErrorMessage(true);
        return false;
    }

    if (!sPlayerBotMgr.DeleteWanderBot(pTarget))
    {
        SendSysMessage("Target is not a wandering bot.");
        SetSentErrorMessage(true);
        return false;
    }

    PSendSysMessage("Wandering bot %s removed.", pTarget->GetName());
    return true;
}

bool ChatHandler::HandleWanderBotRemoveAllCommand(char* args)
{
    sPlayerBotMgr.DeleteWanderBots();
    SendSysMessage("All wandering bots removed. New ones come at the next refresh (WanderBot.Refresh).");
    return true;
}

bool ChatHandler::HandleWanderBotInfoCommand(char* args)
{
    WanderBotStats stats;
    sPlayerBotMgr.GetWanderBotStats(stats);

    PSendSysMessage("Wandering bots: %u online, %u loading, target %u (min %u, max %u, refresh %u ms).",
        stats.online, stats.loading, sPlayerBotMgr.GetWanderBotsTarget(), sWorld.getConfig(CONFIG_UINT32_WANDER_BOT_MIN_BOTS),
        sWorld.getConfig(CONFIG_UINT32_WANDER_BOT_MAX_BOTS), sWorld.getConfig(CONFIG_UINT32_WANDER_BOT_REFRESH));
    PSendSysMessage("Alliance: %u, Horde: %u, dead: %u.", stats.alliance, stats.horde, stats.dead);
    PSendSysMessage("Pets: %u of %u hunters (level %u+), %u of %u warlocks.", stats.huntersWithPet, stats.hunters,
        WANDER_BOT_STATS_HUNTER_PET_LEVEL, stats.warlocksWithPet, stats.warlocks);

    std::string levels;
    for (uint32 band = 0; band < WANDER_BOT_STATS_LEVEL_BANDS; ++band)
    {
        uint32 const minLevel = band * WANDER_BOT_STATS_LEVEL_BAND + 1;
        if (!levels.empty())
            levels += ", ";
        levels += std::to_string(minLevel) + "-" + std::to_string(minLevel + WANDER_BOT_STATS_LEVEL_BAND - 1) + ": " + std::to_string(stats.perLevelBand[band]);
    }
    PSendSysMessage("Levels %s.", levels.c_str());
    PSendSysMessage("Wander nodes: %u loaded, %u spawn nodes.", sWanderNodeMgr.GetNodeCount(), sWanderNodeMgr.GetSpawnNodeCount());

    // The selected wandering bot, if any.
    if (Player* pTarget = GetSelectedPlayer())
    {
        if (WanderBotAI* pAI = dynamic_cast<WanderBotAI*>(pTarget->AI()))
        {
            if (WanderNode const* pNode = pAI->GetCurrentNode())
                PSendSysMessage("%s (level %u): node %u '%s' (levels %u-%u), %.1f yards away.", pTarget->GetName(), pTarget->GetLevel(),
                    pNode->id, pNode->name.c_str(), pNode->minLevel, pNode->maxLevel, pTarget->GetDistance2d(pNode->x, pNode->y));
            else
                PSendSysMessage("%s (level %u): no current node.", pTarget->GetName(), pTarget->GetLevel());
        }
    }

    return true;
}

bool ChatHandler::HandleWanderBotListCommand(char* args)
{
    WanderBotListFilter const filter(args);

    std::vector<Player*> bots;
    sPlayerBotMgr.GetWanderBots(bots);
    std::sort(bots.begin(), bots.end(), [](Player const* a, Player const* b) { return std::string(a->GetName()) < b->GetName(); });

    uint32 shown = 0;
    uint32 matched = 0;
    for (Player const* pBot : bots)
    {
        ChrRacesEntry const* pRace = sChrRacesStore.LookupEntry(pBot->GetRace());
        ChrClassesEntry const* pClass = sChrClassesStore.LookupEntry(pBot->GetClass());
        AreaEntry const* pZone = AreaEntry::GetById(pBot->GetZoneId());
        char const* race = pRace ? pRace->name[0] : "Unknown";
        char const* class_ = pClass ? pClass->name[0] : "Unknown";
        char const* faction = pBot->GetTeam() == ALLIANCE ? "Alliance" : "Horde";
        char const* zone = pZone ? pZone->Name : "Unknown";

        if (!filter.Matches(pBot, race, class_, faction, zone))
            continue;

        if (++matched > WANDER_BOT_COMMAND_MAX_LIST)
            continue;

        ++shown;
        PSendSysMessage("%s - level %u %s %s, %s, %s%s", pBot->GetName(), pBot->GetLevel(), race, class_, faction, zone,
            pBot->IsDead() ? " (dead)" : "");
    }

    if (matched > shown)
        PSendSysMessage("%u of %u wandering bots shown: narrow the filter to see the rest.", shown, matched);
    else
        PSendSysMessage("%u of %u wandering bots match.", matched, uint32(bots.size()));
    return true;
}

bool ChatHandler::HandleWanderBotNodesCommand(char* args)
{
    Player* pPlayer = m_session->GetPlayer();

    float range = WANDER_BOT_COMMAND_NODES_RANGE;
    ExtractOptFloat(&args, range, WANDER_BOT_COMMAND_NODES_RANGE);
    range = std::min(std::max(range, 0.0f), WANDER_BOT_COMMAND_NODES_MAX_RANGE);

    std::vector<WanderNode const*> nodes;
    sWanderNodeMgr.GetNodesInRange(pPlayer->GetMapId(), pPlayer->GetPositionX(), pPlayer->GetPositionY(), range, nodes);
    std::sort(nodes.begin(), nodes.end(), [pPlayer](WanderNode const* pNode1, WanderNode const* pNode2)
    {
        return pNode1->GetDistance2dSq(pPlayer->GetPositionX(), pPlayer->GetPositionY()) < pNode2->GetDistance2dSq(pPlayer->GetPositionX(), pPlayer->GetPositionY());
    });

    PSendSysMessage("%u wander nodes within %.0f yards:", uint32(nodes.size()), range);
    for (WanderNode const* pNode : nodes)
    {
        std::string links;
        for (WanderNode const* pLink : pNode->links)
        {
            if (!links.empty())
                links += " ";
            links += std::to_string(pLink->id);
        }
        PSendSysMessage("%u '%s': levels %u-%u, flags 0x%X, %.1f yards, links %s", pNode->id, pNode->name.c_str(), pNode->minLevel,
            pNode->maxLevel, pNode->flags, pPlayer->GetDistance2d(pNode->x, pNode->y), links.c_str());
    }
    return true;
}
