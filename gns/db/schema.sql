-- Grimoire & Steel game database (gns.db) schema.
--
-- Single source of truth for the G&S ruleset content: traits, kin, callings,
-- trainings, weapons, armor, monsters, spells, advancement, plus the full rules
-- bible (sections) with full-text search. Loaded by gns/build/load_gns.py from
-- gns/data/*.json. Idempotent: this script drops and recreates everything.
--
-- The C++ game (game/) opens this DB read-only as reference data; the Flask app
-- (gns/app/app.py) browses it. Tables use STRICT typing.

PRAGMA foreign_keys = OFF;

-- Drop in reverse-dependency order (children, then parents, then FTS).
DROP TRIGGER IF EXISTS section_au;
DROP TRIGGER IF EXISTS section_ad;
DROP TRIGGER IF EXISTS section_ai;
DROP VIEW IF EXISTS v_advancement_table;
DROP VIEW IF EXISTS v_calling_summary;
DROP VIEW IF EXISTS v_monster_summary;
DROP VIEW IF EXISTS v_spell_summary;
DROP TABLE IF EXISTS rules_search;
DROP TABLE IF EXISTS section_fts;
DROP TABLE IF EXISTS section;
DROP TABLE IF EXISTS character_sheet;
DROP TABLE IF EXISTS level_improvement_option;
DROP TABLE IF EXISTS module_completion_award;
DROP TABLE IF EXISTS advancement_level;
DROP TABLE IF EXISTS magic_weapon_example;
DROP TABLE IF EXISTS spell;
DROP TABLE IF EXISTS monster;
DROP TABLE IF EXISTS challenge_number;
DROP TABLE IF EXISTS armor;
DROP TABLE IF EXISTS weapon_category;
DROP TABLE IF EXISTS calling_weapon_option;
DROP TABLE IF EXISTS calling_training_option;
DROP TABLE IF EXISTS calling;
DROP TABLE IF EXISTS training;
DROP TABLE IF EXISTS kin;
DROP TABLE IF EXISTS trait;
DROP TABLE IF EXISTS game;

-- ---------------------------------------------------------------------------
-- Game metadata (single row).
-- ---------------------------------------------------------------------------
CREATE TABLE game (
    game_id INTEGER PRIMARY KEY CHECK (game_id = 1),
    title TEXT NOT NULL UNIQUE,
    abbreviation TEXT NOT NULL UNIQUE,
    version TEXT NOT NULL,
    source_file TEXT NOT NULL,
    source_sha256 TEXT NOT NULL,
    created_utc TEXT NOT NULL,
    notes TEXT NOT NULL
) STRICT;

-- ---------------------------------------------------------------------------
-- Core character building blocks.
-- ---------------------------------------------------------------------------
CREATE TABLE trait (
    trait_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT NOT NULL,
    sort_order INTEGER NOT NULL UNIQUE
) STRICT;

CREATE TABLE kin (
    kin_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT NOT NULL,
    gift_name TEXT NOT NULL,
    gift_description TEXT NOT NULL,
    sort_order INTEGER NOT NULL UNIQUE
) STRICT;

CREATE TABLE training (
    training_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT,
    sort_order INTEGER NOT NULL UNIQUE
) STRICT;

CREATE TABLE calling (
    calling_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT NOT NULL,
    starting_gear TEXT NOT NULL,
    armor_allowed TEXT NOT NULL,
    gift_name TEXT NOT NULL,
    gift_description TEXT NOT NULL,
    sort_order INTEGER NOT NULL UNIQUE
) STRICT;

CREATE TABLE calling_training_option (
    calling_id INTEGER NOT NULL REFERENCES calling(calling_id) ON DELETE CASCADE,
    training_id INTEGER NOT NULL REFERENCES training(training_id) ON DELETE RESTRICT,
    PRIMARY KEY (calling_id, training_id)
) STRICT;

CREATE TABLE calling_weapon_option (
    calling_id INTEGER NOT NULL REFERENCES calling(calling_id) ON DELETE CASCADE,
    weapon_name TEXT NOT NULL,
    PRIMARY KEY (calling_id, weapon_name)
) STRICT;

-- ---------------------------------------------------------------------------
-- Equipment & resolution tables.
-- ---------------------------------------------------------------------------
CREATE TABLE weapon_category (
    weapon_category_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    damage_die TEXT NOT NULL,
    examples TEXT NOT NULL,
    sort_order INTEGER NOT NULL UNIQUE
) STRICT;

CREATE TABLE armor (
    armor_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    defense_bonus INTEGER NOT NULL,
    notes TEXT,
    sort_order INTEGER NOT NULL UNIQUE
) STRICT;

CREATE TABLE challenge_number (
    challenge_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    target_number INTEGER NOT NULL,
    description TEXT,
    sort_order INTEGER NOT NULL UNIQUE,
    CHECK (target_number > 0)
) STRICT;

-- ---------------------------------------------------------------------------
-- Bestiary & spells.
-- ---------------------------------------------------------------------------
CREATE TABLE monster (
    monster_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT NOT NULL,
    life INTEGER NOT NULL,
    defense INTEGER NOT NULL,
    attack_bonus INTEGER NOT NULL,
    damage TEXT NOT NULL,
    ap_value INTEGER NOT NULL,
    special_rule TEXT,
    sort_order INTEGER NOT NULL UNIQUE,
    CHECK (life > 0),
    CHECK (defense > 0),
    CHECK (ap_value >= 0)
) STRICT;

CREATE INDEX idx_monster_ap_value ON monster(ap_value);
CREATE INDEX idx_monster_defense ON monster(defense);

CREATE TABLE spell (
    spell_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    description TEXT NOT NULL,
    combat_effect TEXT,
    challenge_number INTEGER,
    notes TEXT,
    sort_order INTEGER NOT NULL UNIQUE,
    CHECK (challenge_number IS NULL OR challenge_number > 0)
) STRICT;

CREATE TABLE magic_weapon_example (
    magic_weapon_id INTEGER PRIMARY KEY,
    name TEXT NOT NULL UNIQUE,
    bonus INTEGER NOT NULL,
    description TEXT,
    CHECK (bonus BETWEEN 1 AND 3)
) STRICT;

-- ---------------------------------------------------------------------------
-- Advancement.
-- ---------------------------------------------------------------------------
CREATE TABLE advancement_level (
    advancement_id INTEGER PRIMARY KEY,
    calling_id INTEGER NOT NULL REFERENCES calling(calling_id) ON DELETE CASCADE,
    level INTEGER NOT NULL,
    ap_required INTEGER NOT NULL,
    UNIQUE (calling_id, level),
    CHECK (level >= 1),
    CHECK (ap_required >= 0)
) STRICT;

CREATE TABLE module_completion_award (
    award_id INTEGER PRIMARY KEY,
    module_type TEXT NOT NULL UNIQUE,
    ap_award INTEGER NOT NULL,
    description TEXT,
    sort_order INTEGER NOT NULL UNIQUE,
    CHECK (ap_award >= 0)
) STRICT;

CREATE TABLE level_improvement_option (
    option_id INTEGER PRIMARY KEY,
    description TEXT NOT NULL UNIQUE,
    sort_order INTEGER NOT NULL UNIQUE
) STRICT;

-- ---------------------------------------------------------------------------
-- Reference handouts & rules text.
-- ---------------------------------------------------------------------------
CREATE TABLE character_sheet (
    sheet_id INTEGER PRIMARY KEY,
    sheet_type TEXT NOT NULL UNIQUE CHECK (sheet_type IN ('sample', 'blank')),
    title TEXT NOT NULL,
    body_markdown TEXT NOT NULL
) STRICT;

CREATE TABLE section (
    section_id INTEGER PRIMARY KEY,
    section_number INTEGER,
    title TEXT NOT NULL,
    heading TEXT NOT NULL,
    slug TEXT NOT NULL UNIQUE,
    body_markdown TEXT NOT NULL,
    sort_order INTEGER NOT NULL UNIQUE,
    CHECK (sort_order > 0)
) STRICT;

-- Full-text index over the rules-bible sections, kept in sync by triggers.
CREATE VIRTUAL TABLE section_fts USING fts5(
    title,
    heading,
    body_markdown,
    content='section',
    content_rowid='section_id',
    tokenize='unicode61 remove_diacritics 2'
);

CREATE TRIGGER section_ai AFTER INSERT ON section BEGIN
    INSERT INTO section_fts(rowid, title, heading, body_markdown)
    VALUES (new.section_id, new.title, new.heading, new.body_markdown);
END;

CREATE TRIGGER section_ad AFTER DELETE ON section BEGIN
    INSERT INTO section_fts(section_fts, rowid, title, heading, body_markdown)
    VALUES ('delete', old.section_id, old.title, old.heading, old.body_markdown);
END;

CREATE TRIGGER section_au AFTER UPDATE ON section BEGIN
    INSERT INTO section_fts(section_fts, rowid, title, heading, body_markdown)
    VALUES ('delete', old.section_id, old.title, old.heading, old.body_markdown);
    INSERT INTO section_fts(rowid, title, heading, body_markdown)
    VALUES (new.section_id, new.title, new.heading, new.body_markdown);
END;

-- Cross-content full-text search (sections + monsters + spells + ...). This is a
-- standalone (non-external-content) FTS table populated directly by the loader.
CREATE VIRTUAL TABLE rules_search USING fts5(
    category,
    name,
    text,
    tokenize='unicode61 remove_diacritics 2'
);

-- ---------------------------------------------------------------------------
-- Convenience views.
-- ---------------------------------------------------------------------------
CREATE VIEW v_advancement_table AS
SELECT
    c.name AS calling,
    a.level,
    a.ap_required
FROM advancement_level a
JOIN calling c ON c.calling_id = a.calling_id
ORDER BY c.sort_order, a.level;

CREATE VIEW v_calling_summary AS
SELECT
    c.name,
    c.description,
    c.armor_allowed,
    c.starting_gear,
    c.gift_name,
    c.gift_description,
    GROUP_CONCAT(t.name, ', ') AS training_options
FROM calling c
LEFT JOIN calling_training_option cto ON c.calling_id = cto.calling_id
LEFT JOIN training t ON cto.training_id = t.training_id
GROUP BY c.calling_id
ORDER BY c.sort_order;

CREATE VIEW v_monster_summary AS
SELECT
    name,
    life,
    defense,
    attack_bonus,
    damage,
    ap_value,
    special_rule
FROM monster
ORDER BY sort_order;

CREATE VIEW v_spell_summary AS
SELECT
    name,
    description,
    combat_effect,
    challenge_number,
    notes
FROM spell
ORDER BY sort_order;
