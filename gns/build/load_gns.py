"""Build gns.db (Grimoire & Steel game database).

Applies gns/db/schema.sql, then loads the curated content from gns/data/*.json
(one JSON file per source table, each a list of row dicts). Idempotent: the
schema drops and recreates every table/view/trigger.

    py gns/build/load_gns.py

The section_fts index is kept in sync automatically by triggers as sections are
inserted; the standalone rules_search FTS is populated directly from
rules_search.json.
"""
import json
import sqlite3
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent          # the gns/ directory
SCHEMA = ROOT / "db" / "schema.sql"
DATA = ROOT / "data"
DB = ROOT / "gns.db"

# On-disk PRAGMA user_version carried by gns.db (matches the shipped database).
USER_VERSION = 5

# Tables loaded in dependency order (parents before children).
TABLES = [
    "game",
    "trait",
    "kin",
    "training",
    "calling",
    "calling_training_option",
    "calling_weapon_option",
    "weapon_category",
    "armor",
    "challenge_number",
    "monster",
    "spell",
    "magic_weapon_example",
    "advancement_level",
    "module_completion_award",
    "level_improvement_option",
    "character_sheet",
    "section",
]


def load_rows(name):
    path = DATA / f"{name}.json"
    if not path.exists():
        raise SystemExit(f"missing data file: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def insert_rows(conn, table, rows):
    for row in rows:
        cols = list(row.keys())
        placeholders = ", ".join("?" for _ in cols)
        conn.execute(
            f"INSERT INTO {table} ({', '.join(cols)}) VALUES ({placeholders})",
            [row[c] for c in cols],
        )
    return len(rows)


def main():
    if not SCHEMA.exists():
        raise SystemExit(f"missing schema: {SCHEMA}")

    conn = sqlite3.connect(DB)
    try:
        conn.executescript(SCHEMA.read_text(encoding="utf-8"))
        conn.execute("PRAGMA foreign_keys = ON")

        total = 0
        for table in TABLES:
            n = insert_rows(conn, table, load_rows(table))
            total += n
            print(f"  {table}: {n}")

        # Standalone cross-content full-text search table.
        rs = load_rows("rules_search")
        for r in rs:
            conn.execute(
                "INSERT INTO rules_search (category, name, text) VALUES (?, ?, ?)",
                (r["category"], r["name"], r["text"]),
            )
        print(f"  rules_search: {len(rs)}")

        conn.execute(f"PRAGMA user_version = {USER_VERSION}")
        conn.commit()
    finally:
        conn.close()

    print(f"\nBuilt {DB} ({total} content rows + {len(rs)} search rows).")
    return 0


if __name__ == "__main__":
    sys.exit(main())
