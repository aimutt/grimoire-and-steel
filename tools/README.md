# tools/ — local SQLite console (private)

A fully offline desktop GUI for running **any** SQL against `gns.db` (or any SQLite
file). Built on Python's bundled Tkinter — no install, no internet.

## Launch
- Double-click **`Launch DB Console.vbs`** (opens with no console window), or
- run `py tools/db_console.pyw` from a terminal.

It opens `../gns/gns.db` by default; use **Open…** to pick any `.db`/`.sqlite` file,
or **New…** to create one.

## What it does
- **Schema panel** (left): tables & views with their columns. Double-click an object
  to auto-run `SELECT * FROM <it> LIMIT 100`.
- **SQL editor** (right): type or **paste** any SQL. `Ctrl+Enter` or `F5` executes
  (runs the selection if you've highlighted some text, otherwise the whole buffer).
  Right-click for Cut/Copy/Paste/Select All.
- **Execute** — runs one or more `;`-separated statements; the last result set shows
  in the grid.
- **Run as Script** — runs the whole buffer with `executescript` (good for large
  DDL/seed batches; note: this commits immediately).
- **Export CSV…** — saves the current grid.
- **Results grid** (bottom): `NULL` and `<BLOB n bytes>` shown explicitly.

## View / edit a row
- **Double-click any result row** to open a detail form (one field per column; long
  text like `description` gets a multi-line box).
- Edit fields and click **Update** — it writes only the columns you changed via
  `UPDATE … WHERE <primary key>`. Clear a field to set it `NULL`; numeric columns are
  coerced to int/real automatically. **Back to results** returns to the grid.
- If you have unsaved edits, leaving the detail view (Back, running a new query,
  opening another DB, etc.) **warns you before discarding** them.
- Editing requires the result to map to a single base table whose **primary key is in
  the results** (e.g. `SELECT * FROM cleric_spells`). Otherwise the detail view is
  read-only (views like `all_spells`, joins, or column subsets without the PK) and
  **Update** is disabled. Primary-key fields are always read-only.
- Updates respect the Autocommit / Commit / Rollback setting like any other write.

## Transactions / full control
Every operation is allowed (SELECT, INSERT/UPDATE/DELETE, CREATE/DROP/ALTER, PRAGMA,
VACUUM, ATTACH, …). `PRAGMA foreign_keys = ON` is set on connect.

- **Autocommit ON** (default): each Execute persists immediately.
- **Autocommit OFF**: an Execute opens a transaction; use **Commit** / **Rollback**.
  Because transactions are managed explicitly, even DDL (e.g. `CREATE TABLE`) can be
  rolled back. The status bar shows `(uncommitted …)` while a transaction is open.

## Files
- `db_console.pyw` — the GUI.
- `sql_engine.py` — GUI-free statement splitter + executor (unit-tested).
- `test_engine.py` — `py tools/test_engine.py` runs the headless engine tests.
- `Launch DB Console.vbs` — double-click launcher.
