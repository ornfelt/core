# Wandering bots - status
Last run: 2026-10-19 - step 0 (branch, inventory, design), step 1 (the `WanderBot.*` config keys), step 2 a / b / d
(the node converter, the `wander_bot_nodes` migration applied locally, `WanderNodeMgr` loaded at startup) and
most of 2c (area remap and capital flags at load).
Next: step 3 - spawning: `PlayerBotMgr` min / max loop, `WanderBotAI` (derived from `BattleBotAI`, see
"Decisions") with its first-update init; finish 2c's open part on the way (see "Open issues").

## Steps

- [x] 0 Setup (2026-10-19): `wander-bots` branch from `development`; inventory of `src/game/PlayerBots`,
      NPCBots' wander code / node SQL and cmangos-tbc's random bot logic (summary under "Design" and
      "Decisions"); this ledger.
- [x] 1 Config (2026-10-19): `CONFIG_BOOL_WANDER_BOT_ENABLE` and `CONFIG_UINT32_WANDER_BOT_*` in
      `src/game/World.h`, loaded in `World::LoadConfigSettings` (`src/game/World.cpp`, max below min raised
      with an error); documented in the "PLAYER BOTS" section of `src/mangosd/mangosd.conf.dist.in`,
      `my_confs/mangosd.conf`, `my_confs/linux/mangosd.conf`, and the run conf `bin/Release/mangosd.conf`
      (gitignored). Nothing reads the keys yet: steps 2 and 3 wire them in.
- [~] 2 Wander nodes:
  - [x] 2a (2026-10-19) Converter `contrib/wander_nodes/convert_npcbots_wander_nodes.py`: replays the 19 NPCBots
        `*wander_nodes*.sql` files in file-name order in memory (no DB needed: the local DB user cannot create
        a scratch schema), keeps maps 0 / 1, masks the flags, clamps levels to 1-60 (drops min level > 60),
        drops links to removed nodes, writes the migration. `-n` dry run, `-v`, `--timestamp` to regenerate,
        `--show-cmd` prints an equivalent mysql replay.
  - [x] 2b (2026-10-19) `sql/migrations/20261019184711_world.sql` (generated): `wander_bot_nodes`, 811 nodes on
        map 0 (45 spawn), 1507 on map 1 (48 spawn). Applied to the local `vmangos_mangos`. Regenerate with
        `python contrib/wander_nodes/convert_npcbots_wander_nodes.py --timestamp 20261019184711`.
  - [~] 2c (2026-10-19) Done at load in `WanderNodeMgr`: area ids 1.12 lacks are remapped from the terrain;
        capital zones (`area_template.flags & AREA_FLAG_CAPITAL`) force their team's faction flag (the
        NPCBots data already has them right: 1 node each in Stormwind / Ironforge / Darnassus / Thunder Bluff).
        Left: the zone level bands against vanilla (`area_template.area_level`), and an explicit check for
        WotLK-only spots on old terrain (Stormwind Harbor, the Northrend zeppelin / boat docks).
  - [x] 2d (2026-10-19) `src/game/PlayerBots/WanderNodeMgr.h/.cpp` (+ CMake, loaded in
        `World::SetInitialWorldSettings` before `sPlayerBotMgr.Load()`): load only with `WanderBot.Enable`,
        validation (continent map, terrain under the node, not > 50 yd above ground / water, flags, levels,
        areas, capital flags, links), `SelectSpawnNode`, `SelectNextNode`, `FindNearestNode`,
        `GetNodesInRange`; a load summary (per map, per level band, skipped / fixed counts, levels a team has
        no spawn node for).
- [ ] 3 Spawning: `PlayerBotMgr` min / max loop (`AddWanderBot`, `DeleteWanderBot`), staggered logins,
      random class / team / race / level / name, spawn at a fitting node, `WanderBotAI` init (gear with
      `WanderBot.AutoEquip` and `WanderBot.RandomGearLevelDifference`, talents, spells, reagents, ammo, pet),
      no XP, PvP flag on PvP realms.
- [ ] 4 Wandering: movement along links, level-fitting next node, pauses, rest, stuck handling, death and
      return.
- [ ] 5 Fighting: monster targeting by level, defending, PvP rules, leash, pets.
- [ ] 6 Commands: `.wanderbot add|remove|removeall|info|nodes` + help text (`command` table migration).
- [ ] 7 Polish and load: mounts from 40, load numbers for 0 / 60 / 120 bots, warnings, final review.

## Design

### Files
- `src/game/PlayerBots/WanderNodeMgr.h/.cpp`: `WanderNode` (id, name, map, zone, area, min / max level,
  min / max wait, proximity, flags, position, links as node pointers; `FitsLevel`, `IsWalkableFor`,
  `IsStartPointFor`), the loader, validation and the graph queries. Singleton `sWanderNodeMgr`. New sources go
  into the explicit lists in `src/game/CMakeLists.txt`.
- `src/game/PlayerBots/WanderBotAI.h/.cpp` (new): the AI. Split if it grows: `WanderBotMovement.cpp`
  (node walking, stuck, death return) and `WanderBotTargeting.cpp` (target scan, PvP rules, leash).
- `src/game/PlayerBots/PlayerBotMgr.h/.cpp`: `WanderBot.*` loop, `AddWanderBot(level)`, `DeleteWanderBot`,
  counts per faction / level for `.wanderbot info`.
- `src/game/Chat/Chat.cpp`, `Chat.h`: `.wanderbot` command table.
- `sql/migrations/<ts>_world.sql`: `wander_bot_nodes` (+ `command` help rows in step 6).
- `contrib/wander_nodes/convert_npcbots_wander_nodes.py`.

### Table `wander_bot_nodes` (world DB)
`id` int unsigned PK, `name` varchar(100), `map_id` smallint unsigned, `zone_id` int unsigned, `area_id` int
unsigned, `min_level` / `max_level` tinyint unsigned, `min_wait_time` / `max_wait_time` int unsigned (ms,
default 3000 / 7000), `proximity` float (random offset radius at the node), `flags` int unsigned,
`position_x` / `_y` / `_z`, `orientation` float, `links` text (`"id id id"`; NPCBots weights are all 0, so
weights are dropped and the pick is uniform).

Flags (same bit values as NPCBots' `BotWPFlags`, only these kept):

| Flag | Value | Meaning |
| --- | --- | --- |
| `WANDER_NODE_FLAG_SPAWN` | 0x00001 | bots can spawn / be teleported here |
| `WANDER_NODE_FLAG_ALLIANCE_ONLY` | 0x00002 | Horde bots never move here; with SPAWN, Alliance spawn point |
| `WANDER_NODE_FLAG_HORDE_ONLY` | 0x00004 | Alliance bots never move here; with SPAWN, Horde spawn point |
| `WANDER_NODE_FLAG_CAN_BACKTRACK_FROM` | 0x00008 | the previous node may be picked even if other links exist |
| `WANDER_NODE_FLAG_IGNORES_FACTION` | 0x00010 | faction flags ignored when walking (not spawning) here |
| `WANDER_NODE_FLAG_IGNORES_PATHING` | 0x00020 | no mmaps path between two nodes with this flag (direct move) |
| `WANDER_NODE_FLAG_JUMP_BEGIN` / `_END` | 0x10000 / 0x20000 | the move between them is a forced jump |
| `WANDER_NODE_FLAG_MILL_RADIUS` | 0x40000 | mill radius at the node reduced to interaction distance |
| `WANDER_NODE_FLAG_NOT_A_START_POINT` | 0x80000 | never a spawn / teleport target |

The BG flags (0x40-0x8000) are dropped (BG maps are not imported).

### Source data (NPCBots, inventory 2026-10-19)
`AzerothCore-wotlk-with-NPCBots/data/sql/custom/db_world/*npcbot_wander_nodes*.sql`. The final state of maps
0 / 1 is ids 1-2371 from `2023_04_04_00`, plus the flag / level `UPDATE`s of `2023_05_14`, `2023_06_16`,
`2026_03_12`, and the links rewired by the trailing `UPDATE ... SET links` of `2023_06_09`. Columns
`minwaittime`, `maxwaittime`, `proximity` come from the `2026_01_07` `ALTER` (defaults 3000 / 7000 / 0).
About 816 nodes on map 0 and 1507 on map 1. Links are `"id:weight "` tokens, all weights 0, mostly mutual.

### Bot lifecycle
1. `PlayerBotMgr::Load` (only with `WanderBot.Enable` and nodes loaded) queues `MinBots` wandering bots.
   `PlayerBotMgr::Update`: every `WanderBot.Refresh` ms pick a target in [min, max]; the difference to the
   online + queued count is added / removed, at most `WANDER_BOT_SPAWNS_PER_UPDATE` per manager update.
2. `AddWanderBot(level)`: team 50/50, class from the team's valid list (as `AddBattleBot`), race via
   `SelectRandomRaceForClass`, level `urand(1, 60)` (class minimum respected), spawn node from
   `sWanderNodeMgr.GetSpawnNode(team, level)`, `new WanderBotAI(...)`, `AddBot(ai)`. Name / appearance from
   `SpawnNewPlayer` (`GenerateFreePlayerName`, `SelectRandomAppearance`). Never saved (`PlayerBot.AllowSaving`
   is not used by these bots: `LogoutPlayer(false)`).
3. `WanderBotAI` first update (as `BattleBotAI`): `GiveLevel`, `InitTalentForLevel`, talents (premade spec when
   one exists for the class / level, else `LearnRandomTalents` + trainer spells), `AutoEquipGear(WanderBot.AutoEquip)`
   with the wander gear level difference, spells, reagents, ammo, pet, `SetPersonalXpRate(0)`, PvP flag on
   PvP realms.

### AI states (WanderBotAI)
`SPAWNING` -> `WANDERING` (walk to the current node) <-> `PAUSED` (at a node, `min/max_wait_time`) <->
`RESTING` (eat / drink below thresholds) <-> `FIGHTING` (target from the scan, leash from the route) ->
`DEAD` (release after a delay, revive at the graveyard, pick the nearest fitting node) -> `WANDERING`.
`STUCK`: no distance progress for a while -> next node; repeated -> teleport to a fitting spawn node.

## Decisions

- **WanderBotAI derives from BattleBotAI** (decided 2026-10-19). `BattleBotAI` already has full per-class
  in / out of combat rotations against players, eating / drinking, mounts and unreachable-target handling
  (about 2700 lines). Deriving reuses them as they are; the only change to `BattleBotAI` is dropping `final`
  from `UpdateAI`, `OnPlayerLogin`, `OnPacketReceived`, `MovementInform` (so `WanderBotAI` can override
  them). BG-specific code is guarded by `GetBattleGround()` / `m_battlegroundId` and stays inert. Because a
  `WanderBotAI` is a `BattleBotAI`, `DeleteBattleBots` and the `.battlebot` commands must skip wandering bots.
  The alternative (derive from `CombatBotBaseAI` and copy the rotations, or move them into the base) was
  rejected as a large duplication or a large refactor of working code.
- **Config in `World`** (`sWorld.getConfig(CONFIG_*)`), like `PartyBot.*` / `BattleBot.AutoEquip`, so both
  `PlayerBotMgr` and the AI read the same values. `WanderBot.Refresh` has a minimum of 1000 ms.
  `WanderBot.MaxBots < MinBots` is raised to `MinBots` with an error log.
- **No XP**: `me->SetPersonalXpRate(0.0f)` at init. `Player::GiveXP` multiplies by the personal rate and
  returns below 1, so no change to the XP path is needed.
- **Level fit** (from NPCBots `GetNextWanderNode`): a node fits level L when `L + 2 >= min_level` and
  `L <= max_level` (`WANDER_NODE_LEVEL_MARGIN = 2`). Spawn nodes: the same check plus `SPAWN` and not
  `NOT_A_START_POINT`.
- **Faction fit** (from NPCBots `IsWanderNodeAvailableForBotFaction`): walking: `IGNORES_FACTION` passes,
  else Alliance needs no `HORDE_ONLY`, Horde no `ALLIANCE_ONLY`. Spawning / teleporting on nodes with
  `max_level <= 10` needs the bot's own faction flag (start zones are never shared); elsewhere the same rule
  as walking. Step 2c adds the faction flag to nodes inside the enemy capitals if NPCBots lacks it.
- **Next node** (from NPCBots): the current node's links that fit level and faction; the previous node removed
  unless it is the only one or the current node has `CAN_BACKTRACK_FROM`; uniform random pick. No fitting
  link: walk to the nearest fitting node within `WANDER_NODE_MAX_WALK_DISTANCE` (800 yd), else teleport to a
  fitting spawn node (the vanilla equivalent of NPCBots' wanderer hearthstone).
- **Pauses** at nodes: `urand(min_wait_time, max_wait_time)`; the destination is offset by up to
  `proximity` yards when it is set.
- **Targets** (from NPCBots `_getTargets`, adapted): creatures that are hostile, not critters, not more than
  6 levels above (3 for elites) or 4 below (8 for elites) the bot; players / wandering bots of the other
  faction on PvP realms within 12 above / 9 below. Units already attacking the bot are always valid.
  Out of combat, no new target when HP is below 85 % (resting first).
- **Death** (skill default): release after a short delay, revive at the corpse or graveyard
  (`RepopAtGraveyard` + `ResurrectPlayer`), then continue from the nearest fitting node. Not despawned.
- **Staggered spawning**: the per-bot `EquipRandomGearInEmptySlots` scans every item prototype, so spawns are
  capped per manager update (`WANDER_BOT_SPAWNS_PER_UPDATE`).
- **Hostile towns** (from cmangos-tbc `RandomTeleport`): enemy capitals are never spawn / walk targets; the
  node flags plus a check of `AreaTableEntry::team` with the capital flag at load.
- **Node validation at load is terrain only**: vmaps and mmaps are loaded per grid / tile when a map grid
  becomes active, so at startup only the terrain grid is reliable. A node is skipped when it has no terrain or
  is more than 50 yd above the ground / water level; nodes below the terrain (Ironforge, Undercity, caves) are
  valid. Reachability between linked nodes is checked at runtime: the wander movement (step 4) treats a
  link the bot cannot path or keeps getting stuck on as broken for that bot and picks another.
- **Run setup on this machine** (Windows): build with `cmake --build build --config Release`; the binaries and
  the run confs are in `bin/Release` (gitignored; the counterpart of `~/vmangos`). `CMAKE_INSTALL_PREFIX`
  (`C:/Program Files/MaNGOS`) does not exist and is not used. DBs: `vmangos_mangos` / `vmangos_characters` /
  `vmangos_realmd` on the local MySQL 8.0 (connection lines in `bin/Release/mangosd.conf`). The realm is
  `GameType = 1` (PvP).

## Verification

- 2026-10-19 build: `cmake --build build --config Release -j 12`, exit 0, no warnings (whole log), no errors.
- 2026-10-19 runtime (mangosd only, `bin/Release`, console `server info` + `server shutdown 1`; ports 3724 / 8085
  were free and no other server was running, so nothing had to be stopped / restored):
  - config: no `WanderBot.*` range errors; world up in about 9 s, clean shutdown, exit 0.
  - nodes: `>> Loaded 2318 wander nodes, 93 spawn nodes.` map 0: 811, map 1: 1507; per min-level band
    500 / 446 / 274 / 497 / 423 / 178; skipped 0 / 0 / 0; fixed: 0 flags, 0 levels, 18 areas from the terrain,
    0 capital flags; 0 links dropped, 0 nodes without links. No "no Alliance / Horde spawn node for levels"
    error, so both teams have a spawn node for every level 1-60.
  - no bots spawned yet (step 3), so no update time numbers.

## Open issues

- 18 nodes get their area from the terrain at load: 4 use area ids 1.12 lacks (215 Plaguewood Tower 4067,
  233 Mudsprocket 4010, 1905 / 1907 Dustwallow 4046 / 4049), 14 sit at dungeon entrances whose vanilla area is
  filed under the instance map (Deadmines, Wailing Caverns, Uldaman, Maraudon, the Karazhan cellar). Both are
  handled; Mudsprocket (a TBC town) is a spawn node in an empty spot of 1.12 Dustwallow - harmless, but could
  lose its spawn flag in 2c.
- Dropped at conversion: 92 "Grim Batol", 451-454 "DragonmawGates*" (level 61-70 in NPCBots, elite dragons in
  1.12). 409 "Seradane" was 60-80, clamped to 60-60.
- Stormwind Harbor / WotLK zeppelin and boat docks: not in 1.12. 2d's ground / mmaps validation must catch nodes
  there (water or no path) and 2c should list them explicitly.

- NPCBots' node data is WotLK: zones on maps 0 / 1 mostly match 1.12, but its level bands (e.g. 33-45) need
  a check against vanilla zone levels in step 2c, and nodes in places 1.12 lacks must be dropped.
- `RandomBot.Enable = 1` (Min 50 / Max 150) in both `my_confs` copies is unrelated to wandering bots and left
  as it is.
- Uncommitted `src/shared/Progression.h` (the user's client build tweak) is left alone.
