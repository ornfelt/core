# Wandering bots - status
Last run: 2026-10-19 (run 2) - step 3 (spawning, min / max loop), step 4 (wandering, death, stuck handling), most of
step 5 (monster / PvP targeting, leash, guard check) and step 6 (`.wanderbot` commands); verified with 3 server runs.
Next: answer the `PlayerLimit` question (see "Open issues"), then step 5's rest (pets checked in a run, deaths per
hour tuned) and step 2c's open part; then step 7.

## Steps

- [x] 0 Setup (2026-10-19): `wander-bots` branch from `development`; inventory of `src/game/PlayerBots`,
      NPCBots' wander code / node SQL and cmangos-tbc's random bot logic (summary under "Design" and
      "Decisions"); this ledger.
- [x] 1 Config (2026-10-19): `CONFIG_BOOL_WANDER_BOT_ENABLE` and `CONFIG_UINT32_WANDER_BOT_*` in
      `src/game/World.h`, loaded in `World::LoadConfigSettings` (`src/game/World.cpp`, max below min raised
      with an error); documented in the "PLAYER BOTS" section of `src/mangosd/mangosd.conf.dist.in`,
      `my_confs/mangosd.conf`, `my_confs/linux/mangosd.conf`, and the run conf `bin/Release/mangosd.conf`
      (gitignored).
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
- [x] 4 Wandering (2026-10-19): `WanderBotMovement.cpp` - walk along links (`MovePoint` with pathfinding,
      straight between two `IGNORES_PATHING` nodes, random offset within `proximity`), level / faction-fitting
      next node, the nearest fitting node within 800 yd at a dead end, else a teleport to a fitting spawn node;
      pauses (`min/max_wait_time`); mount for legs over 60 yd (`BattleBotAI::UseMount`, level 40+); rest
      below 85 % health / 60 % mana until full (`BattleBotAI::DrinkAndEat`); stuck: < 3 yd in 5 s three times ->
      turn back to the previous node once, then a teleport onto the node. Death: release after 5 s, revive at the
      graveyard 10 s later at 50 %, go on from the nearest fitting node.
- [~] 5 Fighting (2026-10-19): `WanderBotTargeting.cpp` - attackers first (nearest), then the pet's attacker, then
      (rested only) hostile monsters within 25 yd and LOS (no critters, civilians, guards, world bosses, no
      monster fighting someone else; normal ones up to 3 levels above / 4 below, elites only 3+ levels below
      the bot, down to 8 below) and, on PvP realms, enemy-faction players / bots / their pets 12 above / 9 below
      with no guard within 50 yd of them; leash 60 yd from where the fight started unless the target attacks the
      bot. Rotations, pets and trinkets come from `BattleBotAI`. Left: a run that checks hunter / warlock pets
      fight, the deaths per hour (about 5 per bot per hour in run 3) looked at per cause.
- [x] 6 Commands (2026-10-19): `.wanderbot add [level] [count]` (raises the current target by the bots added),
      `remove` (selected bot, lowers the target), `removeall` (target 0 until the next refresh), `info` (online /
      loading / target, per faction, dead, per level band, nodes, the selected bot's node), `nodes [range]`.
      `src/game/PlayerBots/WanderBotCommands.cpp`, the table in `src/game/Chat/Chat.cpp` with the help text in
      code (as the other bot tables; the world `command` table holds only 2 rows, so no migration).
- [ ] 7 Polish and load: load numbers for 0 / 60 / 120 bots, the `AddCooldown` errors, deaths per hour, warnings,
      final review against "Requirements".

## Design

### Files
- `src/game/PlayerBots/WanderNodeMgr.h/.cpp`: `WanderNode` (id, name, map, zone, area, min / max level,
  min / max wait, proximity, flags, position, links as node pointers; `FitsLevel`, `IsWalkableFor`,
  `IsStartPointFor`, `HasLinkFor`), the loader, validation and the graph queries. Singleton `sWanderNodeMgr`.
- `src/game/PlayerBots/WanderBotAI.h`: the class and every tuning constant (`WB_*`).
- `src/game/PlayerBots/WanderBotAI.cpp`: init, `UpdateAI` (the `BattleBotAI` flow minus the battleground parts),
  death / revive, rest check.
- `src/game/PlayerBots/WanderBotMovement.cpp`: node walking, pauses, stuck handling, spawn-node teleport,
  `MovementInform`.
- `src/game/PlayerBots/WanderBotTargeting.cpp`: target scan, PvP rules, guard check, leash.
- `src/game/PlayerBots/WanderBotCommands.cpp`: the `.wanderbot` handlers.
- `src/game/PlayerBots/PlayerBotMgr.h/.cpp`: `UpdateWanderBots`, `AddWanderBot(s)`, `DeleteWanderBot(s)`,
  `GetWanderBotStats`, `IsDebugEnabled`; `DeleteBattleBots` and `.battlebot remove` skip wandering bots.
- `src/game/PlayerBots/CombatBotBaseAI.h/.cpp`: `virtual GetRandomGearLevelDifference()` (party bot config by
  default, so party / battle bots are unchanged) and `LearnRandomTalentsAndAllSpells()` (the existing fallback
  branch of `LearnPremadeSpecForClass`, moved out so `WanderBotAI` can call it).
- `src/game/PlayerBots/BattleBotAI.h`: `UpdateAI` and `MovementInform` are `override` instead of `final`.
- `src/game/PlayerBots/PlayerBotAI.cpp`: `SpawnNewPlayer` creates a missing continent map.
- `sql/migrations/20261019184711_world.sql`: `wander_bot_nodes`.
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
- **Staggered spawning**: 4 bots per manager update (`PlayerBot.UpdateMs` = 1000 here: 60 bots in about 15 s).
- **`.wanderbot add` / `remove` move the current target** so the loop does not undo them at once; the next
  refresh picks a new target as usual. `removeall` sets the target to 0 until the next refresh.
- **Hostile towns**: enemy capitals are never spawn / walk targets (node flags, capital check at load).
- **Node validation at load is terrain only** (see run 1 of this ledger); reachability is handled at runtime by
  the stuck check (3 runs: 0 stuck teleports so far).
- **Run setup on this machine** (Windows): build with `cmake --build build --config Release`; the binaries and
  the run confs are in `bin/Release` (gitignored; the counterpart of `~/vmangos`). DBs on the local MySQL 8.0
  (connection lines in `bin/Release/mangosd.conf`). The realm is `GameType = 1` (PvP). Test runs use a copy of
  the run conf in the scratchpad with `PlayerBot.Debug = 1` (and in run 3 `PlayerLimit = 0`), mangosd's console
  fed through stdin.

## Verification

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

- **Question for the user: `PlayerLimit`.** Bot sessions count towards `PlayerLimit` (100 in the run conf), so
  with up to 120 wandering bots the bots queue (seen in run 1) and real players would be queued behind them.
  Options: (a) wandering bot sessions skip the queue and do not count towards the limit (a small change in
  `World::AddSession_` / the queue count, wandering bots only) - recommended; (b) leave the core alone and
  document that `PlayerLimit` must be above `WanderBot.MaxBots` plus the real players; (c) cap the target at
  `PlayerLimit` minus a reserve.
- `Player::AddCooldown> Spell(... ) try to add and already existing cooldown?` errors (5019 wand shoot,
  23506 / 24347 trinkets) in run 3: from the reused `BattleBotAI` rotations / `UseTrinketEffects`, 26 in 5 min;
  check in step 7 whether battle bots show them too.
- Deaths: about 5 per bot per hour in run 3, spread over many zones; look at the causes (monsters pulled in
  packs, PvP, elites aggroing) in step 5 / 7.
- `JUMP_BEGIN` / `JUMP_END` and `MILL_RADIUS` flags are loaded but not used by the movement yet.
- 18 nodes get their area from the terrain at load: 4 use area ids 1.12 lacks (215 Plaguewood Tower 4067,
  233 Mudsprocket 4010, 1905 / 1907 Dustwallow 4046 / 4049), 14 sit at dungeon entrances whose vanilla area is
  filed under the instance map. Both are handled; Mudsprocket (a TBC town) is a spawn node in an empty spot of
  1.12 Dustwallow - harmless, but could lose its spawn flag in 2c.
- Dropped at conversion: 92 "Grim Batol", 451-454 "DragonmawGates*" (level 61-70 in NPCBots, elite dragons in
  1.12). 409 "Seradane" was 60-80, clamped to 60-60.
- Stormwind Harbor / WotLK zeppelin and boat docks, and NPCBots' WotLK level bands: still to check in 2c.
- `RandomBot.Enable = 1` (Min 50 / Max 150) in both `my_confs` copies is unrelated to wandering bots and left
  as it is.
- Uncommitted `src/shared/Progression.h` (the user's client build tweak) is left alone.
