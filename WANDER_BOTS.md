# Wandering bots

Temporary bots of random race, class, level (1-60), talents, gear and name that roam both continents between wander
nodes fitting their level. They fight monsters and, on a PvP realm, enemy players and wandering bots. They never
level up and are never saved: every server start makes new ones. Progress and design notes: `WANDER_BOTS_STATUS.md`.

## Configuration (`mangosd.conf`, "PLAYER BOTS" section)

| Key | Default | What it does |
| --- | --- | --- |
| `WanderBot.Enable` | 1 | 0 turns them off: no wander nodes loaded, no bots. |
| `WanderBot.MinBots` / `WanderBot.MaxBots` | 60 / 120 | The bot count moves between these. |
| `WanderBot.Refresh` | 60000 | Every this many ms a new count in [min, max] is picked; bots are added or removed a few at a time. |
| `WanderBot.AutoEquip` | 1 | Gear: 0 starting items, 1 random items for the level, 2 premade gear. |
| `WanderBot.RandomGearLevelDifference` | 10 | How far below the bot's level random items may be. |
| `WanderBot.PlayerMapInterval` | 0 | Every this many ms the online bots are written to `characters_playermap` for the playermap web apps (0 = off). |
| `PlayerBot.Debug` | 0 | 1 logs each spawn, node reached and death (with the killer) to `Server.log`. |
| `PlayerBot.ShowInWhoList` | 0 | 1 lists bots in the in-game `/who`. |

Wandering bots skip the login queue and do not count towards `PlayerLimit`.

## GM commands

`.wanderbot` needs an account of GM level 6 (administrator); `.goname` / `.namego` need level 2.

| Command | What it does |
| --- | --- |
| `.wanderbot info` | Counts: online, loading, target, Alliance / Horde, dead, pets, per level band, nodes loaded. With a wandering bot selected, also the node it is walking to and how far away it is. |
| `.wanderbot add [level] [count]` | Adds `count` bots (default 1) of `level` (default 0 = random). |
| `.wanderbot remove` | Removes the selected wandering bot. |
| `.wanderbot removeall` | Removes all of them; new ones come at the next refresh. |
| `.wanderbot list [filter]` | Lists the online bots by name: level, race, class, faction, zone, and "(dead)". The filter is a level (`25`), a level range (`20-30`), or a text found in the name, race, class, faction or zone (`warlock`, `horde`, `stranglethorn vale`), case-insensitive. At most 50 lines. Works from the server console too. |
| `.wanderbot nodes [range]` | Lists the wander nodes within `range` yards of you (default 50). |

### Finding a bot and going to it

To find one:

- **`.wanderbot list [filter]`**: names, levels, classes and zones, e.g. `.wanderbot list barrens` or
  `.wanderbot list 40-50`.
- **`/who`** (with `PlayerBot.ShowInWhoList = 1`): bots show up like players. Filter it, as the client allows:
  `/who 20-30` (levels), `/who z-"Stranglethorn Vale"` (zone), `/who c-Warlock` (class), `/who r-Orc` (race).
  At most 49 results. A GM account sees both factions; a normal account only its own unless
  `AllowTwoSide.WhoList = 1`.
- **The playermap** (`WanderBot.PlayerMapInterval` set, see below): every bot is a pin with its name, level, class
  and zone. Clicking a bot pin copies `.goname <name>` (to where the bot is now; your own pin copies `.go xyz`).
- **`PlayerBot.Debug = 1`**: `Server.log` names each bot as it spawns, with its level, class, zone and node.
- **`.wanderbot nodes`**: shows the nodes near you; bots of fitting levels walk between them.

Once you have a name:

| Command | What it does |
| --- | --- |
| `.goname <name>` | Teleports you to that bot (any online player). |
| `.namego <name>` | Summons the bot to you. It then heads back to the node it was walking to (teleported there if it gets stuck). |
| `.go xyz <x> <y> <z> [map]` | Teleports you to a position (what a click on a player's playermap pin copies). |

Select the bot, then `.wanderbot info` shows which node it is heading to, and `.wanderbot remove` removes it.

## Playermap

With `WanderBot.PlayerMapInterval` set (5000 in `my_confs`), the server replaces the rows of `characters_playermap`
(characters DB) with the online wandering bots at that interval, and empties the table at startup and shutdown. The
table comes from `sql/migrations/20261020061142_characters.sql`. The vmangos playermap apps in
`Code2/Python/wander_nodes_util` read it and show the bots as playerbots:

```
playermap.ps1 vmangos        # py (default)
playermap.ps1 vmangos js
playermap.ps1 vmangos ts
```

There is no php playermap for vmangos (php is only for acore / tcore).
