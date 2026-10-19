---
name: vmangos-wander-bots
description: Add wandering bots to vmangos (Code2/C++/core) - random-race / class / level (1-60) / name / talents / gear player bots that spawn at random level-appropriate places in the world, walk between wander nodes (imported from AzerothCore-NPCBots' wander-node graph), fight nearby monsters and, on a PvP realm, opposite-faction players and wandering bots. A WanderBot.* conf block (on by default, min 60 / max 120, can be turned off), reusing the PartyBot / BattleBot code (CombatBotBaseAI, PlayerBotMgr). Repeatable - every run picks up where the last one stopped, aims for ~1500-3000 lines, ledger in WANDER_BOTS_STATUS.md on the wander-bots branch, each run commits.
disable-model-invocation: true
argument-hint: "[empty or continue = next step; status; step <n>; review]"
---

# vmangos wandering bots

## Requirements (as stated by the user)

The user's words, spelling fixed and slightly expanded. When a later section seems to say
something different, this section wins. Ask the user about the conflict.

> Please see this repo / dir (vmangos): `{code_root_dir}/Code2/C++/core`
>
> Add a new feature to it: a new setting in the conf files (similar to the party bots and battle
> bots) - **wandering bots**. I want to be able to set a minimum and a maximum amount for them. The
> minimum should be 60 and the maximum 120 by default, and it should be possible to turn them off
> via the conf as well.
>
> The wandering bots should simply wander around in the world (anywhere) between specific / random
> wander nodes (coordinates), sort of like how the playerbots / NPCBots work in AzerothCore and
> cmangos-tbc. If needed, see the reference code in
> `{code_root_dir}/Code2/C++/AzerothCore-wotlk-with-NPCBots` and
> `{code_root_dir}/Code2/C++/mangos-tbc`, where `code_root_dir` is an environment variable.
>
> The wandering bots should wander around but also attack nearby monsters, and players and other
> wandering bots of the opposite faction (if it is a PvP server).
>
> Make the wandering bots spawn at different (random) locations, at random levels (1-60), with some
> random gear suitable for their level, and with random names, random races / classes and random
> talents / specs. Hopefully the existing PartyBot / BattleBot logic can be reused here.
>
> Also make sure that the bots wander in zones that are appropriate for their level, so they don't
> die constantly.
>
> For now I don't want a leveling system: the bots can be hard-coded to their levels and never
> level up.

## Answers given when the skill was written (2026-10-19)

| Question | Answer |
| --- | --- |
| "with some random ..." (cut off in the request) | Random **names**, random **race / class** and random **talents / spec** - all three. |
| Where the wander nodes come from | **Imported from AzerothCore-NPCBots**' wander-node SQL (maps 0 / 1, links, level ranges), converted into a new vmangos world table plus a migration, the zone levels checked against vanilla. |
| Default and persistence | **On by default** (`WanderBot.Enable = 1`, min 60, max 120). The bots are **temporary**, as BattleBots are: created fresh at startup / refresh, never saved to the characters DB. |
| Git and server | Work on a **`wander-bots` branch** made from `development`. A run **may** build, install and restart the local servers to test. |

## Defaults taken when the skill was written

Not asked. Change one only if the user says so. Decisions taken during runs go in the
"Decisions" section of `WANDER_BOTS_STATUS.md`, never in this file.

| Topic | Default |
| --- | --- |
| Separate from RandomBot | `RandomBot.*` stays as it is (it logs in characters from the `playerbot` table, without AI). Wandering bots are a new, independent `WanderBot.*` system next to it. |
| Conf keys | `WanderBot.Enable = 1`, `WanderBot.MinBots = 60`, `WanderBot.MaxBots = 120`, `WanderBot.Refresh = 60000` (ms, as `RandomBot.Refresh`), `WanderBot.AutoEquip = 1` (the same 0 / 1 / 2 values as `PartyBot.AutoEquip`), `WanderBot.RandomGearLevelDifference = 10`. Enable = 0 turns them off: no bots spawned and the wander nodes not loaded. More keys only when a step needs one, each documented in the conf. |
| How min / max works | As RandomBot does: at startup spawn `MinBots`. Every `Refresh`, pick a target in [min, max] and add or remove bots towards it, a few per world tick (no login spike of 120 at once). A max below the min is fixed up with a warning, as `PlayerBotMgr::Load` does. |
| Faction mix | About half Alliance, half Horde. Classes valid for the faction in vanilla (Paladin Alliance only, Shaman Horde only), race through `SelectRandomRaceForClass`. Level uniform in 1-60. |
| PvP rules | On a PvP realm (`sWorld.IsPvPRealm()`), bots are PvP flagged and attack opposite-faction players and wandering bots in range. On a normal realm they only fight back when attacked (or when the target is PvP flagged and attacks them). Monsters: hostile ones in aggro range whose level is not far above the bot's (no elites well above it). |
| No leveling | A wandering bot never gains XP: its level is fixed at spawn. |
| Death | The bot comes back after a short delay, at the nearest graveyard or at its corpse, like a player who releases and runs back, then goes on wandering. Not despawned and replaced. |
| Commands | GM commands `.wanderbot add [level] [count]`, `.wanderbot remove` (the target), `.wanderbot removeall`, `.wanderbot info` (online / target / per-faction / per-level counts, nodes loaded), `.wanderbot nodes` (the nodes near the GM), wired like the `.battlebot` commands, help text included. |
| Build switch | None: the conf setting is the switch. If one is ever needed, follow `preprocessor-directives` (a CMake option and a `#define`). |
| Commits | One commit per run on `wander-bots`, no push, only the files this run changed, staged by name. **No `Co-Authored-By` line or any other trailer.** |

## Trees

| What | Path | Access |
| --- | --- | --- |
| vmangos (the target) | `$code_root_dir/Code2/C++/core` (remote `ornfelt/core`, branch `wander-bots`) | write |
| Bot code to reuse | `src/game/PlayerBots/` - `PlayerBotMgr.*` (spawning, config, `AddBattleBot`, `AddRandomBot`), `PlayerBotAI.*` (`SpawnNewPlayer`: name, appearance, session), `CombatBotBaseAI.*` (spells and rotations per class, `AutoEquipGear`, `LearnRandomTalents`, `LearnPremadeSpecForClass`, reagents, ammo), `BattleBotAI.*` (an AI that moves on its own between waypoints and fights players), `PartyBotAI.*` | write |
| Config wiring | `src/game/World.h` / `World.cpp` (`CONFIG_*` enums, `setConfigMinMax`), `src/mangosd/mangosd.conf.dist.in` ("PLAYER BOTS" section), `src/game/Chat/Chat.cpp` (command table) | write |
| The user's tracked confs | `my_confs/mangosd.conf`, `my_confs/linux/mangosd.conf` | write: add the `WanderBot.*` block only |
| World DB migrations | `sql/migrations/` (`YYYYmmddHHMMSS_world.sql`, first line `INSERT INTO migrations VALUES (...)`, see its README; `merge.sh` combines them) | write |
| Install / run | `~/vmangos` (`bin/mangosd`, `bin/realmd`, `etc/mangosd.conf`), from `build/` (`CMAKE_INSTALL_PREFIX`); the DB connection lines are in `~/vmangos/etc/mangosd.conf` | build, install, run; add the `WanderBot.*` block to the installed conf |
| NPCBots wander nodes | `$code_root_dir/Code2/C++/AzerothCore-wotlk-with-NPCBots/data/sql/custom/db_world/*wander_nodes*.sql` (a create, then updates; apply them **in file-name order** to get the final state) | read |
| NPCBots wander code | `.../AzerothCore-wotlk-with-NPCBots/src/server/game/AI/NpcBots/` - `botwanderful.h/.cpp` (`WanderNode`, `BotWPFlags`, links, the level / faction checks), `botdatamgr.cpp` (making the wandering bots, spawn node choice), `botmgr.cpp`, `bot_ai.cpp` (wander movement, next-node choice, stuck handling), `botconfig.cpp` | read |
| cmangos-tbc playerbots | `$code_root_dir/Code2/C++/mangos-tbc/src/modules/PlayerBots/playerbot/` - `RandomPlayerbotMgr.cpp` (random levels, level-appropriate teleport places, the add / remove loop), `PlayerbotFactory.cpp` (gear, talents for a level), `TravelMgr.*` / `TravelNode.*` (travel between points of interest) | read |
| Generic skills (not loaded from this repo) | `$code_root_dir/Code2/General/gfx/.claude/skills/<name>/SKILL.md` | read |

Address every tree through `$code_root_dir` (`$env:code_root_dir` in PowerShell), never a literal
home directory. `~/vmangos` is the install prefix on this Linux machine; read `CMAKE_INSTALL_PREFIX`
from `build/CMakeCache.txt` on another one.

## Scope guard

- Write only inside `Code2/C++/core`, plus the `WanderBot.*` block in the installed
  `~/vmangos/etc/mangosd.conf`. Never edit the reference trees.
- Change only what the feature needs (`minimal-code-changes`): PartyBots, BattleBots and RandomBots
  keep working as before. A shared helper moved into `CombatBotBaseAI` / `PlayerBotMgr` for reuse
  keeps its old callers' behavior.
- Uncommitted changes in files this skill does not touch (for example the user's
  `src/shared/Progression.h` client build tweak) are left alone: never staged, reverted or
  committed. Mention them in the summary.
- Never `git add -A` or `git add .`. Stage by name.
- Never commit to `development`, never push, never open a pull request. Merging is the user's call.

## Code conventions

These generic skills apply (read them from the gfx tree, see "Trees"): `no-magic-values`
(radii, delays, level margins, node flags, counts become named constants or enums: no repeated
literals), `minimal-code-changes`, and `confirmation-prompt` if a script asks yes / no. A helper
script (the node converter) also follows `script-cli-conventions` and `script-colored-prints`.

The C++ reads like the vmangos code around it: 4 spaces, braces on their own lines, `m_` members,
`sLog.Out(LOG_BASIC, ...)`, `sWorld.getConfig(CONFIG_...)`, the same comment density. New files
`src/game/PlayerBots/WanderBotAI.h/.cpp` and `WanderNodeMgr.h/.cpp` (or as the inventory finds
best), added to the PlayerBots sources in CMake. Keep a file at about 1500 lines at most. Split
by responsibility (nodes, movement, combat targeting) rather than letting `WanderBotAI.cpp` grow
like `BattleBotAI.cpp`.

## Design

Read the reference code, never only this summary. This is the starting shape. The setup run
refines it after the inventory and records it in the ledger.

- **Wander nodes.** A world table (e.g. `wander_bot_nodes`: id, name, map, zone, area, min / max
  level, flags, x / y / z / o, links), made by one migration from the NPCBots data. A converter
  script (`contrib/wander_nodes/convert_npcbots_wander_nodes.py`) applies the NPCBots files in order
  and writes the migration. It keeps maps 0 and 1 only, keeps the flags that make sense outside
  battlegrounds (spawn, Alliance / Horde only, backtrack, ignore faction / pathing, jump, not a
  start point), clamps levels to 1-60, and drops links to removed nodes. Check each zone's levels
  against vanilla, and drop nodes in places 1.12 lacks. The generated data does not count towards
  the run's line budget. `WanderNodeMgr` loads the table at startup (only with `WanderBot.Enable`)
  into a graph. It checks each node against the map (ground height, with mmaps a reachable path to
  its links), logs and skips bad ones, and answers "a spawn node for team T and level L" and "the
  next node from N for team T and level L".
- **Spawning.** `PlayerBotMgr` gets the `WanderBot.*` config and the min / max loop. A wandering bot
  is a temporary bot like `AddBattleBot`'s. It gets a random class and team, race through
  `SelectRandomRaceForClass`, a random level, and a name from `GenerateFreePlayerName` (via
  `SpawnNewPlayer`). It is created at a random spawn node that fits its team and level, then gets
  `AutoEquipGear(WanderBot.AutoEquip)`, random talents (`LearnRandomTalents`, or a premade spec
  when the class needs one to work), spells, reagents and ammo. Hunter and warlock pets work as
  they do for BattleBots.
- **Level-appropriate zones.** A bot only spawns at, and only walks to, nodes whose [min, max]
  level fits its level, with a small margin. A bot that ends up out of range (no fitting link)
  turns back, or is teleported to a fitting spawn node. Never into the enemy faction's towns
  (faction-only flags).
- **Wandering.** `WanderBotAI` (derived from `CombatBotBaseAI`, as BattleBotAI is) walks along
  links with the motion master and pathfinding. It pauses a little at nodes, rests (eat / drink /
  bandage as the existing code does) when low, and handles stuck movement (no progress for a while
  means the next node, or a teleport to a node). It keeps its AI update cheap (`PlayerBot.UpdateMs`).
- **Fighting.** It scans for targets by the PvP rules above, at a modest radius, and uses
  `CombatBotBaseAI`'s class rotations (as BattleBotAI does against players). It does not chase far
  from its route: it leaves combat and returns to its node path past a leash distance. It gains no
  XP (a check in the XP path for wandering bots, or a level reset: pick the clean one).
- **Load.** 120 bots spread over the world keep grids active. Measure the world update time with
  0, 60 and 120 bots and record it. Keep it reasonable: no per-bot work that scales with the number
  of bots.

## Steps

The setup run writes these into `WANDER_BOTS_STATUS.md` as a checklist, refined after the
inventory. Later runs tick them off and split them as needed. Each run lands code: setup does
step 0 and as much of step 1 as fits.

0. **Setup.** The `wander-bots` branch. An inventory of `src/game/PlayerBots` (what the
   PartyBot / BattleBot spawn, gear, talent, movement and combat code does, what can be reused
   as it is), of NPCBots' wander code and node SQL, and of cmangos-tbc's random bot level /
   location logic. The design and the steps in the ledger.
1. **Config.** The `WanderBot.*` keys in `World.h` / `World.cpp`, `mangosd.conf.dist.in` (documented
   in the "PLAYER BOTS" section in the style of the others), both `my_confs` copies and the
   installed conf.
2. **Wander nodes.** The converter, the world migration, the table applied to the local world DB,
   and `WanderNodeMgr` (load, validate, the queries), with a load summary in the log (nodes per
   map, per level band, skipped and why).
3. **Spawning.** The min / max loop in `PlayerBotMgr`, random race / class / level / name /
   appearance / talents / gear, the spawn at a fitting node, no XP, faction mix, staggered logins.
4. **Wandering.** `WanderBotAI` movement along the graph, level-fitting next node, pauses, resting,
   stuck handling, death and coming back.
5. **Fighting.** Monster targeting, defending, the PvP rules (players and opposite-faction
   wandering bots), the leash, pets.
6. **Commands.** `.wanderbot ...` and their help text (the command table, and the world DB
   `command` table through a migration if vmangos keeps help there).
7. **Polish and load.** Mounts from level 40 when the bot can ride (if cheap), the load numbers
   with 0 / 60 / 120 bots, warnings cleaned, a final review against "Requirements".

## Modes

1. **Continue** - no argument or `continue` (the default): the next unchecked step (or part of it).
2. **Status** - `status`: print the ledger cross-checked against the branch. Change nothing.
3. **Step** - `step <n>`: do that step next. Warn if a step it needs is not done.
4. **Review** - `review`: compare the code with "Requirements" and the design, report the gaps.
   Change nothing unless asked.

## Every run

1. **Orient.**

   ```bash
   C="$code_root_dir/Code2/C++/core"
   git -C "$C" status --short
   git -C "$C" branch --show-current
   git -C "$C" log --oneline -10
   sed -n '1,120p' "$C/WANDER_BOTS_STATUS.md" 2>/dev/null
   ```

   The first run: `git -C "$C" switch -c wander-bots development` (local uncommitted changes come
   along and stay unstaged). Every later run: `git -C "$C" switch wander-bots` before touching a
   file. The tree is authoritative: a step marked done whose code is missing or does not build is
   not done. Uncommitted changes in files this skill owns mean a run stopped early: read them, build
   on them, and include them in this run's commit, saying so.
2. **Report the status** before changing anything: the steps done, the one in progress, what the
   last run left open, and what this run will do. With `status`, stop here.
3. **Do the work.** Aim for roughly **1500-3000 lines of code change** per run (a guideline, not a
   strict rule; generated SQL data does not count). Stop early at a good point rather than in a
   broken state. Never end a run on a build that does not compile.
4. **Verify** (see "Verification").
5. **Record and commit** (see "Hand-off").

## Verification

- **Build every run:** `cmake --build "$C/build" -j"$(nproc)"`, with no new warnings in the files
  this run touched.
- **Runtime check** once the run's code can run (from step 1 on):
  1. Install: `cmake --install "$C/build"` (to `~/vmangos`), and apply new migrations to the local
     world DB (connection from `~/vmangos/etc/mangosd.conf`, through the `mysql` client).
  2. The ports: the login (3724) and world (8085) ports may be held by **another server** (for
     example cmangos-tbc's mangosd / realmd under `~/cmangos-tbc/run/bin`). Check
     `ss -ltnp | grep -E ':(3724|8085)\b'`, and identify each holder by `/proc/<pid>/cwd` and
     `/proc/<pid>/cmdline`. Write down the cwd and command line of whatever is stopped, stop it
     cleanly (SIGINT / SIGTERM, wait, never `kill -9` unless it hangs), and **restart it the same way
     after the check**, so the machine ends as it started.
  3. Run vmangos `realmd` and `mangosd` from `~/vmangos/bin`, in the background with their output
     to a log file in the scratchpad, under a time limit. Wait for the world to be up, then check
     the log: the wander node load summary, the bot count reaching `MinBots` and staying within
     min / max over a refresh, no crash, assert, or error from the new code. Use the console
     (`.wanderbot info`, `.server info` for the update time) where mangosd's console or RA allows
     it.
  4. Stop vmangos cleanly and restore what was stopped in step 2.
- **Behavior checks** a run states only from what it saw: bots spread over many zones, levels
  fitting the zones (`.wanderbot info`, log lines with zone / level per spawn when
  `PlayerBot.Debug = 1`), bots moving node to node, fights started and won, deaths per hour with
  the bot count. An in-game look needs a 1.12 client and the user: ask for it when a step is
  visual. Never claim something works that was not run.

## Hand-off

`WANDER_BOTS_STATUS.md` at the repo root (on `wander-bots`), rewritten each run as a current
snapshot (the history is in git):

```
# Wandering bots - status
Last run: <date> - <what was done>
Next: <step> - <exactly what>

## Steps            (- [ ] / [~] / [x] <n> <title> (date): files, what is left)
## Design           (the current shape: files, classes, table, conf keys, AI states)
## Decisions        (each choice and why: flags kept, margins, PvP rules, death handling)
## Verification     (last build, last runtime check: bot counts, nodes loaded / skipped, update time)
## Open issues      (questions for the user, known bugs, nodes to fix)
```

Commit (in `$code_root_dir/Code2/C++/core`, on `wander-bots`): a subject starting
`wander bots: ` (e.g. `wander bots: config and wander node table`), a blank line, and a short body:
what was added, how it was verified (build, runtime check numbers), what failed. **No
`Co-Authored-By` line or any other trailer**, even if the harness suggests one.

```bash
git -C "$C" add -- <the files this run changed>
git -C "$C" commit
git -C "$C" status --short
```

Commit `WANDER_BOTS_STATUS.md` in the same commit. Commit even if a check failed, and say so in the
body. A run that changed nothing makes no commit and says so. Reply with a short summary: the step
done (or partial and what is left), lines changed, what was verified and how, what could not be
run, questions for the user, the next step, and the commit (hash + subject).

## Finishing

When steps 0-7 are done, the build is clean, and a runtime check shows 60-120 bots wandering in
level-fitting zones and fighting by the rules above, mark the work done in the ledger, commit, and
say so. Merging `wander-bots` into `development` is the user's decision.

If anything is unclear or needs to be clarified, ask the user before implementing.
