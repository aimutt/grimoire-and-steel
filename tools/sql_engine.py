"""GUI-free SQL execution helpers for the Grimoire & Steel DB console.

Kept separate from the Tkinter GUI so the logic can be unit-tested headlessly.
Pure standard library (sqlite3).
"""
import sqlite3


def split_statements(sql):
    """Split a SQL buffer into individual statements.

    Splits on ';' while ignoring semicolons inside single/double-quoted string
    literals and inside -- line comments and /* */ block comments. Returns a
    list of trimmed, non-empty statement strings (comments-only chunks dropped).
    """
    statements = []
    buf = []
    i = 0
    n = len(sql)
    in_single = in_double = in_line_comment = in_block_comment = False

    while i < n:
        ch = sql[i]
        nxt = sql[i + 1] if i + 1 < n else ""

        if in_line_comment:
            buf.append(ch)
            if ch == "\n":
                in_line_comment = False
            i += 1
            continue
        if in_block_comment:
            buf.append(ch)
            if ch == "*" and nxt == "/":
                buf.append(nxt)
                i += 2
                in_block_comment = False
                continue
            i += 1
            continue
        if in_single:
            buf.append(ch)
            if ch == "'":
                in_single = False
            i += 1
            continue
        if in_double:
            buf.append(ch)
            if ch == '"':
                in_double = False
            i += 1
            continue

        # Not in any quote/comment.
        if ch == "-" and nxt == "-":
            in_line_comment = True
            buf.append(ch)
            i += 1
            continue
        if ch == "/" and nxt == "*":
            in_block_comment = True
            buf.append(ch)
            buf.append(nxt)
            i += 2
            continue
        if ch == "'":
            in_single = True
            buf.append(ch)
            i += 1
            continue
        if ch == '"':
            in_double = True
            buf.append(ch)
            i += 1
            continue
        if ch == ";":
            stmt = "".join(buf).strip()
            if _has_sql(stmt):
                statements.append(stmt)
            buf = []
            i += 1
            continue

        buf.append(ch)
        i += 1

    tail = "".join(buf).strip()
    if _has_sql(tail):
        statements.append(tail)
    return statements


def _has_sql(stmt):
    """True if the statement contains executable SQL (not only comments/blank)."""
    if not stmt:
        return False
    out = []
    i, n = 0, len(stmt)
    in_line = in_block = False
    while i < n:
        ch = stmt[i]
        nxt = stmt[i + 1] if i + 1 < n else ""
        if in_line:
            if ch == "\n":
                in_line = False
            i += 1
            continue
        if in_block:
            if ch == "*" and nxt == "/":
                in_block = False
                i += 2
                continue
            i += 1
            continue
        if ch == "-" and nxt == "-":
            in_line = True
            i += 2
            continue
        if ch == "/" and nxt == "*":
            in_block = True
            i += 2
            continue
        out.append(ch)
        i += 1
    return "".join(out).strip() != ""


def run_sql(conn, sql, max_rows=5000):
    """Execute every statement in `sql` on `conn`.

    Returns a list of per-statement result dicts:
        {"sql": str, "columns": [...], "rows": [...], "truncated": bool}  # result set
        {"sql": str, "rowcount": int}                                     # no result set

    Raises sqlite3.Error on failure; the exception gains a `.statement`
    attribute holding the offending SQL. The caller controls commit/rollback.
    """
    results = []
    for stmt in split_statements(sql):
        cur = conn.cursor()
        try:
            cur.execute(stmt)
        except sqlite3.Error as e:
            e.statement = stmt
            raise
        if cur.description is not None:
            columns = [d[0] for d in cur.description]
            rows = cur.fetchmany(max_rows)
            truncated = cur.fetchone() is not None
            results.append(
                {"sql": stmt, "columns": columns, "rows": rows, "truncated": truncated}
            )
        else:
            results.append({"sql": stmt, "rowcount": cur.rowcount})
        cur.close()
    return results
