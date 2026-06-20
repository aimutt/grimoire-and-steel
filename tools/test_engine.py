"""Headless tests for sql_engine (no Tkinter needed).

    py tools/test_engine.py
"""
import sqlite3
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
from sql_engine import run_sql, split_statements  # noqa: E402

ROOT = Path(__file__).resolve().parent.parent
GNS_DB = ROOT / "gns" / "gns.db"

passed = failed = 0


def check(label, got, expected):
    global passed, failed
    if got == expected:
        passed += 1
        print(f"  [OK ] {label}")
    else:
        failed += 1
        print(f"  [XX ] {label}: got {got!r}, expected {expected!r}")


print("split_statements:")
check("simple two statements",
      split_statements("SELECT 1; SELECT 2;"), ["SELECT 1", "SELECT 2"])
check("semicolon inside string literal",
      split_statements("SELECT 'a;b';"), ["SELECT 'a;b'"])
check("semicolon inside line comment",
      split_statements("SELECT 1 -- a;b\n; SELECT 2;"),
      ["SELECT 1 -- a;b", "SELECT 2"])
check("semicolon inside block comment",
      split_statements("SELECT /* x;y */ 1;"), ["SELECT /* x;y */ 1"])
check("trailing statement without semicolon",
      split_statements("SELECT 1"), ["SELECT 1"])
check("comment-only chunk dropped",
      split_statements("-- just a comment\n"), [])
check("double-quoted identifier with semicolon",
      split_statements('SELECT "a;b" FROM t;'), ['SELECT "a;b" FROM t'])

print("\nrun_sql on in-memory db (DDL + DML + SELECT):")
mem = sqlite3.connect(":memory:")
res = run_sql(mem, """
    CREATE TABLE t (id INTEGER PRIMARY KEY, name TEXT);
    INSERT INTO t (name) VALUES ('alpha'), ('beta');
    SELECT name FROM t ORDER BY id;
""")
check("three statement results", len(res), 3)
check("insert rowcount", res[1].get("rowcount"), 2)
check("select columns", res[2].get("columns"), ["name"])
check("select rows", [r[0] for r in res[2]["rows"]], ["alpha", "beta"])

print("\nrun_sql error handling:")
try:
    run_sql(mem, "SELECT * FROM does_not_exist;")
    check("error raised", False, True)
except sqlite3.Error as e:
    check("error raised", True, True)
    check("error carries offending statement",
          getattr(e, "statement", None), "SELECT * FROM does_not_exist")
mem.close()

print("\nrun_sql on gns.db (read-only):")
if GNS_DB.exists():
    conn = sqlite3.connect(GNS_DB)
    res = run_sql(conn, "SELECT name, reversed_name FROM cleric_spells "
                        "WHERE is_reversible = 1 ORDER BY name;")
    check("reversible cleric rows", len(res[0]["rows"]), 6)
    check("columns", res[0]["columns"], ["name", "reversed_name"])
    multi = run_sql(conn, "SELECT 1; SELECT count(*) FROM magic_user_spells;")
    check("multi-statement last result", multi[-1]["rows"][0][0], 50)
    conn.close()
else:
    print(f"  [--] skipped: {GNS_DB} not found (run gns/build/load_gns.py)")

print(f"\nResult: {passed} passed, {failed} failed")
sys.exit(0 if failed == 0 else 1)
