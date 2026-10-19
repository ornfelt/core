#!/usr/bin/env python3
"""
Convert AzerothCore-NPCBots' wander nodes into a vmangos world migration (`wander_bot_nodes`).

The NPCBots files (`*wander_nodes*.sql`) are replayed in memory in file-name order (create, inserts,
deletes, updates, the added columns), so the result is the final state of their table. Then only the
continents of 1.12 (maps 0 / 1) are kept, BG-only flags are dropped, levels are clamped to 1-60 and
links to removed nodes are dropped. No database is needed.

Usage examples:
default source tree ($code_root_dir/Code2/C++/AzerothCore-wotlk-with-NPCBots), new migration in sql/migrations:
python contrib/wander_nodes/convert_npcbots_wander_nodes.py

explicit NPCBots sql dir (positional or --src):
python contrib/wander_nodes/convert_npcbots_wander_nodes.py C:/src/npcbots/data/sql/custom/db_world

dry run, print the summary only:
python contrib/wander_nodes/convert_npcbots_wander_nodes.py -n -v

regenerate an existing migration (same timestamp / file name):
python contrib/wander_nodes/convert_npcbots_wander_nodes.py --timestamp 20261019190000

print an equivalent mysql replay command instead of converting:
python contrib/wander_nodes/convert_npcbots_wander_nodes.py --show-cmd --cmd-shell bash
"""

import argparse
import datetime
import fnmatch
import os
import re
import sys

# ── ANSI colors ─────────────────────────────────────────────────────────────
RED = "\033[31m"
GREEN = "\033[32m"
YELLOW = "\033[33m"
CYAN = "\033[36m"
MAGENTA = "\033[35m"
DIM = "\033[2m"
RESET = "\033[0m"


def _enable_windows_ansi() -> bool:
    """Turn on virtual terminal processing so ANSI codes work in cmd/PowerShell."""
    if os.name != "nt":
        return True
    try:
        import ctypes

        kernel32 = ctypes.windll.kernel32
        handle = kernel32.GetStdHandle(-11)  # STD_OUTPUT_HANDLE
        mode = ctypes.c_uint32()
        if not kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            return False
        return bool(kernel32.SetConsoleMode(handle, mode.value | 0x4))
    except Exception:
        return False


def write_ok(msg):
    print(f"{GREEN}{msg}{RESET}")


def write_err(msg):
    print(f"{RED}{msg}{RESET}", file=sys.stderr)


def write_warn(msg):
    print(f"{YELLOW}{msg}{RESET}")


def write_info(msg):
    print(f"{CYAN}{msg}{RESET}")


def write_info_alt(msg):
    print(f"{MAGENTA}{msg}{RESET}")


# ── Constants ───────────────────────────────────────────────────────────────
SOURCE_TABLE = "creature_template_npcbot_wander_nodes"
TARGET_TABLE = "wander_bot_nodes"
DEFAULT_FILE_PATTERN = "*wander_nodes*.sql"
DEFAULT_NPCBOTS_SUBDIR = os.path.join("Code2", "C++", "AzerothCore-wotlk-with-NPCBots", "data", "sql", "custom", "db_world")
REPO_ROOT = os.path.normpath(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", ".."))
MIGRATIONS_DIR = os.path.join(REPO_ROOT, "sql", "migrations")
TIMESTAMP_FORMAT = "%Y%m%d%H%M%S"

DEFAULT_MAPS = (0, 1)  # Eastern Kingdoms, Kalimdor
MIN_LEVEL = 1
MAX_LEVEL = 60

# NPCBots BotWPFlags kept for the open world (bit values unchanged). The BG flags 0x40-0x8000 are dropped.
WANDER_NODE_FLAG_SPAWN = 0x00001
WANDER_NODE_FLAG_ALLIANCE_ONLY = 0x00002
WANDER_NODE_FLAG_HORDE_ONLY = 0x00004
WANDER_NODE_FLAG_CAN_BACKTRACK_FROM = 0x00008
WANDER_NODE_FLAG_IGNORES_FACTION = 0x00010
WANDER_NODE_FLAG_IGNORES_PATHING = 0x00020
WANDER_NODE_FLAG_JUMP_BEGIN = 0x10000
WANDER_NODE_FLAG_JUMP_END = 0x20000
WANDER_NODE_FLAG_MILL_RADIUS = 0x40000
WANDER_NODE_FLAG_NOT_A_START_POINT = 0x80000
KEPT_FLAGS_MASK = (WANDER_NODE_FLAG_SPAWN | WANDER_NODE_FLAG_ALLIANCE_ONLY | WANDER_NODE_FLAG_HORDE_ONLY
                   | WANDER_NODE_FLAG_CAN_BACKTRACK_FROM | WANDER_NODE_FLAG_IGNORES_FACTION
                   | WANDER_NODE_FLAG_IGNORES_PATHING | WANDER_NODE_FLAG_JUMP_BEGIN | WANDER_NODE_FLAG_JUMP_END
                   | WANDER_NODE_FLAG_MILL_RADIUS | WANDER_NODE_FLAG_NOT_A_START_POINT)

# Columns of the NPCBots table after all its files (the last three come from the 2026_01_07 ALTER).
SOURCE_COLUMNS = ["id", "name", "mapid", "zoneid", "areaid", "minlevel", "maxlevel", "flags",
                  "x", "y", "z", "o", "links"]
SOURCE_DEFAULTS = {"name": "RENAME_ME", "mapid": "0", "zoneid": "0", "areaid": "0", "minlevel": "0",
                   "maxlevel": "0", "flags": "0", "x": "0", "y": "0", "z": "0", "o": "0", "links": None}

LEVEL_BAND_SIZE = 10  # summary only


class SqlError(Exception):
    pass


# ── SQL lexing ──────────────────────────────────────────────────────────────
def split_statements(text):
    """Split a MySQL script into statements: comments removed, strings kept, DELIMITER blocks skipped."""
    statements = []
    current = []
    i = 0
    n = len(text)
    at_line_start = True
    while i < n:
        c = text[i]
        if at_line_start:
            line_end = text.find("\n", i)
            line_end = n if line_end < 0 else line_end
            line = text[i:line_end].strip()
            if re.match(r"(?i)^DELIMITER\s+(?!;)", line):
                # A stored-procedure definition up to "DELIMITER ;": the CALLs that use it are handled instead.
                end = re.compile(r"(?im)^\s*DELIMITER\s+;\s*$").search(text, line_end)
                if not end:
                    raise SqlError("DELIMITER block without 'DELIMITER ;'")
                i = end.end()
                continue
        at_line_start = False
        if c in ("'", '"', "`"):
            j = i + 1
            while j < n:
                if text[j] == "\\" and c != "`":
                    j += 2
                    continue
                if text[j] == c:
                    if j + 1 < n and text[j + 1] == c:
                        j += 2
                        continue
                    break
                j += 1
            current.append(text[i:j + 1])
            i = j + 1
            continue
        if c == "#" or (c == "-" and text.startswith("--", i) and (i + 2 >= n or text[i + 2] in " \t\r\n")):
            line_end = text.find("\n", i)
            i = n if line_end < 0 else line_end
            continue
        if text.startswith("/*", i):
            end = text.find("*/", i + 2)
            if end < 0:
                raise SqlError("unterminated /* comment")
            i = end + 2
            continue
        if c == ";":
            stmt = "".join(current).strip()
            if stmt:
                statements.append(stmt)
            current = []
            i += 1
            continue
        if c == "\n":
            at_line_start = True
        current.append(c)
        i += 1
    stmt = "".join(current).strip()
    if stmt:
        statements.append(stmt)
    return statements


def parse_values(text, pos):
    """Parse '(v, v, ...)' starting at text[pos] == '('. Returns (list of raw values, next pos)."""
    if text[pos] != "(":
        raise SqlError(f"expected '(' at {pos}")
    values = []
    i = pos + 1
    n = len(text)
    while True:
        while i < n and text[i] in " \t\r\n":
            i += 1
        if text[i] == "'":
            j = i + 1
            chars = []
            while True:
                if text[j] == "\\":
                    chars.append(text[j + 1])
                    j += 2
                    continue
                if text[j] == "'":
                    if j + 1 < n and text[j + 1] == "'":
                        chars.append("'")
                        j += 2
                        continue
                    break
                chars.append(text[j])
                j += 1
            values.append("".join(chars))
            i = j + 1
        else:
            m = re.compile(r"[^,)\s]+").match(text, i)
            if not m:
                raise SqlError(f"bad value at {i}: {text[i:i + 30]!r}")
            token = m.group(0)
            values.append(None if token.upper() == "NULL" else token)
            i = m.end()
        while i < n and text[i] in " \t\r\n":
            i += 1
        if text[i] == ",":
            i += 1
            continue
        if text[i] == ")":
            return values, i + 1
        raise SqlError(f"expected ',' or ')' at {i}: {text[i:i + 30]!r}")


# ── Replay ──────────────────────────────────────────────────────────────────
class NodeTable:
    def __init__(self):
        self.columns = None
        self.defaults = {}
        self.rows = {}
        self.vars = {}

    def require(self, stmt):
        if self.columns is None:
            raise SqlError(f"statement before CREATE TABLE: {stmt[:80]}")

    def resolve_int(self, token):
        token = token.strip().strip("'")
        if token.startswith("@"):
            return self.vars[token[1:].lower()]
        return int(token)

    def apply(self, stmt):
        """Apply one statement. Returns False if it does not touch the node table."""
        flat = " ".join(stmt.split())
        upper = flat.upper()
        table_ref = f"`{SOURCE_TABLE}`".upper()

        m = re.match(r"(?i)^SET\s+@(\w+)\s*=\s*(\d+)$", flat)
        if m:
            self.vars[m.group(1).lower()] = int(m.group(2))
            return True
        if upper.startswith("DROP TABLE"):
            if table_ref in upper:
                self.columns = None
                self.rows = {}
                return True
            return False
        if upper.startswith("CREATE TABLE"):
            if table_ref not in upper:
                return False
            self.columns = list(SOURCE_COLUMNS)
            self.defaults = dict(SOURCE_DEFAULTS)
            self.rows = {}
            return True
        if upper.startswith("CALL TMPADDCOLUMNIFNOTEXISTS"):
            m = re.search(r"(?i)ADD\s+`(\w+)`\s+\w+(?:\s+UNSIGNED)?\s+NOT NULL\s+DEFAULT\s+([\w.]+)", flat)
            if not m:
                raise SqlError(f"unexpected ALTER call: {flat[:120]}")
            self.require(stmt)
            column, default = m.group(1).lower(), m.group(2)
            if column not in self.columns:
                self.columns.append(column)
                self.defaults[column] = default
                for row in self.rows.values():
                    row[column] = default
            return True
        if upper.startswith("DROP PROCEDURE"):
            return True
        if table_ref not in upper:
            return False
        self.require(stmt)

        if upper.startswith("DELETE"):
            m = re.search(r"(?i)WHERE\s+`id`\s+BETWEEN\s+(\S+)\s+AND\s+(\S+)$", flat)
            if m:
                low, high = self.resolve_int(m.group(1)), self.resolve_int(m.group(2))
                for node_id in [k for k in self.rows if low <= k <= high]:
                    del self.rows[node_id]
                return True
            m = re.search(r"(?i)WHERE\s+`id`\s+IN\s*\(([^)]*)\)$", flat)
            if m:
                for token in m.group(1).split(","):
                    self.rows.pop(self.resolve_int(token), None)
                return True
            raise SqlError(f"unexpected DELETE: {flat[:120]}")

        if upper.startswith("INSERT"):
            m = re.match(r"(?is)^INSERT\s+INTO\s+`\w+`\s*\(([^)]*)\)\s*VALUES\s*", stmt)
            if not m:
                raise SqlError(f"unexpected INSERT: {flat[:120]}")
            columns = [c.strip().strip("`").lower() for c in m.group(1).split(",")]
            pos = m.end()
            while pos < len(stmt):
                values, pos = parse_values(stmt, pos)
                if len(values) != len(columns):
                    raise SqlError(f"column count mismatch in INSERT near {values[:2]}")
                row = dict(self.defaults)
                row.update(zip(columns, values))
                node_id = int(row["id"])
                if node_id in self.rows:
                    raise SqlError(f"duplicate id {node_id} in INSERT")
                self.rows[node_id] = row
                while pos < len(stmt) and stmt[pos] in " \t\r\n,":
                    pos += 1
            return True

        if upper.startswith("UPDATE"):
            m = re.match(r"(?i)^UPDATE\s+`\w+`\s+SET\s+(.*?)\s+WHERE\s+(.*)$", flat)
            if not m:
                raise SqlError(f"unexpected UPDATE: {flat[:120]}")
            assignments = []
            for part in re.findall(r"`(\w+)`\s*=\s*('(?:[^']|'')*'|`\w+`\s*\|\s*\d+)", m.group(1)):
                column, value = part[0].lower(), part[1]
                if value.startswith("'"):
                    assignments.append((column, "set", value[1:-1].replace("''", "'")))
                else:
                    assignments.append((column, "or", int(value.split("|")[1])))
            if not assignments:
                raise SqlError(f"no assignments in UPDATE: {flat[:120]}")
            where = m.group(2).strip()
            ids = re.fullmatch(r"\(?`id`\s*=\s*'?(\d+)'?\)?", where)
            if ids:
                targets = [int(ids.group(1))]
            else:
                in_list = re.fullmatch(r"(?i)`id`\s+IN\s*\(([^)]*)\)", where)
                if not in_list:
                    raise SqlError(f"unexpected UPDATE condition: {where[:120]}")
                targets = [self.resolve_int(t) for t in in_list.group(1).split(",")]
            for node_id in targets:
                row = self.rows.get(node_id)
                if row is None:
                    continue
                for column, op, value in assignments:
                    if op == "set":
                        row[column] = value
                    else:
                        row[column] = str(int(row[column]) | value)
            return True

        raise SqlError(f"unsupported statement on the node table: {flat[:120]}")


def replay(files, verbose):
    table = NodeTable()
    for path in files:
        with open(path, encoding="utf-8") as f:
            text = f.read()
        statements = split_statements(text)
        applied = 0
        for stmt in statements:
            try:
                if table.apply(stmt):
                    applied += 1
                elif verbose:
                    write_warn(f"  ignored (other table): {' '.join(stmt.split())[:100]}")
            except (SqlError, KeyError, IndexError, ValueError) as e:
                raise SqlError(f"{os.path.basename(path)}: {e}") from e
        if verbose:
            print(f"{DIM}  {os.path.basename(path)}: {applied}/{len(statements)} statements, "
                  f"{len(table.rows)} nodes{RESET}")
    if table.columns is None:
        raise SqlError("the files never create the node table")
    return table


# ── Convert ─────────────────────────────────────────────────────────────────
def convert(table, maps, stats):
    kept = {}
    for node_id, row in sorted(table.rows.items()):
        map_id = int(row["mapid"])
        if map_id not in maps:
            stats["dropped_map"] += 1
            continue
        min_level = int(row["minlevel"]) or MIN_LEVEL
        max_level = int(row["maxlevel"]) or MAX_LEVEL
        if min_level > MAX_LEVEL:
            stats["dropped_level"] += 1
            stats["dropped_level_ids"].append(node_id)
            continue
        if max_level > MAX_LEVEL:
            stats["clamped_level"] += 1
        min_level = max(MIN_LEVEL, min(min_level, MAX_LEVEL))
        max_level = max(min_level, min(max_level, MAX_LEVEL))
        flags = int(row["flags"])
        if flags & ~KEPT_FLAGS_MASK:
            stats["flags_stripped"] += 1
        kept[node_id] = {
            "id": node_id,
            "name": row["name"],
            "map_id": map_id,
            "zone_id": int(row["zoneid"]),
            "area_id": int(row["areaid"]),
            "min_level": min_level,
            "max_level": max_level,
            "min_wait_time": int(row.get("minwaittime", "3000")),
            "max_wait_time": int(row.get("maxwaittime", "7000")),
            "proximity": row.get("proximity", "0"),
            "flags": flags & KEPT_FLAGS_MASK,
            "x": row["x"], "y": row["y"], "z": row["z"], "o": row["o"],
            "raw_links": row["links"] or "",
        }

    for node in kept.values():
        links = []
        for token in node["raw_links"].split():
            link_id = int(token.split(":")[0])
            if link_id == node["id"] or link_id in links:
                stats["links_bad"] += 1
                continue
            target = kept.get(link_id)
            if target is None or target["map_id"] != node["map_id"]:
                stats["links_dropped"] += 1
                continue
            links.append(link_id)
        node["links"] = links
        if not links:
            stats["no_links"].append(node["id"])
    return kept


def sql_string(value):
    return "'" + value.replace("\\", "\\\\").replace("'", "''") + "'"


def build_migration(nodes, timestamp, source_files):
    lines = []
    out = lines.append
    out("DROP PROCEDURE IF EXISTS add_migration;")
    out("DELIMITER ??")
    out("CREATE PROCEDURE `add_migration`()")
    out("BEGIN")
    out("DECLARE v INT DEFAULT 1;")
    out(f"SET v = (SELECT COUNT(*) FROM `migrations` WHERE `id`='{timestamp}');")
    out("IF v = 0 THEN")
    out(f"INSERT INTO `migrations` VALUES ('{timestamp}');")
    out("-- Add your query below.")
    out("")
    out("-- Wandering bots: wander nodes (a graph of points the bots walk between), maps 0 and 1.")
    out("-- Generated by contrib/wander_nodes/convert_npcbots_wander_nodes.py from AzerothCore-NPCBots'")
    out(f"-- {SOURCE_TABLE} ({len(source_files)} files, {source_files[0]} .. {source_files[-1]}).")
    out("-- flags: 0x1 spawn, 0x2 Alliance only, 0x4 Horde only, 0x8 can backtrack from, 0x10 movement ignores")
    out("-- faction, 0x20 movement ignores pathing, 0x10000 / 0x20000 forced jump begin / end, 0x40000 mill")
    out("-- radius reduced, 0x80000 not a start point. links: ids of the linked nodes, space separated.")
    out(f"DROP TABLE IF EXISTS `{TARGET_TABLE}`;")
    out(f"CREATE TABLE `{TARGET_TABLE}` (")
    out("  `id` INT UNSIGNED NOT NULL,")
    out("  `name` VARCHAR(100) NOT NULL DEFAULT '',")
    out("  `map_id` SMALLINT UNSIGNED NOT NULL DEFAULT '0',")
    out("  `zone_id` INT UNSIGNED NOT NULL DEFAULT '0',")
    out("  `area_id` INT UNSIGNED NOT NULL DEFAULT '0',")
    out(f"  `min_level` TINYINT UNSIGNED NOT NULL DEFAULT '{MIN_LEVEL}',")
    out(f"  `max_level` TINYINT UNSIGNED NOT NULL DEFAULT '{MAX_LEVEL}',")
    out("  `min_wait_time` INT UNSIGNED NOT NULL DEFAULT '3000' COMMENT 'ms',")
    out("  `max_wait_time` INT UNSIGNED NOT NULL DEFAULT '7000' COMMENT 'ms',")
    out("  `proximity` FLOAT NOT NULL DEFAULT '0' COMMENT 'random offset radius at the node',")
    out("  `flags` INT UNSIGNED NOT NULL DEFAULT '0',")
    out("  `position_x` FLOAT NOT NULL DEFAULT '0',")
    out("  `position_y` FLOAT NOT NULL DEFAULT '0',")
    out("  `position_z` FLOAT NOT NULL DEFAULT '0',")
    out("  `orientation` FLOAT NOT NULL DEFAULT '0',")
    out("  `links` TEXT,")
    out("  PRIMARY KEY (`id`)")
    out(") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci COMMENT='Wandering bots: wander nodes';")
    out("")
    out(f"INSERT INTO `{TARGET_TABLE}` (`id`, `name`, `map_id`, `zone_id`, `area_id`, `min_level`, `max_level`, "
        "`min_wait_time`, `max_wait_time`, `proximity`, `flags`, `position_x`, `position_y`, `position_z`, "
        "`orientation`, `links`) VALUES")
    ordered = [nodes[k] for k in sorted(nodes)]
    for index, n in enumerate(ordered):
        links = " ".join(str(link) for link in n["links"])
        end = ";" if index == len(ordered) - 1 else ","
        out(f"({n['id']}, {sql_string(n['name'])}, {n['map_id']}, {n['zone_id']}, {n['area_id']}, "
            f"{n['min_level']}, {n['max_level']}, {n['min_wait_time']}, {n['max_wait_time']}, {n['proximity']}, "
            f"{n['flags']}, {n['x']}, {n['y']}, {n['z']}, {n['o']}, {sql_string(links)}){end}")
    out("")
    out("")
    out("-- End of migration.")
    out("END IF;")
    out("END??")
    out("DELIMITER ;")
    out("CALL add_migration();")
    out("DROP PROCEDURE IF EXISTS add_migration;")
    return "\n".join(lines) + "\n"


def print_summary(nodes, stats, maps):
    write_info("Summary")
    for map_id in maps:
        on_map = [n for n in nodes.values() if n["map_id"] == map_id]
        spawn = sum(1 for n in on_map if n["flags"] & WANDER_NODE_FLAG_SPAWN)
        write_ok(f"  map {map_id}: {len(on_map)} nodes, {spawn} spawn nodes")
    bands = {}
    for n in nodes.values():
        band = (n["min_level"] - 1) // LEVEL_BAND_SIZE
        bands[band] = bands.get(band, 0) + 1
    for band in sorted(bands):
        low = band * LEVEL_BAND_SIZE + 1
        print(f"{DIM}  min level {low}-{low + LEVEL_BAND_SIZE - 1}: {bands[band]} nodes{RESET}")
    write_info_alt(f"  dropped: {stats['dropped_map']} on other maps, {stats['dropped_level']} above level "
                   f"{MAX_LEVEL} {stats['dropped_level_ids'] or ''}")
    write_info_alt(f"  levels clamped to {MAX_LEVEL}: {stats['clamped_level']}, BG flags stripped: "
                   f"{stats['flags_stripped']}")
    write_info_alt(f"  links dropped: {stats['links_dropped']} to removed nodes, {stats['links_bad']} self / duplicate")
    if stats["no_links"]:
        write_warn(f"  nodes without links ({len(stats['no_links'])}): {stats['no_links']}")


def show_command(files, maps, shell):
    """Print a mysql replay of the same files into a scratch schema, selecting what the converter keeps."""
    names = [f.replace("\\", "/") for f in files]
    select = (f"SELECT id, name, mapid, zoneid, areaid, LEAST(GREATEST(minlevel, {MIN_LEVEL}), {MAX_LEVEL}), "
              f"LEAST(GREATEST(maxlevel, {MIN_LEVEL}), {MAX_LEVEL}), minwaittime, maxwaittime, proximity, "
              f"flags & {KEPT_FLAGS_MASK}, x, y, z, o, links FROM {SOURCE_TABLE} "
              f"WHERE mapid IN ({', '.join(str(m) for m in maps)}) AND minlevel <= {MAX_LEVEL} ORDER BY id;")
    print(f"# python {' '.join(sys.argv)}")
    print("# replays the NPCBots files into a scratch schema (needs CREATE DATABASE); links to dropped nodes are")
    print("# not filtered by this query")
    if shell == "powershell":
        print("mysql -u <user> -p -e 'CREATE DATABASE wander_nodes_scratch;'")
        print("Get-Content -Raw " + ", ".join(f"'{n}'" for n in names) + " | mysql -u <user> -p wander_nodes_scratch")
        print(f"mysql -u <user> -p wander_nodes_scratch -e \"{select}\"")
        print("mysql -u <user> -p -e 'DROP DATABASE wander_nodes_scratch;'")
    else:
        print("mysql -u <user> -p -e 'CREATE DATABASE wander_nodes_scratch;'")
        print("cat " + " ".join(f"'{n}'" for n in names) + " | mysql -u <user> -p wander_nodes_scratch")
        print(f"mysql -u <user> -p wander_nodes_scratch -e \"{select}\"")
        print("mysql -u <user> -p -e 'DROP DATABASE wander_nodes_scratch;'")


def default_source_dir():
    root = os.environ.get("code_root_dir")
    return os.path.join(root, DEFAULT_NPCBOTS_SUBDIR) if root else None


def main():
    _enable_windows_ansi()
    parser = argparse.ArgumentParser(description="Convert AzerothCore-NPCBots wander nodes into a vmangos world migration.")
    parser.add_argument("path", nargs="?", help="NPCBots db_world sql dir (same as --src)")
    parser.add_argument("--src", help="NPCBots db_world sql dir (default: $code_root_dir/" + DEFAULT_NPCBOTS_SUBDIR.replace(os.sep, "/") + ")")
    parser.add_argument("--pattern", default=DEFAULT_FILE_PATTERN, help=f"file name pattern (default: {DEFAULT_FILE_PATTERN})")
    parser.add_argument("--case-sensitive", action="store_true", help="match --pattern case sensitively (default: insensitive)")
    parser.add_argument("--maps", default=",".join(str(m) for m in DEFAULT_MAPS), help="maps to keep (default: 0,1)")
    parser.add_argument("--timestamp", help="migration timestamp YYYYmmddHHMMSS (default: now, UTC)")
    parser.add_argument("--out", help="output file (default: sql/migrations/<timestamp>_world.sql)")
    parser.add_argument("-n", "--dry-run", action="store_true", help="convert and print the summary, write nothing")
    parser.add_argument("-v", "--verbose", action="store_true", help="per-file progress and ignored statements")
    parser.add_argument("--show-cmd", action="store_true", help="print an equivalent mysql replay command instead of converting")
    parser.add_argument("--cmd-shell", choices=["powershell", "bash"], default="powershell" if os.name == "nt" else "bash",
                        help="shell for --show-cmd (default: the platform's)")
    args = parser.parse_args()

    src = args.src or args.path or default_source_dir()
    if not src or not os.path.isdir(src):
        write_err(f"NPCBots sql dir not found: {src} (set $code_root_dir or pass it)")
        return 1
    maps = tuple(int(m) for m in args.maps.split(",") if m.strip())

    def matches(name):
        if args.case_sensitive:
            return fnmatch.fnmatchcase(name, args.pattern)
        return fnmatch.fnmatchcase(name.lower(), args.pattern.lower())

    names = sorted(f for f in os.listdir(src) if matches(f))
    if not names:
        write_err(f"no files matching {args.pattern} in {src}")
        return 1
    files = [os.path.join(src, f) for f in names]

    if args.show_cmd:
        show_command(files, maps, args.cmd_shell)
        return 0

    write_info(f"Replaying {len(files)} files from {src}")
    try:
        table = replay(files, args.verbose)
    except SqlError as e:
        write_err(f"replay failed: {e}")
        return 1
    write_ok(f"  final NPCBots table: {len(table.rows)} nodes, columns {', '.join(table.columns)}")

    stats = {"dropped_map": 0, "dropped_level": 0, "dropped_level_ids": [], "clamped_level": 0,
             "flags_stripped": 0, "links_dropped": 0, "links_bad": 0, "no_links": []}
    nodes = convert(table, maps, stats)
    print_summary(nodes, stats, maps)

    timestamp = args.timestamp or datetime.datetime.now(datetime.timezone.utc).strftime(TIMESTAMP_FORMAT)
    if not re.fullmatch(r"\d{14}", timestamp):
        write_err(f"bad --timestamp {timestamp}, expected {TIMESTAMP_FORMAT}")
        return 1
    out = args.out or os.path.join(MIGRATIONS_DIR, f"{timestamp}_world.sql")
    if args.dry_run:
        write_warn(f"dry run: would write {len(nodes)} nodes to {out}")
        return 0
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(build_migration(nodes, timestamp, names))
    write_ok(f"wrote {out}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
