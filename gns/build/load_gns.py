"""Build gns.db (Grimoire & Steel game database) — Spells module.

Applies gns/db/schema.sql, then loads the curated spell data from
gns/data/{magic_user_spells,cleric_spells}.json, including each spell's
optional detail mini-tables. Idempotent: the schema drops/recreates tables.

    py gns/build/load_gns.py
"""
import json
import re
import sqlite3
import sys
from pathlib import Path

_TREASURE_CODE_RE = re.compile(r"\b([A-T])\b")


def parse_treasure_code(text):
    """Return the first A-T treasure-type code in the text (e.g. 'C + 1000 gp'
    -> 'C', 'Individuals L, M; ...' -> 'L'), or None for nil/variable/none."""
    if not text:
        return None
    m = _TREASURE_CODE_RE.search(text)
    return m.group(1) if m else None

ROOT = Path(__file__).resolve().parent.parent          # the gns/ directory
SCHEMA = ROOT / "db" / "schema.sql"
DATA = ROOT / "data"
DB = ROOT / "gns.db"

# Columns shared by both spell tables.
COMMON_COLS = [
    "name", "spell_level", "range_text", "range_feet",
    "duration_text", "duration_turns", "description", "is_described",
    "source_pdf_index", "source_printed_page",
]


def insert_spell(conn, table, spell, extra_cols):
    cols = COMMON_COLS + extra_cols
    placeholders = ", ".join("?" for _ in cols)
    values = [spell.get(c) for c in cols]
    cur = conn.execute(
        f"INSERT INTO {table} ({', '.join(cols)}) VALUES ({placeholders})", values
    )
    spell_id = cur.lastrowid
    insert_detail_tables(conn, table, spell_id, spell.get("detail_tables") or [])
    return spell_id


def insert_detail_tables(conn, table, spell_id, detail_tables):
    fk = "mu_spell_id" if table == "magic_user_spells" else "cleric_spell_id"
    for dt in detail_tables:
        cur = conn.execute(
            f"INSERT INTO spell_detail_tables ({fk}, caption) VALUES (?, ?)",
            (spell_id, dt["caption"]),
        )
        dt_id = cur.lastrowid
        rows = [
            (dt_id, i, r.get("key_low"), r.get("key_high"),
             r.get("key_label"), r["value_text"])
            for i, r in enumerate(dt.get("rows") or [])
        ]
        conn.executemany(
            "INSERT INTO spell_detail_rows "
            "(detail_table_id, row_order, key_low, key_high, key_label, value_text) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            rows,
        )


MONSTER_COLS = [
    "name", "parent_id", "is_group", "is_described",
    "move_text", "hit_dice_text", "armor_class_text", "treasure_type", "treasure_type_code",
    "alignment_text", "attacks_text", "damage_text",
    "size_text", "lair_text", "breath_weapon_text", "breath_range_text", "special_text",
    "hit_dice_num", "armor_class_num", "description",
    "source_pdf_index", "source_printed_page",
]


def insert_monster(conn, obj, parent_id, is_group, is_described):
    row = {c: obj.get(c) for c in MONSTER_COLS}
    row["parent_id"] = parent_id
    row["is_group"] = is_group
    row["is_described"] = is_described
    row["treasure_type_code"] = parse_treasure_code(obj.get("treasure_type"))
    placeholders = ", ".join("?" for _ in MONSTER_COLS)
    cur = conn.execute(
        f"INSERT INTO monsters ({', '.join(MONSTER_COLS)}) VALUES ({placeholders})",
        [row[c] for c in MONSTER_COLS],
    )
    return cur.lastrowid


def insert_monster_detail_tables(conn, monster_id, detail_tables):
    for dt in detail_tables:
        cur = conn.execute(
            "INSERT INTO monster_detail_tables (monster_id, caption) VALUES (?, ?)",
            (monster_id, dt["caption"]),
        )
        tid = cur.lastrowid
        rows = [
            (tid, i, r.get("key_low"), r.get("key_high"), r.get("key_label"), r["value_text"])
            for i, r in enumerate(dt.get("rows") or [])
        ]
        conn.executemany(
            "INSERT INTO monster_detail_rows "
            "(detail_table_id, row_order, key_low, key_high, key_label, value_text) "
            "VALUES (?, ?, ?, ?, ?, ?)",
            rows,
        )


def load_monsters(conn, monsters):
    n_top = n_var = 0
    name_id = {}
    for m in monsters:
        is_group = int(m.get("is_group", 0) or 0)
        mid = insert_monster(conn, m, parent_id=None, is_group=is_group, is_described=1)
        name_id[m["name"]] = mid
        n_top += 1
        for v in m.get("variants") or []:
            v = dict(v)
            v.setdefault("source_pdf_index", m.get("source_pdf_index"))
            v.setdefault("source_printed_page", m.get("source_printed_page"))
            name_id[v["name"]] = insert_monster(conn, v, parent_id=mid, is_group=0, is_described=0)
            n_var += 1
        insert_monster_detail_tables(conn, mid, m.get("detail_tables") or [])
    return n_top, n_var, name_id


def load_treasure(conn, data):
    """Load the treasure-type table and the magic-item categories + items."""
    for t in data.get("treasure_types") or []:
        conn.execute(
            "INSERT INTO treasure_type (code, copper, silver, electrum, gold, platinum, "
            "gems_jewelry, maps_magic, notes) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (t["code"], t.get("copper"), t.get("silver"), t.get("electrum"), t.get("gold"),
             t.get("platinum"), t.get("gems_jewelry"), t.get("maps_magic"), t.get("notes")),
        )
    cat_id = {}
    for c in data.get("magic_item_categories") or []:
        cur = conn.execute(
            "INSERT INTO magic_item_category (name, die_low, die_high, description) "
            "VALUES (?, ?, ?, ?)",
            (c["name"], c.get("die_low"), c.get("die_high"), c.get("description")),
        )
        cat_id[c["name"]] = cur.lastrowid
    n_items = 0
    for catname, rows in (data.get("magic_items") or {}).items():
        for row in rows:
            die, name = row[0], row[1]
            desc = row[2] if len(row) > 2 else None
            conn.execute(
                "INSERT INTO magic_item (category_id, die_roll, name, description) "
                "VALUES (?, ?, ?, ?)",
                (cat_id.get(catname), die, name, desc),
            )
            n_items += 1
    return {"treasure_types": len(data.get("treasure_types") or []),
            "categories": len(cat_id), "items": n_items}


def load_environments(conn, data, monster_id):
    """Load environments and the wandering-monster table (monsters by name)."""
    env_id = {}
    for e in data.get("environments") or []:
        cur = conn.execute(
            "INSERT INTO environment (name, description) VALUES (?, ?)",
            (e["name"], e.get("description")),
        )
        env_id[e["name"]] = cur.lastrowid
    n = 0
    for w in data.get("wandering_monsters") or []:
        mid = monster_id.get(w["monster"])
        if mid is None:
            raise ValueError(f"wandering_monster references unknown monster: {w['monster']!r}")
        conn.execute(
            "INSERT INTO wandering_monster (environment_id, monster_id, party_level_min, "
            "party_level_max, number_min, number_max, weight, notes) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
            (env_id.get(w["environment"]), mid, w.get("party_level_min", 1),
             w.get("party_level_max", 3), w.get("number_min"), w.get("number_max"),
             w.get("weight", 1), w.get("notes")),
        )
        n += 1
    return {"environments": len(env_id), "wandering": n}


def load_rules(conn, data):
    """Load the 1978 reference/rules tables (engine math)."""
    for a in data.get("ability_adjustments") or []:
        conn.execute(
            "INSERT INTO ability_adjustment (applies_to, score_low, score_high, modifier, effect)"
            " VALUES (?, ?, ?, ?, ?)",
            (a["applies_to"], a.get("score_low"), a.get("score_high"), a.get("modifier"),
             a.get("effect")),
        )
    n_attack = 0
    for kind, rows in (data.get("attack_table") or {}).items():
        for i, r in enumerate(rows):
            for ac, roll in r["ac"].items():
                conn.execute(
                    "INSERT INTO attack_table (attacker_kind, attacker_label, sort_order, "
                    "target_ac, roll_needed) VALUES (?, ?, ?, ?, ?)",
                    (kind, r["label"], i, int(ac), roll),
                )
                n_attack += 1
    for s in data.get("saving_throws") or []:
        conn.execute(
            "INSERT INTO saving_throw (class_group, vs_spell_staff, vs_magic_wand, "
            "vs_death_poison, vs_stone, vs_dragon_breath) VALUES (?, ?, ?, ?, ?, ?)",
            (s["class_group"], s.get("vs_spell_staff"), s.get("vs_magic_wand"),
             s.get("vs_death_poison"), s.get("vs_stone"), s.get("vs_dragon_breath")),
        )
    for m in data.get("monster_xp") or []:
        conn.execute(
            "INSERT INTO monster_xp (hit_dice, value, special_ability_bonus) VALUES (?, ?, ?)",
            (m["hit_dice"], m["value"], m.get("special_ability_bonus")),
        )
    for t in data.get("thief_skills") or []:
        conn.execute(
            "INSERT INTO thief_skill (level, skill, chance_pct, chance_text) VALUES (?, ?, ?, ?)",
            (t["level"], t["skill"], t.get("chance_pct"), t.get("chance_text")),
        )
    for u in data.get("turn_undead") or []:
        conn.execute(
            "INSERT INTO turn_undead (cleric_level, undead_type, undead_order, result)"
            " VALUES (?, ?, ?, ?)",
            (u["cleric_level"], u["undead_type"], u.get("undead_order"), u["result"]),
        )
    for mi in data.get("magic_user_intelligence") or []:
        conn.execute(
            "INSERT INTO magic_user_intelligence (int_low, int_high, pct_to_know, min_spells, "
            "max_spells) VALUES (?, ?, ?, ?, ?)",
            (mi.get("int_low"), mi.get("int_high"), mi.get("pct_to_know"),
             mi.get("min_spells"), mi.get("max_spells")),
        )
    for r in data.get("reaction_table") or []:
        conn.execute(
            "INSERT INTO reaction_table (roll_low, roll_high, reaction) VALUES (?, ?, ?)",
            (r.get("roll_low"), r.get("roll_high"), r.get("reaction")),
        )
    for lg in data.get("languages") or []:
        conn.execute(
            "INSERT INTO language (name, kind, description) VALUES (?, ?, ?)",
            (lg["name"], lg.get("kind"), lg.get("description")),
        )
    for c in data.get("coins") or []:
        conn.execute(
            "INSERT INTO coin (name, abbreviation, value_gp, notes) VALUES (?, ?, ?, ?)",
            (c["name"], c.get("abbreviation"), c.get("value_gp"), c.get("notes")),
        )
    for v in data.get("treasure_values") or []:
        conn.execute(
            "INSERT INTO treasure_value (kind, roll_low, roll_high, value_gp, dice, notes)"
            " VALUES (?, ?, ?, ?, ?, ?)",
            (v["kind"], v.get("roll_low"), v.get("roll_high"), v.get("value_gp"),
             v.get("dice"), v.get("notes")),
        )
    return {
        "ability_adjustments": len(data.get("ability_adjustments") or []),
        "attack_rows": n_attack,
        "saving_throws": len(data.get("saving_throws") or []),
        "monster_xp": len(data.get("monster_xp") or []),
        "thief_skills": len(data.get("thief_skills") or []),
        "turn_undead": len(data.get("turn_undead") or []),
        "languages": len(data.get("languages") or []),
    }


def load_equipment(conn, data):
    """Load weapons, armor, armor classes, equipment and strength capacity.
    Returns name->id maps for weapon/armor/equipment (for inventory linking)."""
    maps = {"weapon": {}, "armor": {}, "equipment": {}}
    for w in data.get("weapons") or []:
        cur = conn.execute(
            "INSERT INTO weapon (name, category, cost_gp, weight_lbs, damage, "
            "damage_min, damage_max, two_handed, cleric_usable, "
            "range_short_ft, range_medium_ft, range_long_ft, notes) "
            "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
            (w["name"], w.get("category"), w.get("cost_gp"), w.get("weight_lbs"),
             w.get("damage"), w.get("damage_min"), w.get("damage_max"),
             int(w.get("two_handed", 0) or 0), int(w.get("cleric_usable", 0) or 0),
             w.get("range_short_ft"), w.get("range_medium_ft"), w.get("range_long_ft"),
             w.get("notes")),
        )
        maps["weapon"][w["name"]] = cur.lastrowid
    for a in data.get("armor") or []:
        cur = conn.execute(
            "INSERT INTO armor (name, armor_class, ac_modifier, cost_gp, weight_lbs, "
            "is_shield, notes) VALUES (?, ?, ?, ?, ?, ?, ?)",
            (a["name"], a.get("armor_class"), a.get("ac_modifier"), a.get("cost_gp"),
             a.get("weight_lbs"), int(a.get("is_shield", 0) or 0), a.get("notes")),
        )
        maps["armor"][a["name"]] = cur.lastrowid
    for ac in data.get("armor_classes") or []:
        conn.execute(
            "INSERT INTO armor_class (armor_class, armor_type, description) VALUES (?, ?, ?)",
            (ac["armor_class"], ac["armor_type"], ac.get("description")),
        )
    for e in data.get("equipment") or []:
        cur = conn.execute(
            "INSERT INTO equipment (name, category, cost_gp, weight_lbs, "
            "light_radius_ft, light_duration_turns, notes) VALUES (?, ?, ?, ?, ?, ?, ?)",
            (e["name"], e.get("category"), e.get("cost_gp"), e.get("weight_lbs"),
             e.get("light_radius_ft"), e.get("light_duration_turns"), e.get("notes")),
        )
        maps["equipment"][e["name"]] = cur.lastrowid
    for s in data.get("strength_capacity") or []:
        conn.execute(
            "INSERT INTO strength_capacity (strength_score, base_capacity_lbs, "
            "max_capacity_lbs, notes) VALUES (?, ?, ?, ?)",
            (s["strength_score"], s["base_capacity_lbs"], s["max_capacity_lbs"], s.get("notes")),
        )
    maps["counts"] = {
        "weapons": len(data.get("weapons") or []),
        "armor": len(data.get("armor") or []),
        "armor_classes": len(data.get("armor_classes") or []),
        "equipment": len(data.get("equipment") or []),
        "strength_capacity": len(data.get("strength_capacity") or []),
    }
    return maps


def load_characters(conn, data, item_maps):
    """Load the character module: abilities, alignments, races, classes
    (base then sub-classes), and any sample character sheets + scores.
    Foreign keys in the JSON are given by name and resolved here."""
    ability_id = {}
    for i, a in enumerate(data.get("abilities") or []):
        cur = conn.execute(
            "INSERT INTO ability (name, abbreviation, sort_order, description) "
            "VALUES (?, ?, ?, ?)",
            (a["name"], a.get("abbreviation"), i + 1, a.get("description")),
        )
        ability_id[a["name"]] = cur.lastrowid

    alignment_id = {}
    for al in data.get("alignments") or []:
        cur = conn.execute(
            "INSERT INTO alignment (name, abbreviation, description) VALUES (?, ?, ?)",
            (al["name"], al.get("abbreviation"), al.get("description")),
        )
        alignment_id[al["name"]] = cur.lastrowid

    race_id = {}
    for r in data.get("races") or []:
        cur = conn.execute(
            "INSERT INTO race (name, restrictions, special_ability, description, "
            "carry_capacity_modifier, base_movement_ft) VALUES (?, ?, ?, ?, ?, ?)",
            (r["name"], r.get("restrictions"), r.get("special_ability"), r.get("description"),
             r.get("carry_capacity_modifier", 1.0), r.get("base_movement_ft", 120)),
        )
        race_id[r["name"]] = cur.lastrowid

    # Base classes first so sub-classes can resolve parent_class_id by name.
    class_id = {}
    classes = data.get("classes") or []
    for c in sorted(classes, key=lambda c: c.get("parent") is not None):
        cur = conn.execute(
            "INSERT INTO character_class "
            "(name, parent_class_id, prime_requisite_ability_id, restrictions, "
            " special_abilities, description) VALUES (?, ?, ?, ?, ?, ?)",
            (c["name"], class_id.get(c.get("parent")),
             ability_id.get(c.get("prime_requisite")),
             c.get("restrictions"), c.get("special_abilities"), c.get("description")),
        )
        class_id[c["name"]] = cur.lastrowid

    for cl in data.get("class_levels") or []:
        conn.execute(
            "INSERT INTO class_level (class_id, level, title, experience_points, "
            "hit_dice, spells) VALUES (?, ?, ?, ?, ?, ?)",
            (class_id.get(cl["class"]), cl["level"], cl.get("title"),
             cl["experience_points"], cl.get("hit_dice"), cl.get("spells")),
        )

    inv_col = {"weapon": "weapon_id", "armor": "armor_id", "equipment": "equipment_id"}
    for cs in data.get("character_sheets") or []:
        cur = conn.execute(
            "INSERT INTO character_sheet ("
            "character_name, player_name, dungeon_master, race_id, class_id, alignment_id, "
            "level, armor_class, hit_points, max_hit_points, base_movement_ft, "
            "experience_points, experience_to_next_level, earned_xp_bonus_pct, gold_pieces, "
            "save_poison_death, save_magic_wand, save_stone_paralysis, save_dragon_breath, "
            "save_spells_staff, special_abilities, special_skills, magic_items, normal_items, "
            "treasure_notes, character_sketch, other_notes) "
            "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)",
            (cs["character_name"], cs.get("player_name"), cs.get("dungeon_master"),
             race_id.get(cs.get("race")), class_id.get(cs.get("class")),
             alignment_id.get(cs.get("alignment")),
             cs.get("level"), cs.get("armor_class"), cs.get("hit_points"),
             cs.get("max_hit_points"), cs.get("base_movement_ft"), cs.get("experience_points"),
             cs.get("experience_to_next_level"), cs.get("earned_xp_bonus_pct"),
             cs.get("gold_pieces"), cs.get("save_poison_death"), cs.get("save_magic_wand"),
             cs.get("save_stone_paralysis"), cs.get("save_dragon_breath"),
             cs.get("save_spells_staff"), cs.get("special_abilities"), cs.get("special_skills"),
             cs.get("magic_items"), cs.get("normal_items"), cs.get("treasure_notes"),
             cs.get("character_sketch"), cs.get("other_notes")),
        )
        sheet_id = cur.lastrowid
        for ab in cs.get("abilities") or []:
            conn.execute(
                "INSERT INTO character_ability "
                "(character_sheet_id, ability_id, score, adjustment_text) VALUES (?, ?, ?, ?)",
                (sheet_id, ability_id.get(ab["ability"]), ab.get("score"),
                 ab.get("adjustment_text")),
            )
        for inv in cs.get("inventory") or []:
            col = inv_col[inv["type"]]
            item_id = item_maps.get(inv["type"], {}).get(inv["name"])
            conn.execute(
                f"INSERT INTO character_inventory "
                f"(character_sheet_id, {col}, quantity, notes) VALUES (?, ?, ?, ?)",
                (sheet_id, item_id, inv.get("quantity", 1), inv.get("notes")),
            )
    return {
        "abilities": len(ability_id), "alignments": len(alignment_id),
        "races": len(race_id), "classes": len(class_id),
        "class_levels": len(data.get("class_levels") or []),
        "sheets": len(data.get("character_sheets") or []),
    }


def load_json(path):
    return json.loads(path.read_text(encoding="utf-8"))["spells"]


def main() -> int:
    for p in (SCHEMA, DATA / "magic_user_spells.json", DATA / "cleric_spells.json",
              DATA / "monsters.json", DATA / "characters.json", DATA / "equipment.json",
              DATA / "treasure.json", DATA / "environments.json", DATA / "rules.json"):
        if not p.exists():
            print(f"Missing required file: {p}", file=sys.stderr)
            return 1

    mu = load_json(DATA / "magic_user_spells.json")
    cleric = load_json(DATA / "cleric_spells.json")
    monsters = json.loads((DATA / "monsters.json").read_text(encoding="utf-8"))["monsters"]
    characters = json.loads((DATA / "characters.json").read_text(encoding="utf-8"))
    equipment = json.loads((DATA / "equipment.json").read_text(encoding="utf-8"))
    treasure = json.loads((DATA / "treasure.json").read_text(encoding="utf-8"))
    environments = json.loads((DATA / "environments.json").read_text(encoding="utf-8"))
    rules = json.loads((DATA / "rules.json").read_text(encoding="utf-8"))

    if DB.exists():
        DB.unlink()
    conn = sqlite3.connect(DB)
    try:
        conn.execute("PRAGMA foreign_keys = ON")
        conn.executescript(SCHEMA.read_text(encoding="utf-8"))

        for s in mu:
            insert_spell(conn, "magic_user_spells", s, [])
        for s in cleric:
            insert_spell(conn, "cleric_spells", s, ["is_reversible", "reversed_name"])

        tstats = load_treasure(conn, treasure)
        n_top, n_var, monster_id = load_monsters(conn, monsters)
        item_maps = load_equipment(conn, equipment)
        cstats = load_characters(conn, characters, item_maps)
        estats = load_environments(conn, environments, monster_id)
        rstats = load_rules(conn, rules)

        conn.commit()
    finally:
        conn.close()

    ec = item_maps["counts"]
    print(f"Built {DB}")
    print(f"  magic_user_spells: {len(mu)}")
    print(f"  cleric_spells:     {len(cleric)}")
    print(f"  monsters:          {n_top} top-level + {n_var} variants = {n_top + n_var}")
    print(f"  treasure:          {tstats['treasure_types']} types, "
          f"{tstats['categories']} magic categories, {tstats['items']} magic items")
    print(f"  characters:        {cstats['abilities']} abilities, {cstats['alignments']} alignments, "
          f"{cstats['races']} races, {cstats['classes']} classes, "
          f"{cstats['class_levels']} class-levels, {cstats['sheets']} sheet(s)")
    print(f"  equipment:         {ec['weapons']} weapons, {ec['armor']} armor, "
          f"{ec['armor_classes']} armor-classes, {ec['equipment']} equipment, "
          f"{ec['strength_capacity']} strength rows")
    print(f"  environments:      {estats['environments']} environments, "
          f"{estats['wandering']} wandering-monster entries")
    print(f"  rules:             {rstats['ability_adjustments']} ability-adj, "
          f"{rstats['attack_rows']} attack rows, {rstats['saving_throws']} save groups, "
          f"{rstats['monster_xp']} monster-xp, {rstats['thief_skills']} thief skills, "
          f"{rstats['turn_undead']} turn-undead, {rstats['languages']} languages")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
