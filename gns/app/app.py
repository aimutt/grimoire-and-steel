"""Flask app to browse/query gns.db (Grimoire & Steel — Spells module).

    py gns/app/app.py        # then open http://127.0.0.1:5001

Routes:
    /                        browse spells (filter by class/level, search by text)
    /spell/<class>/<id>      full detail for one spell, incl. its mini-tables
    /health, /shutdown       used by Launcher.hta
"""
import os
import sqlite3
import threading
import time
from pathlib import Path

from flask import Flask, abort, g, jsonify, render_template, request

PORT = 5001
ROOT = Path(__file__).resolve().parent.parent      # the gns/ directory
DB = ROOT / "gns.db"

CLASSES = {"magic-user": "magic_user_spells", "cleric": "cleric_spells"}

app = Flask(__name__)


@app.after_request
def allow_local_control(resp):
    resp.headers["Access-Control-Allow-Origin"] = "*"
    return resp


@app.route("/health")
def health():
    return jsonify(status="ok")


@app.route("/shutdown")
def shutdown():
    def _kill():
        time.sleep(0.4)
        os._exit(0)

    threading.Thread(target=_kill, daemon=True).start()
    return jsonify(status="stopping")


def get_db():
    if "db" not in g:
        if not DB.exists():
            raise RuntimeError(f"{DB} not found - run 'py gns/build/load_gns.py' first.")
        g.db = sqlite3.connect(DB)
        g.db.row_factory = sqlite3.Row
    return g.db


@app.teardown_appcontext
def close_db(exc):
    db = g.pop("db", None)
    if db is not None:
        db.close()


@app.route("/")
def browse():
    db = get_db()
    q = (request.args.get("q") or "").strip()
    cls = request.args.get("class") or ""
    level = request.args.get("level") or ""

    where, params = [], []
    if q:
        where.append("(name LIKE ? OR IFNULL(description,'') LIKE ?)")
        params += [f"%{q}%", f"%{q}%"]
    if cls in CLASSES:
        where.append("spell_class = ?")
        params.append(cls)
    if level in ("1", "2", "3"):
        where.append("spell_level = ?")
        params.append(int(level))

    sql = "SELECT * FROM all_spells"
    if where:
        sql += " WHERE " + " AND ".join(where)
    sql += " ORDER BY spell_class, spell_level, name"
    spells = db.execute(sql, params).fetchall()

    counts = {
        "magic-user": db.execute("SELECT COUNT(*) FROM magic_user_spells").fetchone()[0],
        "cleric": db.execute("SELECT COUNT(*) FROM cleric_spells").fetchone()[0],
    }
    return render_template(
        "index.html", spells=spells, q=q, cls=cls, level=level, counts=counts
    )


@app.route("/spell/<spell_class>/<int:spell_id>")
def spell(spell_class, spell_id):
    if spell_class not in CLASSES:
        abort(404)
    db = get_db()
    table = CLASSES[spell_class]
    row = db.execute(f"SELECT * FROM {table} WHERE id = ?", (spell_id,)).fetchone()
    if row is None:
        abort(404)

    fk = "mu_spell_id" if spell_class == "magic-user" else "cleric_spell_id"
    detail_tables = []
    for dt in db.execute(
        f"SELECT id, caption FROM spell_detail_tables WHERE {fk} = ? ORDER BY id",
        (spell_id,),
    ).fetchall():
        rows = db.execute(
            "SELECT key_label, value_text FROM spell_detail_rows "
            "WHERE detail_table_id = ? ORDER BY row_order",
            (dt["id"],),
        ).fetchall()
        detail_tables.append({"caption": dt["caption"], "rows": rows})

    return render_template(
        "spell.html", spell=row, spell_class=spell_class, detail_tables=detail_tables
    )


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=PORT, debug=False, use_reloader=False)
