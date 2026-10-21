# Wandering bots - status
User guide (config, GM commands, finding / teleporting to bots, playermap): `WANDER_BOTS.md`.
Last run: 2026-10-20 (run 4) - step 2c finished (zone levels checked, Mudsprocket spawn flag removed), step 5
finished (deaths by cause: hostile-guard nodes closed per team, revive danger check, hunter pets recalled, warlock
demons learned from class quests), step 7 mostly (load numbers for 0 / 60 / 120 bots, `AddCooldown` errors traced).
Then (run 5, the user's request): wandering bots on the playermap web apps (`characters_playermap`, step 8).
Next: step 7's rest - an in-game look with a 1.12 client (the user), then mark the work done; optional tuning from
"Open issues".

## Steps

- [x] 0 Setup (2026-10-19): `wander-bots` branch from `development`; inventory of `src/game/PlayerBots`,
      NPCBots' wander code / node SQL and cmangos-tbc's random bot logic (summary under "Design" and
      "Decisions"); this ledger.
- [x] 1 Config (2026-10-19): `CONFIG_BOOL_WANDER_BOT_ENABLE` and `CONFIG_UINT32_WANDER_BOT_*` in
      `src/game/World.h`, loaded in `World::LoadConfigSettings` (`src/game/World.cpp`, max below min raised
      with an error); documented in the "PLAYER BOTS" section of `src/mangosd/mangosd.conf.dist.in`,
      `my_confs/mangosd.conf`, `my_confs/linux/mangosd.conf`, and the run conf `bin/Release/mangosd.conf`
      (gitignored).
- [x] 2 Wander nodes:
  - [x] 2a (2026-10-19) Converter `contrib/wander_nodes/convert_npcbots_wander_nodes.py`: replays the 19 NPCBots
        `*wander_nodes*.sql` files in file-name order in memory (no DB needed: the local DB user cannot create
        a scratch schema), keeps maps 0 / 1, masks the flags, clamps levels to 1-60 (drops min level > 60),
        drops links to removed nodes, writes the migration. `-n` dry run, `-v`, `--timestamp` to regenerate,
        `--show-cmd` prints an equivalent mysql replay.
  - [x] 2b (2026-10-19) `sql/migrations/20261019184711_world.sql` (generated): `wander_bot_nodes`, 811 nodes on
        map 0 (45 spawn), 1507 on map 1 (48 spawn). Applied to the local `vmangos_mangos`. Regenerate with
        `python contrib/wander_nodes/convert_npcbots_wander_nodes.py --timestamp 20261019184711`.
  - [x] 2c (2026-10-19 / run 4) Done at load in `WanderNodeMgr`: area ids 1.12 lacks are remapped from the
        terrain; capital zones (`area_template.flags & AREA_FLAG_CAPITAL`) force their team's faction flag.
        Run 4: the node level bands per zone checked against the 1.12 zone ranges (`area_template.area_level` is 0
        for almost every zone, so the 1.12 ranges were used): they match (NPCBots' WotLK bands equal vanilla for
        these zones; the wide ranges come from border nodes, e.g. Barrens nodes on the Dustwallow coast at 33-45,
        Twilight Grove / Bough Shadow / Tainted Scar at 50-60). No Stormwind Harbor or WotLK zeppelin-tower nodes
        exist (searched by name and position). Mudsprocket (233, a TBC town) loses its spawn flag in the converter
        (`FLAGS_REMOVED_FOR_1_12`); migration regenerated with the same timestamp, local DB updated. Run 4 also
        closes nodes within 40 yd of a hostile guard spawn per team (see "Decisions").
  - [x] 2d (2026-10-19) `src/game/PlayerBots/WanderNodeMgr.h/.cpp` (+ CMake, loaded in
        `World::SetInitialWorldSettings` before `sPlayerBotMgr.Load()`): load only with `WanderBot.Enable`,
        validation, `SelectSpawnNode`, `SelectNextNode`, `FindNearestNode`, `GetNodesInRange`; a load summary.
        Run 2: `FindNearestNode` takes a node to exclude; `SelectSpawnNode` only picks nodes with a link that fits
        the team and level (`WanderNode::HasLinkFor`); `GetSpawnNodeCount`.
- [x] 3 Spawning (2026-10-19): `PlayerBotMgr::UpdateWanderBots` (every `PlayerBot.UpdateMs` manager update: a new
      target in [min, max] every `WanderBot.Refresh`, at most `WANDER_BOT_CHANGES_PER_UPDATE` = 4 bots added or
      removed per update), `AddWanderBot(level)` (team 50/50, `SelectRandomClassForTeam` - factored out of
      `AddBattleBot`, same behavior -, `SelectRandomRaceForClass`, level 1 - `MaxPlayerLevel`, a fitting spawn
      node), `WanderBotAI::Initialize` (level, talents and spells, role, gear with `WanderBot.AutoEquip` and
      `WanderBot.RandomGearLevelDifference`, reagents, skills, pet, XP rate 0). `SpawnNewPlayer` now creates a
      continent no player has entered yet instead of failing (before: `Map (1, 0) not found`).
      Run 3: wandering bot sessions never queue and do not take a player slot: `World::CanSkipQueue` lets them
      through, and the `PlayerLimit` / `PlayerHardLimit` checks and the realm population use the new
      `World::GetPlayerLimitedSessionCount()` (active sessions minus wandering bots, via
      `PlayerBotMgr::IsExemptFromPlayerLimit`). Documented under `WanderBot.MaxBots` in the confs.
- [x] 4 Wandering (2026-10-19): `WanderBotMovement.cpp` - walk along links (`MovePoint` with pathfinding,
      straight between two `IGNORES_PATHING` nodes, random offset within `proximity`), level / faction-fitting
      next node, the nearest fitting node within 800 yd at a dead end, else a teleport to a fitting spawn node;
      pauses (`min/max_wait_time`); mount for legs over 60 yd (`BattleBotAI::UseMount`, level 40+); rest
      below 85 % health / 60 % mana until full (`BattleBotAI::DrinkAndEat`); stuck: < 3 yd in 5 s three times ->
      turn back to the previous node once, then a teleport onto the node. Death: release after 5 s, revive at the
      graveyard 10 s later at 50 %, go on from the nearest fitting node.
- [x] 5 Fighting (2026-10-19 / run 4): `WanderBotTargeting.cpp` - attackers first (nearest), then the pet's attacker, then
      (rested only) hostile monsters within 25 yd and LOS (no critters, civilians, guards, world bosses, no
      monster fighting someone else; normal ones up to 3 levels above / 4 below, elites only 3+ levels below
      the bot, down to 8 below) and, on PvP realms, enemy-faction players / bots / their pets 12 above / 9 below
      with no guard within 50 yd of them; leash 60 yd from where the fight started unless the target attacks the
      bot. Rotations, pets and trinkets come from `BattleBotAI`.
      Run 4: the death log names the last enemy and the attacker count (`PlayerBot.Debug = 1`); `.wanderbot info`
      counts live pets per class. Fixes from the causes: nodes near hostile guards closed per team, a revived bot
      next to a hostile guard or a much stronger monster is moved to a spawn node (death loops at the Thelsamar and
      Wetlands graveyards), hunters recall / revive a lost pet out of combat, all bots learn their class-quest
      spells (warlocks had no demon at all). See "Verification".
- [x] 6 Commands (2026-10-19; `list` run 5): `.wanderbot add [level] [count]` (raises the current target by the bots added),
      `remove` (selected bot, lowers the target), `removeall` (target 0 until the next refresh), `info` (online /
      loading / target, per faction, dead, per level band, nodes, the selected bot's node), `nodes [range]`,
      `list [filter]` (run 5: name, level, race, class, faction, zone; a level, a level range or a text filter;
      50 lines at most; console too; teleporting is `.goname <name>`, see `WANDER_BOTS.md`).
      `src/game/PlayerBots/WanderBotCommands.cpp`, the table in `src/game/Chat/Chat.cpp` with the help text in
      code (as the other bot tables; the world `command` table holds only 2 rows, so no migration).
- [~] 7 Polish and load (run 4): load numbers for 0 / 60 / 120 bots (see "Verification"), `AddCooldown` errors
      traced to the shared trinket / wand code (left, see "Decisions"), deaths per hour down from 12.7 to 8.8,
      no build warnings, review against "Requirements" (below). Mounts from level 40 were already in step 4.
      Left: an in-game look with a 1.12 client (needs the user).

- [x] 8 Playermap (2026-10-20, run 5, asked by the user): the server writes the online wandering bots to
      `characters_playermap` (characters DB, migration `20261020061142_characters.sql`) every
      `WanderBot.PlayerMapInterval` ms (default 0 = off; 5000 in `my_confs` and the run conf). The vmangos playermap
      apps in `Code2/Python/wander_nodes_util` (`py_map/app_cmangos.py`, `js_map_tbc/server.js`,
      `ts_map_tbc/src/server.ts`, a separate repo) read it and show the rows as playerbots. No php map exists for
      vmangos (the launcher allows php only for acore / tcore).

### Review against "Requirements" (run 4)
- Conf min / max (60 / 120 by default) and an off switch: `WanderBot.Enable`, `MinBots`, `MaxBots` (step 1);
  `Enable = 0` loads no nodes and spawns nothing (run 4, 0-bot run: "Wander nodes: 0 loaded").
- Wander anywhere between nodes: 2318 nodes on both continents, about 535 node arrivals per 8 minutes with 120 bots.
- Attack monsters, and on a PvP realm enemy players / wandering bots: yes (35 of 141 deaths in run 4b were to
  enemy wandering bots, so PvP happens both ways). Real players: same code path, not seen with a real player.
- Random spawn places, levels 1-60, gear, names, race / class, talents: yes (step 3).
- Level-appropriate zones: node level fit plus the margin; deaths spread over the level bands.
- No leveling: XP rate 0, and every XP source (kills, exploration, quests) goes through `Player::GiveXP`, which
  applies it.

## Design

### Files
- `src/game/PlayerBots/WanderNodeMgr.h/.cpp`: `WanderNode` (id, name, map, zone, area, min / max level,
  min / max wait, proximity, flags, `guardedAgainstAlliance / Horde`, position, links as node pointers;
  `FitsLevel`, `IsGuardedAgainst`, `IsWalkableFor`, `IsStartPointFor`, `HasLinkFor`), the loader, validation,
  `MarkGuardedNodes` and the graph queries. Singleton `sWanderNodeMgr`.
- `src/game/PlayerBots/WanderBotAI.h`: the class and every tuning constant (`WB_*`).
- `src/game/PlayerBots/WanderBotAI.cpp`: init (`LearnClassQuestSpells`), `UpdateAI` (the `BattleBotAI` flow minus
  the battleground parts, plus the hunter pet recall), death / revive (`RememberEnemy` / `DescribeLastEnemy` for
  the death log, the revive danger check), rest check.
- `src/game/PlayerBots/WanderBotMovement.cpp`: node walking, pauses, stuck handling, spawn-node teleport,
  `MovementInform`.
- `src/game/PlayerBots/WanderBotTargeting.cpp`: target scan, PvP rules, guard check, revive danger check, leash.
- `src/game/PlayerBots/WanderBotCommands.cpp`: the `.wanderbot` handlers.
- `src/game/PlayerBots/PlayerBotMgr.h/.cpp`: `UpdateWanderBots`, `AddWanderBot(s)`, `DeleteWanderBot(s)`,
  `GetWanderBotStats` (run 4: live pets of hunters level 10+ and warlocks), `IsDebugEnabled`; `DeleteBattleBots` and `.battlebot remove` skip wandering bots.
- `src/game/PlayerBots/CombatBotBaseAI.h/.cpp`: `virtual GetRandomGearLevelDifference()` (party bot config by
  default, so party / battle bots are unchanged) and `LearnRandomTalentsAndAllSpells()` (the existing fallback
  branch of `LearnPremadeSpecForClass`, moved out so `WanderBotAI` can call it).
- `src/game/PlayerBots/BattleBotAI.h`: `UpdateAI` and `MovementInform` are `override` instead of `final`.
- `src/game/PlayerBots/PlayerBotAI.cpp`: `SpawnNewPlayer` creates a missing continent map.
- `sql/migrations/20261019184711_world.sql`: `wander_bot_nodes`.
- `sql/migrations/20261020061142_characters.sql`: `characters_playermap` (guid, account, name, race, class, gender,
  level, map, zone, position_x / y / z, extra_flags, online, dead): the columns the playermap apps select.
- `PlayerBotMgr::UpdateWanderBotPlayerMap` / `ClearWanderBotPlayerMap`: config `WanderBot.PlayerMapInterval`
  (`CONFIG_UINT32_WANDER_BOT_PLAYER_MAP_INTERVAL`).
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
| `WANDER_NODE_FLAG_JUMP_BEGIN` / `_END` | 0x10000 / 0x20000 | the move between them is a forced jump (not used yet) |
| `WANDER_NODE_FLAG_MILL_RADIUS` | 0x40000 | mill radius at the node reduced to interaction distance (not used yet) |
| `WANDER_NODE_FLAG_NOT_A_START_POINT` | 0x80000 | never a spawn / teleport target |

The BG flags (0x40-0x8000) are dropped (BG maps are not imported).

### Source data (NPCBots, inventory 2026-10-19)
`AzerothCore-wotlk-with-NPCBots/data/sql/custom/db_world/*npcbot_wander_nodes*.sql`. The final state of maps
0 / 1 is ids 1-2371 from `2023_04_04_00`, plus the flag / level `UPDATE`s of `2023_05_14`, `2023_06_16`,
`2026_03_12`, and the links rewired by the trailing `UPDATE ... SET links` of `2023_06_09`. Columns
`minwaittime`, `maxwaittime`, `proximity` come from the `2026_01_07` `ALTER` (defaults 3000 / 7000 / 0).

### Bot lifecycle
1. `PlayerBotMgr::Load` sets the target to `MinBots` (only with `WanderBot.Enable` and nodes loaded).
2. `PlayerBotMgr::Update` -> `UpdateWanderBots` once per `PlayerBot.UpdateMs`: every `WanderBot.Refresh` ms a new
   target `urand(min, max)`; counts the wandering bots that are loading or online and not being removed; adds
   or flags for removal (random online ones) up to 4 per update. Removed bots go through the existing
   `requestRemoval` path and are erased (custom bots).
3. `AddWanderBot`: `new WanderBotAI(race, class, level, map, continent instance, spawn node)` -> `AddBot(ai)`;
   name and appearance from `SpawnNewPlayer`. Never saved: `Player::Create` sets `m_saveDisabled` for these
   bots.
4. First AI update: `Initialize` (see step 3). Then `UpdateAI` every `WB_UPDATE_INTERVAL` (1 s).

### AI flow (WanderBotAI::UpdateAI)
not initialized -> `Initialize`; dead -> `UpdateDeath` (release, revive); just revived -> forget the node;
crowd control -> break it; casting -> wait; leash; stealthed victim -> drop it; out of combat: rest (below the
thresholds, or already sitting) -> out-of-combat class AI (buffs) -> new target (`SelectAttackTarget`) or
`UpdateWanderMovement`; in combat: switch / keep target as `BattleBotAI` does -> in-combat class AI.
Movement state: `m_node` (walking to, or paused at once `m_arrived`), `m_previousNode`, pause / stuck timers.

## Decisions

- **WanderBotAI derives from BattleBotAI** (decided 2026-10-19). Reuses its per-class rotations, eating /
  drinking, mounts and unreachable-target handling; `WanderBotAI` replaces `UpdateAI` and `MovementInform`
  (so only those two lost `final`; `OnPlayerLogin` / `OnPacketReceived` are inherited unchanged).
  `SelectAttackTarget` / `AttackStart` of `BattleBotAI` are not virtual: `WanderBotAI` hides
  `SelectAttackTarget` with its own (called from its own `UpdateAI` only) and wraps `AttackStart` in
  `StartAttack` (records the leash point). `DeleteBattleBots` and `.battlebot remove` skip wandering bots.
- **Config in `World`** (`sWorld.getConfig(CONFIG_*)`). `WanderBot.Refresh` has a minimum of 1000 ms.
- **No XP**: `me->SetPersonalXpRate(0.0f)` at init.
- **Talents / spells**: a premade spec when one exists for exactly the class and level (a random one of them),
  else random talents plus all trainer spells and item spells. Not `LearnPremadeSpecForClass` alone: it would
  take a lower-level template and leave talent points unspent.
- **Gear level difference**: a virtual `CombatBotBaseAI::GetRandomGearLevelDifference()` instead of a new
  parameter, so `AutoEquipGear` and its callers stay as they are.
- **Level fit** (from NPCBots): a node fits level L when `L + 2 >= min_level` and `L <= max_level`. Spawn nodes
  also need a fitting, walkable link (run 1: 1-60 `DarnassusEntrance` led only into Teldrassil 1-10 nodes, so
  higher bots spawned there were teleported away at once).
- **Faction fit**: as before (`IsWalkableFor`, `IsStartPointFor`).
- **Next node**: as NPCBots; a dead end -> the nearest fitting node within 800 yd (`WB_MAX_WALK_DISTANCE`) other
  than the current one -> else a teleport to a fitting spawn node.
- **Arrival**: `MovementInform` for point id `WB_MOVE_POINT_ID` counts only within `proximity + 5` yd of the
  node; a move cut short is left to the stuck check.
- **Rest**: below 85 % health or 60 % mana (`WB_REST_*`), then eat / drink to full (`DrinkAndEat` keeps going while
  sitting). No new target is pulled while below the thresholds; attackers are always fought.
- **Targets**: run 1 had many deaths in neutral towns (Booty Bay, Gadgetzan, Cenarion Hold, Light's Hope) - bots
  of the two factions fighting next to the neutral guards. A player target now needs no guard
  (`CREATURE_FLAG_EXTRA_GUARD`) within 50 yd of it; run 3 had no death cluster in a town. Monsters: hostile
  only (not neutral), level window tightened after run 1 (a level 1 bot died to monsters up to 6 levels
  above): normal up to 3 above, elites only when 3+ levels below the bot.
- **PvP flag**: not forced. On a PvP realm `Player::IsOutdoorPvPActive` and the zone rules flag the bots as
  they do players (in contested / enemy zones), and `IsValidAttackTarget` decides; on a normal realm the
  bots never pick a player as a new target and only fight back.
- **Death** (skill default): release after 5 s, revive at the graveyard 10 s later with 50 % health (no corpse
  run: the bot does not walk back), bones left at the corpse, then on from the nearest fitting node.
- **`PlayerLimit`** (user's choice, 2026-10-19): wandering bots skip the queue and do not count towards
  `PlayerLimit` / `PlayerHardLimit` or the realm population; party / battle / random bots are unchanged (they
  still count). Sessions enter and leave `m_sessions` in six places, so instead of a counter that could drift,
  `GetPlayerLimitedSessionCount()` scans the sessions - only with `WanderBot.Enable`, once per login / logout
  and once per world tick in `UpdateSessions`. The uptime table, `.server info` and the `Creature` respawn
  scaling still count every session (bots included).
- **Staggered spawning**: 4 bots per manager update (`PlayerBot.UpdateMs` = 1000 here: 60 bots in about 15 s).
- **`.wanderbot add` / `remove` move the current target** so the loop does not undo them at once; the next
  refresh picks a new target as usual. `removeall` sets the target to 0 until the next refresh.
- **Hostile towns**: enemy capitals are never spawn / walk targets (node flags, capital check at load).
  Run 4: at load, every node within 40 yd (`WANDER_NODE_HOSTILE_GUARD_RANGE`) of a guard spawn
  (`CREATURE_FLAG_EXTRA_GUARD`, or `CREATURE_STATIC_FLAG_CALLS_GUARDS` town folk) whose faction is hostile to the
  human / orc faction template is closed to that team (walking and spawning). Spawns from ObjectMgr's creature data
  (patch-filtered), not a SQL query.
- **Revive danger check** (run 4): run 4a's data had two graveyard death loops (one bot 11 times at the Wetlands
  graveyard to summoned Menethil Guards, one 12 times at Thelsamar to Mountaineer Stenn, who has no guard flag).
  A bot revived with a live hostile guard, or a hostile creature more than `WB_MAX_LEVEL_ABOVE` levels above it,
  within 50 yd is teleported to a fitting spawn node. Not a guard-only check: the killers there were neither
  static spawns nor guard-flagged.
- **Hunter pets** (run 4): `BattleBotAI` calls a hunter pet only at spawn, revive and BG start, so a pet lost in a
  fight stayed lost (about half the hunters had none). `WanderBotAI` calls `SummonPetIfNeeded` out of combat while
  the hunter has no live pet; `BattleBotAI` is unchanged.
- **Class-quest spells** (run 4): warlock demons, druid bear / aquatic form and the like are class-quest rewards,
  which `LearnRandomTalentsAndAllSpells` (learn-all-trainer + items) does not give, so wandering warlocks had no
  demon (0 of 17 with a pet). `LearnClassQuestSpells` teaches what the `LEARN_SPELL` effects of the reward spells
  of active class quests up to the bot's level teach (race-restricted quests only for that race). Read from the
  quest templates, not a hard-coded list.
- **Playermap** (run 5): the table name and columns are those of the user's AzerothCore NPCBots fork, so the
  apps read it the same way. The whole table is replaced in one async transaction per interval (DELETE + one
  multi-row INSERT; 120 rows), not row updates: bots that left drop out on the next write. It is emptied at startup
  (`Load`, also when wandering bots or the interval are off, so no stale bots stay on the map) and at shutdown
  (`DeleteAll`). Writes and clears use one DB serial id, so they run in order on one worker and the shutdown clear
  is never overtaken by a queued write. A missing table is found once at startup (`SHOW TABLES`) and logged; no
  writes then. Off by default in the dist conf: upstream servers do not get DB writes they did not ask for. A crash
  leaves the last rows until the next start.
- **`AddCooldown` errors**: left as they are. They come from `CombatBotBaseAI::UseItemEffect` (trinkets 23506 /
  24347, 30 of 37 in runs 4a) and the wand / auto shot calls shared with `PartyBotAI`; `Player::AddCooldown` logs
  and returns, so nothing breaks. Fixing them means changing the shared bot code (party / battle bots too).
- **Node validation at load is terrain only** (see run 1 of this ledger); reachability is handled at runtime by
  the stuck check (3 runs: 0 stuck teleports so far).
- **Run setup on this machine** (Windows): build with `cmake --build build --config Release`; the binaries and
  the run confs are in `bin/Release` (gitignored; the counterpart of `~/vmangos`). DBs on the local MySQL 8.0
  (connection lines in `bin/Release/mangosd.conf`). The realm is `GameType = 1` (PvP). Test runs use a copy of
  the run conf in the scratchpad with `PlayerBot.Debug = 1` (and in run 3 `PlayerLimit = 0`), mangosd's console
  fed through stdin.

## Verification

- 2026-10-20 (run 5, playermap) build: clean (game and mangosd, no warnings). Characters migration applied to the
  local `vmangos_characters`. One mangosd run (the run conf, 60-120 bots, `WanderBot.PlayerMapInterval = 5000`):
  50 s after start `characters_playermap` held 60 rows (both maps, levels 3-60, 2 dead), 60 again 6 s later;
  the py app's `/api/players` (Flask test client, `SELECTED_SERVER=vmangos`) returned 60 characters, all kind 2
  (playerbot), 1 dead; after the clean shutdown the table was empty. The js app passes `node --check`; the ts
  app was not type-checked (no TypeScript compiler on this machine); neither was run.
  `.wanderbot list` (console, 60 bots): no filter 50 of 60 shown with the narrow-the-filter line; `warlock` 10
  (all warlocks), `horde` 28, `20-30` 4 (levels 21-26), `barrens` 1, `stranglethorn vale` 3 (all in it),
  `zzzz` / `61-70` 0. Clean shutdown.

- 2026-10-20 (run 4) build: `cmake --build build --config Release -j 12`, exit 0, no warnings (after each of the
  4 builds). Runtime: mangosd only, from `bin/Release` (3724 / 8085 free, only MySQL listening: nothing stopped
  or restored), scratch confs with `PlayerBot.Debug = 1`, `PlayerLimit = 0`, `PerformanceLog.SlowMapSystemUpdate`
  / `SlowSessionsUpdate = 1`, console fed through stdin, clean shutdown (exit 0) every time. Load = mangosd CPU time
  over a fixed window after the bots were in (the "map system" time is wall time and includes the wait for the map
  threads: 20-22 ms median per tick with or without bots, so it does not measure the bots):

  | Run | Bots | Window | mangosd CPU (one core) | World ticks > 100 ms | Deaths / bot / hour |
  | --- | --- | --- | --- | --- | --- |
  | 4a-0 | 0 (`Enable = 0`, 0 nodes loaded) | 180 s | 0.4 % | 0 | - |
  | 4a-60 | 60 (min = max) | 180 s | 12.3 % | 0 | 9.3 |
  | 4a-120 | 120 (min = max) | 481 s | 15.4 % | 2 (max 104 ms) | 12.7 |
  | 4b | 120, guard nodes / revive check / hunter pets | 480 s | 15.5 % | 4 (max 144 ms) | 8.8 |
  | 4c | 60-120 (the defaults), plus class-quest spells | 300 s | 14.4 % | 4 (max 115 ms) | 7.4 |

  - 4a-120 deaths by cause: 146 of 225 monsters (80 in packs), 38 wandering bots, 25 no enemy, 16 elites; the two
    graveyard death loops (27 Menethil Guard, 24 Mountaineer Stenn). Pets: 8-10 of 24-27 hunters / warlocks.
  - 4b: 67 / 74 nodes closed to Alliance / Horde; no guard among the top killers; 7 spawn-node teleports (the
    revive check); pets 9-10 of 11 hunters, 0 of 17-18 warlocks (no demon spell: fixed in 4c).
  - 4c: 191 spawns, `.wanderbot info` 119 online (target 119) at the start of the window and 97 (target 97) at the
    end, within [60, 120]; Alliance 57 / Horde 62; levels 20 / 21 / 17 / 19 / 24 / 18 per band of 10; pets 12 of 13
    hunters, 19 of 21 warlocks (then 8 / 9 and 8 / 8); 312 node arrivals, 0 stuck teleports, 5 spawn-node
    teleports; deaths 35 monster packs, 18 monsters, 8 wandering bots, 9 no enemy, 4 elites.
  - Errors from bot code: `AddCooldown` (41 in 4c, see "Decisions"), and 3 `CastSpell: unknown spell id 0` in 4c
    only (see "Open issues"). No crash or assert.
  - Not run: an in-game look (needs a 1.12 client and the user), a real player in PvP with the bots.

- 2026-10-19 (run 3) build: exit 0, no warnings. Server run 4 (`PlayerLimit = 20` in the scratch conf): 62 bots
  online at 1:21 and 82 at 2:41, `0 queued` both times, Alliance 39 / Horde 43, 106 node arrivals, 17 deaths,
  clean shutdown. A real player taking a free slot next to the bots was not tested (needs a client).

- 2026-10-19 (run 2) build: `cmake --build build --config Release -j 12`, exit 0, no warnings, no errors (after
  each of the 5 builds of this run).
- 2026-10-19 (run 2) runtime, mangosd only from `bin/Release` (ports 3724 / 8085 free, no other server running,
  nothing stopped / restored), about 5 minutes each:
  - server run 1 (`PlayerLimit = 100`): nodes loaded (2318 / 93 spawn), `Spawning 60 wandering bots`, 69 online
    at 2:21, then `100 online (7 queued)` at 4:21 - the target had risen above 100 and bot sessions queue (see
    "Open issues"). 2 spawns failed with `Map (1, 0) not found` (fixed), 49 deaths, many in neutral towns (fixed
    with the guard check), bots teleported away from `DarnassusEntrance` (fixed in `SelectSpawnNode`).
  - server run 2: a map-exists pre-check in `AddWanderBot` stopped every spawn (the continents only exist once a
    player is created there) - replaced by the on-demand creation in `SpawnNewPlayer`.
  - server run 3 (`PlayerLimit = 0`): `.wanderbot info` at 1:31: 93 online, target 93 (refresh at 1:00), Alliance
    44 / Horde 49, levels 22 / 22 / 14 / 9 / 14 / 12 per band of 10. `.wanderbot add 30 2`: "2 wandering bots
    added", target 95. At 4:56: 77 online, target 77, max online 95. `removeall` then `info`: 0 online, 4
    loading (the next refresh had set 116). 100 spawns, 249 node arrivals, 0 stuck teleports, 0 spawn-node
    teleports, 40 deaths spread over 14 zones (about 5 per bot per hour), no crash or assert, clean shutdown,
    exit 0.
  - not measured: the world update time with 0 / 60 / 120 bots (step 7); `.wanderbot remove` and `nodes`
    (need an in-game GM: they use the selection / position); no in-game look (needs a 1.12 client and the user).

## Open issues

- `Player::AddCooldown` errors: about 3 per minute with 120 bots, from the shared trinket / wand code (see
  "Decisions"). A fix belongs in `CombatBotBaseAI` and would touch party / battle bots: the user's call.
- Deaths: 8.8 per bot per hour with 120 bots after run 4's fixes, mostly monster packs (78 of 141) and PvP between
  wandering bots (35). Tuning left open: a smaller `WB_AGGRO_RANGE`, or pack avoidance (count the monsters near
  a target before pulling it). "No enemy" deaths (9) are one-shots within one AI second or falls.
- `CastSpell: unknown spell id 0 by caster: Player ...`: 3 times in run 4c, all from two level 4 human paladins
  that spawned in Stormwind / Ironforge; not in the four other runs. The level 4 paladins learn no class-quest
  spell (the first paladin one is level 12), so it is not run 4's change; the source in the bot code was not
  found (the `CastSpell(..., uint32)` callers in `PlayerBots` all check their id). Logged and ignored by the core.
- `JUMP_BEGIN` / `JUMP_END` and `MILL_RADIUS` flags are loaded but not used by the movement yet.
- 18 nodes get their area from the terrain at load: 4 use area ids 1.12 lacks (215 Plaguewood Tower 4067,
  233 Mudsprocket 4010, 1905 / 1907 Dustwallow 4046 / 4049), 14 sit at dungeon entrances whose vanilla area is
  filed under the instance map. Both are handled; Mudsprocket (a TBC town)
  lost its spawn flag in run 4.
- Dropped at conversion: 92 "Grim Batol", 451-454 "DragonmawGates*" (level 61-70 in NPCBots, elite dragons in
  1.12). 409 "Seradane" was 60-80, clamped to 60-60.
- `RandomBot.Enable = 1` (Min 50 / Max 150) in both `my_confs` copies is unrelated to wandering bots and left
  as it is.
- Uncommitted `src/shared/Progression.h` (the user's client build tweak) is left alone.
