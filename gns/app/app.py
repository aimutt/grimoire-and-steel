"""Flask app to browse/query gns.db (Grimoire & Steel — rules & content).

    py gns/app/app.py        # then open http://127.0.0.1:5001

Routes:
    /                        landing: game metadata + rules-bible section index
    /section/<slug>          one rules-bible section, rendered from markdown
    /search?q=               full-text search across sections + content (rules_search)
    /monsters /spells /callings /kin /traits /weapons /armor /advancement
                             curated content browsers (backed by the v_* views)
    /table/<name>            generic viewer for any table/view (whitelisted)
    /health, /shutdown       used by Launcher.hta
"""
import os
import sqlite3
import threading
import time
from pathlib import Path

from flask import Flask, abort, g, jsonify, redirect, render_template, request, url_for

try:
    import markdown as _markdown  # optional; see gns/requirements.txt

    def render_markdown(text):
        return _markdown.markdown(text or "", extensions=["tables", "fenced_code", "sane_lists"])
except ImportError:  # graceful fallback: show the raw markdown in a <pre> block
    from markupsafe import escape

    def render_markdown(text):
        return "<pre class='book'>" + str(escape(text or "")) + "</pre>"


PORT = 5001
ROOT = Path(__file__).resolve().parent.parent      # the gns/ directory
DB = ROOT / "gns.db"

# Curated content browsers: route -> (page title, SQL over a view/table).
CONTENT_PAGES = {
    "monsters":    ("Monsters",     "SELECT * FROM v_monster_summary"),
    "spells":      ("Spells",       "SELECT * FROM v_spell_summary"),
    "callings":    ("Callings",     "SELECT * FROM v_calling_summary"),
    "kin":         ("Kin",          "SELECT name, description, gift_name, gift_description FROM kin ORDER BY sort_order"),
    "traits":      ("Traits",       "SELECT name, description FROM trait ORDER BY sort_order"),
    "weapons":     ("Weapons",      "SELECT name, damage_die, examples FROM weapon_category ORDER BY sort_order"),
    "armor":       ("Armor",        "SELECT name, defense_bonus, notes FROM armor ORDER BY sort_order"),
    "advancement": ("Advancement",  "SELECT calling, level, ap_required FROM v_advancement_table"),
}

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


def listable_objects(db):
    """Tables + views that are safe to expose in /table/<name> (no FTS internals)."""
    rows = db.execute(
        "SELECT name, type FROM sqlite_master "
        "WHERE type IN ('table','view') AND name NOT LIKE 'sqlite_%' "
        "ORDER BY type DESC, name"
    ).fetchall()
    skip = ("section_fts", "rules_search")
    return [
        r["name"]
        for r in rows
        if r["name"] not in skip
        and not any(r["name"].endswith(s) for s in ("_config", "_content", "_data", "_docsize", "_idx"))
    ]


def fts_query(raw):
    """Turn a user query into a safe FTS5 MATCH string: quote each term so FTS
    punctuation/operators can't raise a syntax error (same idea as concept/)."""
    terms = [t for t in raw.replace('"', " ").split() if t]
    return " ".join(f'"{t}"' for t in terms)


@app.route("/")
def index():
    db = get_db()
    game = db.execute("SELECT * FROM game WHERE game_id = 1").fetchone()
    sections = db.execute(
        "SELECT section_number, title, slug FROM section ORDER BY sort_order"
    ).fetchall()
    return render_template(
        "index.html",
        game=game,
        sections=sections,
        content_pages=CONTENT_PAGES,
        tables=listable_objects(db),
    )


@app.route("/section/<slug>")
def section(slug):
    db = get_db()
    row = db.execute("SELECT * FROM section WHERE slug = ?", (slug,)).fetchone()
    if row is None:
        abort(404)
    nav = db.execute(
        "SELECT slug, title FROM section ORDER BY sort_order"
    ).fetchall()
    return render_template(
        "section.html", section=row, body_html=render_markdown(row["body_markdown"]), nav=nav
    )


@app.route("/search")
def search():
    db = get_db()
    q = (request.args.get("q") or "").strip()
    results = []
    if q:
        match = fts_query(q)
        if match:
            slug_by_title = {
                r["title"]: r["slug"] for r in db.execute("SELECT title, slug FROM section")
            }
            for r in db.execute(
                "SELECT category, name, "
                "snippet(rules_search, 2, '<mark>', '</mark>', '…', 12) AS snip "
                "FROM rules_search WHERE rules_search MATCH ? "
                "ORDER BY rank LIMIT 100",
                (match,),
            ):
                results.append(
                    {
                        "category": r["category"],
                        "name": r["name"],
                        "snippet": r["snip"],
                        "slug": slug_by_title.get(r["name"]) if r["category"] == "section" else None,
                    }
                )
    return render_template("search.html", q=q, results=results)


@app.route("/c/<key>")
def content(key):
    if key not in CONTENT_PAGES:
        abort(404)
    db = get_db()
    title, sql = CONTENT_PAGES[key]
    rows = db.execute(sql).fetchall()
    cols = rows[0].keys() if rows else []
    return render_template("rows.html", title=title, columns=cols, rows=rows, source=key)


# Friendly aliases (/monsters, /spells, ...) for the curated content pages.
def _make_alias(key):
    def view():
        return content(key)

    view.__name__ = f"content_{key}"
    return view


for _key in CONTENT_PAGES:
    app.add_url_rule(f"/{_key}", endpoint=f"content_{_key}", view_func=_make_alias(_key))


@app.route("/table/<name>")
def table(name):
    db = get_db()
    if name not in listable_objects(db):
        abort(404)
    rows = db.execute(f"SELECT * FROM {name} LIMIT 1000").fetchall()
    cols = rows[0].keys() if rows else [
        c["name"] for c in db.execute(f"PRAGMA table_info({name})")
    ]
    return render_template("rows.html", title=name, columns=cols, rows=rows, source=name)


if __name__ == "__main__":
    app.run(host="127.0.0.1", port=PORT, debug=False, use_reloader=False)
