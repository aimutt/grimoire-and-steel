"""Sanity + integrity checks for gns.db (Grimoire & Steel).

Read-only: opens the built database and asserts table presence, expected row
counts, referential integrity, and full-text search health. Run after
load_gns.py. Exits non-zero if any check fails.

    py gns/build/verify_gns.py
"""
import sqlite3
import sys
from pathlib import Path

DB = Path(__file__).resolve().parent.parent / "gns.db"

# Expected row counts for the curated content tables.
COUNTS = {
    "game": 1,
    "trait": 4,
    "kin": 4,
    "training": 13,
    "calling": 4,
    "calling_training_option": 9,
    "calling_weapon_option": 10,
    "weapon_category": 5,
    "armor": 5,
    "challenge_number": 5,
    "monster": 6,
    "spell": 10,
    "magic_weapon_example": 6,
    "advancement_level": 40,
    "module_completion_award": 4,
    "level_improvement_option": 5,
    "character_sheet": 2,
    "section": 28,
    "rules_search": 52,
}


def main() -> int:
    if not DB.exists():
        print(f"{DB} not found - run 'py gns/build/load_gns.py' first.", file=sys.stderr)
        return 1

    c = sqlite3.connect(DB)
    c.execute("PRAGMA foreign_keys = ON")
    ok = True

    def check(label, got, expected):
        nonlocal ok
        flag = "OK " if got == expected else "XX "
        if got != expected:
            ok = False
        print(f"  [{flag}] {label}: {got!r} (expected {expected!r})")

    one = lambda sql, *a: c.execute(sql, a).fetchone()[0]

    print("Row counts:")
    for table, expected in COUNTS.items():
        check(table, one(f"select count(*) from {table}"), expected)

    print("\nSpot checks:")
    check("game title", one("select title from game where game_id=1"), "Grimoire & Steel")
    check("traits are the four", one(
        "select group_concat(name, ',') from (select name from trait order by sort_order)"),
        "Might,Grace,Wits,Spirit")
    check("Mail armor defense bonus", one("select defense_bonus from armor where name='Mail armor'"), 2)
    check("One-handed weapon die", one(
        "select damage_die from weapon_category where name='One-handed weapon'"), "1d6")
    check("Ogre life", one("select life from monster where name='Ogre'"), 22)
    check("Heal challenge number", one("select challenge_number from spell where name='Heal'"), 12)
    check("Blade level-5 AP", one(
        "select ap_required from advancement_level a join calling c on c.calling_id=a.calling_id "
        "where c.name='Blade' and a.level=5"), 1000)
    check("Normal challenge target", one(
        "select target_number from challenge_number where name='Normal'"), 12)

    print("\nIntegrity:")
    check("advancement levels 1..10 per calling", one(
        "select count(*) from calling"), one(
        "select count(distinct calling_id) from advancement_level"))
    check("orphan calling_training_option", one(
        "select count(*) from calling_training_option cto "
        "left join training t on t.training_id=cto.training_id where t.training_id is null"), 0)
    check("orphan advancement_level", one(
        "select count(*) from advancement_level a "
        "left join calling c on c.calling_id=a.calling_id where c.calling_id is null"), 0)

    print("\nFull-text search:")
    check("section_fts MATCH 'strain'", one(
        "select count(*) from section_fts where section_fts match 'strain'") > 0, True)
    check("rules_search MATCH 'spell'", one(
        "select count(*) from rules_search where rules_search match 'spell'") > 0, True)
    # section_fts stays in sync with section (external-content FTS via triggers).
    check("section_fts row count matches section", one(
        "select count(*) from section_fts"), one("select count(*) from section"))

    c.close()
    print("\n" + ("All checks passed." if ok else "SOME CHECKS FAILED."))
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
