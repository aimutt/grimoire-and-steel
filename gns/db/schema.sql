-- Grimoire & Steel game database (gns.db) — Spells module.
--
-- Design notes:
--   * Magic-User and Cleric spells live in SEPARATE tables: although they share
--     attributes, the same-named spell (e.g. Hold Person, Light, Detect Evil)
--     differs by class in level/range/duration, so a single table would be wrong.
--   * Verbatim source text is preserved in *_text / description columns. Numeric
--     columns (range_feet, duration_turns) are DERIVED conveniences, NULL where the
--     source value is variable/level-based/special.
--   * A spell may own captioned mini-tables (e.g. Charm Person's re-save schedule),
--     normalized into spell_detail_tables (one per mini-table) + spell_detail_rows.

PRAGMA foreign_keys = ON;

DROP VIEW  IF EXISTS all_spells;
DROP TABLE IF EXISTS spell_detail_rows;
DROP TABLE IF EXISTS spell_detail_tables;
DROP TABLE IF EXISTS cleric_spells;
DROP TABLE IF EXISTS magic_user_spells;

CREATE TABLE magic_user_spells (
    id                  INTEGER PRIMARY KEY,
    name                TEXT    NOT NULL UNIQUE,
    spell_level         INTEGER NOT NULL CHECK (spell_level BETWEEN 1 AND 3),
    range_text          TEXT,                 -- verbatim, e.g. "120 feet", "0"
    range_feet          INTEGER,              -- derived; NULL if variable/level-based
    duration_text       TEXT,                 -- verbatim; NULL if none stated
    duration_turns      INTEGER,              -- derived; NULL if variable/special/infinite
    description         TEXT,                 -- verbatim; NULL for name-only stubs
    is_described        INTEGER NOT NULL DEFAULT 1 CHECK (is_described IN (0, 1)),
    source_pdf_index    INTEGER,
    source_printed_page INTEGER
);

CREATE TABLE cleric_spells (
    id                  INTEGER PRIMARY KEY,
    name                TEXT    NOT NULL UNIQUE,
    spell_level         INTEGER NOT NULL CHECK (spell_level BETWEEN 1 AND 3),
    range_text          TEXT,
    range_feet          INTEGER,
    duration_text       TEXT,
    duration_turns      INTEGER,
    description         TEXT,
    is_described        INTEGER NOT NULL DEFAULT 1 CHECK (is_described IN (0, 1)),
    is_reversible       INTEGER NOT NULL DEFAULT 0 CHECK (is_reversible IN (0, 1)),
    reversed_name       TEXT,                 -- evil-cleric form; NULL unless reversible
    source_pdf_index    INTEGER,
    source_printed_page INTEGER,
    CHECK ((is_reversible = 1) = (reversed_name IS NOT NULL))
);

-- A captioned mini-table belonging to exactly one spell. Two nullable FKs + a
-- CHECK give real referential integrity for this polymorphic association.
CREATE TABLE spell_detail_tables (
    id              INTEGER PRIMARY KEY,
    mu_spell_id     INTEGER REFERENCES magic_user_spells(id) ON DELETE CASCADE,
    cleric_spell_id INTEGER REFERENCES cleric_spells(id)     ON DELETE CASCADE,
    caption         TEXT NOT NULL,
    CHECK ((mu_spell_id IS NOT NULL) + (cleric_spell_id IS NOT NULL) = 1)
);

-- Ordered rows of a mini-table. key_low/key_high give the numeric range the row
-- applies to (e.g. Intelligence 3-6, Hit Dice up to 1); key_label/value_text are
-- the verbatim key and value as printed.
CREATE TABLE spell_detail_rows (
    id              INTEGER PRIMARY KEY,
    detail_table_id INTEGER NOT NULL REFERENCES spell_detail_tables(id) ON DELETE CASCADE,
    row_order       INTEGER NOT NULL,
    key_low         INTEGER,
    key_high        INTEGER,
    key_label       TEXT,
    value_text      TEXT NOT NULL
);

CREATE INDEX idx_mu_level     ON magic_user_spells(spell_level);
CREATE INDEX idx_cleric_level ON cleric_spells(spell_level);
CREATE INDEX idx_detail_rows  ON spell_detail_rows(detail_table_id, row_order);

-- Convenience read-only union of both spell tables with a class discriminator.
-- Storage stays split; this is only for cross-class queries.
CREATE VIEW all_spells AS
    SELECT 'magic-user' AS spell_class, id, name, spell_level,
           range_text, range_feet, duration_text, duration_turns,
           description, is_described,
           0 AS is_reversible, NULL AS reversed_name,
           source_pdf_index, source_printed_page
    FROM magic_user_spells
    UNION ALL
    SELECT 'cleric' AS spell_class, id, name, spell_level,
           range_text, range_feet, duration_text, duration_turns,
           description, is_described,
           is_reversible, reversed_name,
           source_pdf_index, source_printed_page
    FROM cleric_spells;


-- =====================================================================
-- Treasure module (TREASURE section, printed pages 34-38). treasure_type is
-- the A-T Treasure Types Table; magic_item_category + magic_item hold the
-- Magic Items Die Roll system and the magic-item descriptions.
-- =====================================================================

DROP TABLE IF EXISTS magic_item;
DROP TABLE IF EXISTS magic_item_category;
DROP TABLE IF EXISTS treasure_type;

-- Each cell is the verbatim "amount:probability" text from the table.
CREATE TABLE treasure_type (
    id           INTEGER PRIMARY KEY,
    code         TEXT NOT NULL UNIQUE,   -- A .. T
    copper       TEXT,
    silver       TEXT,
    electrum     TEXT,
    gold         TEXT,
    platinum     TEXT,
    gems_jewelry TEXT,
    maps_magic   TEXT,
    notes        TEXT
);

-- The 7 magic-item categories (percentile ranges from the Magic Items Die Roll)
-- with the general explanation text for that category.
CREATE TABLE magic_item_category (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    die_low     INTEGER,   -- percentile 1-100
    die_high    INTEGER,
    description TEXT
);

-- A specific magic item from a category's 1-0 (d20) sub-table, with its
-- description where the rules give one.
CREATE TABLE magic_item (
    id          INTEGER PRIMARY KEY,
    category_id INTEGER NOT NULL REFERENCES magic_item_category(id) ON DELETE CASCADE,
    die_roll    INTEGER,   -- 1-10 within the sub-table (printed "0" = 10)
    name        TEXT NOT NULL,
    description TEXT
);

CREATE INDEX idx_magic_item_cat ON magic_item(category_id, die_roll);


-- =====================================================================
-- Monsters module
--
-- Design notes:
--   * One `monsters` table holds both book entries and the sub-types promoted
--     from a parent's variation table (Dragon colors, Giant types, were-forms,
--     Spider sizes). A sub-type is a row with parent_id set; the four umbrella
--     entries are flagged is_group = 1.
--   * Stats a sub-type table does not specify are inherited from the parent;
--     the monster_effective view exposes the COALESCE(own, parent) stat block.
--   * Verbatim source text lives in *_text / description; *_num are derived
--     conveniences (NULL when the source value is variable/ranged/"see below").
-- =====================================================================

DROP VIEW  IF EXISTS monster_effective;
DROP TABLE IF EXISTS monster_detail_rows;
DROP TABLE IF EXISTS monster_detail_tables;
DROP TABLE IF EXISTS monsters;

CREATE TABLE monsters (
    id                  INTEGER PRIMARY KEY,
    parent_id           INTEGER REFERENCES monsters(id) ON DELETE CASCADE,
    name                TEXT    NOT NULL UNIQUE,
    is_group            INTEGER NOT NULL DEFAULT 0 CHECK (is_group IN (0, 1)),
    is_described        INTEGER NOT NULL DEFAULT 1 CHECK (is_described IN (0, 1)),
    -- verbatim stat block
    move_text           TEXT,
    hit_dice_text       TEXT,
    armor_class_text    TEXT,
    treasure_type       TEXT,                 -- verbatim book value (may be compound)
    treasure_type_code  TEXT REFERENCES treasure_type(code),  -- primary A-T code (NULL = none)
    alignment_text      TEXT,
    attacks_text        TEXT,
    damage_text         TEXT,
    -- variant-oriented (mostly NULL for base monsters)
    size_text           TEXT,
    lair_text           TEXT,
    breath_weapon_text  TEXT,
    breath_range_text   TEXT,
    special_text        TEXT,
    -- derived numerics (NULL when not cleanly parseable)
    hit_dice_num        INTEGER,
    armor_class_num     INTEGER,
    description         TEXT,
    source_pdf_index    INTEGER,
    source_printed_page INTEGER
);

CREATE INDEX idx_monsters_parent ON monsters(parent_id);

-- Generic captioned lookup tables attached to a monster (e.g. the Dragon age
-- table). Mirrors the spell_detail_* shape.
CREATE TABLE monster_detail_tables (
    id          INTEGER PRIMARY KEY,
    monster_id  INTEGER NOT NULL REFERENCES monsters(id) ON DELETE CASCADE,
    caption     TEXT NOT NULL
);

CREATE TABLE monster_detail_rows (
    id              INTEGER PRIMARY KEY,
    detail_table_id INTEGER NOT NULL REFERENCES monster_detail_tables(id) ON DELETE CASCADE,
    row_order       INTEGER NOT NULL,
    key_low         INTEGER,
    key_high        INTEGER,
    key_label       TEXT,
    value_text      TEXT NOT NULL
);

CREATE INDEX idx_monster_detail_rows ON monster_detail_rows(detail_table_id, row_order);

-- Each monster's effective stat block: a promoted sub-type inherits any stat it
-- does not override from its parent (parent_id).
CREATE VIEW monster_effective AS
    SELECT
        m.id, m.parent_id, m.name, m.is_group,
        COALESCE(m.move_text,        p.move_text)        AS move_text,
        COALESCE(m.hit_dice_text,    p.hit_dice_text)    AS hit_dice_text,
        COALESCE(m.armor_class_text, p.armor_class_text) AS armor_class_text,
        COALESCE(m.treasure_type,    p.treasure_type)    AS treasure_type,
        COALESCE(m.alignment_text,   p.alignment_text)   AS alignment_text,
        COALESCE(m.attacks_text,     p.attacks_text)     AS attacks_text,
        COALESCE(m.damage_text,      p.damage_text)      AS damage_text,
        m.size_text, m.lair_text, m.breath_weapon_text, m.breath_range_text,
        m.special_text,
        COALESCE(m.description,      p.description)       AS description
    FROM monsters m
    LEFT JOIN monsters p ON p.id = m.parent_id;


-- =====================================================================
-- Characters module (Grimoire & Steel — NOT the 1978 rules; classes and
-- races are deliberately separated). character_sheet is modeled on the
-- D&D character record sheet; a character's six ability scores live in the
-- character_ability junction (one row per ability).
-- =====================================================================

DROP VIEW  IF EXISTS character_full;
DROP TABLE IF EXISTS character_ability;
DROP TABLE IF EXISTS character_sheet;
DROP TABLE IF EXISTS character_class;
DROP TABLE IF EXISTS race;
DROP TABLE IF EXISTS alignment;
DROP TABLE IF EXISTS ability;

-- The six abilities (reference list).
CREATE TABLE ability (
    id           INTEGER PRIMARY KEY,
    name         TEXT NOT NULL UNIQUE,
    abbreviation TEXT,
    sort_order   INTEGER,
    description  TEXT
);

-- The five alignments.
CREATE TABLE alignment (
    id           INTEGER PRIMARY KEY,
    name         TEXT NOT NULL UNIQUE,
    abbreviation TEXT,
    description  TEXT
);

-- Playable races (separate from class).
CREATE TABLE race (
    id                      INTEGER PRIMARY KEY,
    name                    TEXT NOT NULL UNIQUE,
    restrictions            TEXT,
    special_ability         TEXT,
    description             TEXT,
    carry_capacity_modifier REAL DEFAULT 1.0,  -- multiplies Strength carry capacity (encumbrance)
    base_movement_ft        INTEGER DEFAULT 120 -- racial movement rate per turn (unencumbered)
);

-- Classes and their sub-classes. A sub-class points to its base class via
-- parent_class_id (NULL = a base class).
CREATE TABLE character_class (
    id                         INTEGER PRIMARY KEY,
    name                       TEXT NOT NULL UNIQUE,
    parent_class_id            INTEGER REFERENCES character_class(id) ON DELETE CASCADE,
    prime_requisite_ability_id INTEGER REFERENCES ability(id),
    restrictions               TEXT,
    special_abilities          TEXT,
    description                TEXT
);

CREATE INDEX idx_class_parent ON character_class(parent_class_id);

-- A character record (see characterSheet.jpg). Saving-throw and stat fields
-- are stored here; the six ability scores are in character_ability.
CREATE TABLE character_sheet (
    id                       INTEGER PRIMARY KEY,
    character_name           TEXT NOT NULL,
    player_name              TEXT,
    dungeon_master           TEXT,
    race_id                  INTEGER REFERENCES race(id),
    class_id                 INTEGER REFERENCES character_class(id),
    alignment_id             INTEGER REFERENCES alignment(id),
    level                    INTEGER,
    armor_class              INTEGER,
    hit_points               INTEGER,
    max_hit_points           INTEGER,
    base_movement_ft         INTEGER,   -- unencumbered movement per turn (reduced by load)
    experience_points        INTEGER,
    experience_to_next_level INTEGER,
    earned_xp_bonus_pct      INTEGER,
    gold_pieces              INTEGER,
    -- saving throws
    save_poison_death        INTEGER,
    save_magic_wand          INTEGER,
    save_stone_paralysis     INTEGER,
    save_dragon_breath       INTEGER,
    save_spells_staff        INTEGER,
    -- free-text sections from the sheet
    special_abilities        TEXT,
    special_skills           TEXT,
    magic_items              TEXT,
    normal_items             TEXT,
    treasure_notes           TEXT,
    character_sketch         TEXT,
    other_notes              TEXT
);

CREATE INDEX idx_sheet_class ON character_sheet(class_id);
CREATE INDEX idx_sheet_race  ON character_sheet(race_id);

-- A character's score in one ability (six rows per character).
CREATE TABLE character_ability (
    id                INTEGER PRIMARY KEY,
    character_sheet_id INTEGER NOT NULL REFERENCES character_sheet(id) ON DELETE CASCADE,
    ability_id        INTEGER NOT NULL REFERENCES ability(id),
    score             INTEGER,
    adjustment_text   TEXT,
    UNIQUE (character_sheet_id, ability_id)
);

-- A character sheet with its class/race/alignment/prime-requisite resolved.
CREATE VIEW character_full AS
    SELECT
        cs.id, cs.character_name, cs.player_name, cs.dungeon_master,
        r.name  AS race,
        cc.name AS class,
        pc.name AS base_class,
        pa.name AS prime_requisite,
        al.name AS alignment,
        cs.level, cs.armor_class, cs.hit_points, cs.max_hit_points,
        cs.experience_points, cs.experience_to_next_level, cs.earned_xp_bonus_pct,
        cs.gold_pieces,
        cs.save_poison_death, cs.save_magic_wand, cs.save_stone_paralysis,
        cs.save_dragon_breath, cs.save_spells_staff,
        cs.special_abilities, cs.special_skills,
        cs.magic_items, cs.normal_items, cs.treasure_notes,
        cs.character_sketch, cs.other_notes
    FROM character_sheet cs
    LEFT JOIN race            r  ON r.id  = cs.race_id
    LEFT JOIN character_class cc ON cc.id = cs.class_id
    LEFT JOIN character_class pc ON pc.id = cc.parent_class_id
    LEFT JOIN ability         pa ON pa.id = cc.prime_requisite_ability_id
    LEFT JOIN alignment       al ON al.id = cs.alignment_id;


-- =====================================================================
-- Class level progression, equipment/weapons/armor, and encumbrance.
-- Per-level class data (XP to reach the level, hit dice, spells) is in
-- class_level (1978 progression covers the 4 base classes, levels 1-3;
-- sub-classes inherit their base class via class_progression).
-- Weapon damage and all weights are NOT in the Basic rules — they are
-- reasonable invented values for this game.
-- =====================================================================

DROP VIEW  IF EXISTS character_load;
DROP VIEW  IF EXISTS class_progression;
DROP TABLE IF EXISTS character_inventory;
DROP TABLE IF EXISTS class_level;
DROP TABLE IF EXISTS strength_capacity;
DROP TABLE IF EXISTS equipment;
DROP TABLE IF EXISTS armor_class;
DROP TABLE IF EXISTS armor;
DROP TABLE IF EXISTS weapon;

-- Experience / hit dice / spells a character has upon reaching each level.
CREATE TABLE class_level (
    id                INTEGER PRIMARY KEY,
    class_id          INTEGER NOT NULL REFERENCES character_class(id) ON DELETE CASCADE,
    level             INTEGER NOT NULL,
    title             TEXT,
    experience_points INTEGER NOT NULL,   -- cumulative XP needed to be this level (level 1 = 0)
    hit_dice          TEXT,               -- e.g. "1d8" (total hit dice at this level)
    spells            TEXT,               -- e.g. "1 first level" / "None"
    UNIQUE (class_id, level)
);

CREATE TABLE weapon (
    id            INTEGER PRIMARY KEY,
    name          TEXT NOT NULL UNIQUE,
    category      TEXT,
    cost_gp       INTEGER,
    weight_lbs    INTEGER,
    damage        TEXT,                 -- e.g. "1d8 (1-8)"
    damage_min    INTEGER,
    damage_max    INTEGER,
    two_handed    INTEGER NOT NULL DEFAULT 0 CHECK (two_handed IN (0, 1)),
    cleric_usable INTEGER NOT NULL DEFAULT 0 CHECK (cleric_usable IN (0, 1)),
    range_short_ft  INTEGER,   -- missile weapons: upper bound of each range band (ft)
    range_medium_ft INTEGER,
    range_long_ft   INTEGER,
    notes         TEXT
);

CREATE TABLE armor (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    armor_class INTEGER,   -- AC granted when worn (NULL for shields)
    ac_modifier INTEGER,   -- e.g. -1 for a shield (improves AC)
    cost_gp     INTEGER,
    weight_lbs  INTEGER,
    is_shield   INTEGER NOT NULL DEFAULT 0 CHECK (is_shield IN (0, 1)),
    notes       TEXT
);

CREATE TABLE armor_class (
    id          INTEGER PRIMARY KEY,
    armor_class INTEGER NOT NULL UNIQUE,
    armor_type  TEXT NOT NULL,
    description TEXT
);

CREATE TABLE equipment (
    id                  INTEGER PRIMARY KEY,
    name                TEXT NOT NULL UNIQUE,
    category            TEXT,
    cost_gp             INTEGER,
    weight_lbs          INTEGER,
    light_radius_ft     INTEGER,   -- for light sources (torch/lantern)
    light_duration_turns INTEGER,
    notes               TEXT
);

-- Max weight (lbs) a character can carry by Strength score. Multiplied by the
-- race's carry_capacity_modifier. (Invented for this game — Basic uses coins.)
CREATE TABLE strength_capacity (
    id                INTEGER PRIMARY KEY,
    strength_score    INTEGER NOT NULL UNIQUE,
    base_capacity_lbs INTEGER NOT NULL,   -- weight before becoming encumbered
    max_capacity_lbs  INTEGER NOT NULL,   -- absolute maximum that can be carried
    notes             TEXT
);

-- Items a character is carrying (exactly one of weapon/armor/equipment per row).
CREATE TABLE character_inventory (
    id                 INTEGER PRIMARY KEY,
    character_sheet_id INTEGER NOT NULL REFERENCES character_sheet(id) ON DELETE CASCADE,
    weapon_id          INTEGER REFERENCES weapon(id),
    armor_id           INTEGER REFERENCES armor(id),
    equipment_id       INTEGER REFERENCES equipment(id),
    quantity           INTEGER NOT NULL DEFAULT 1,
    notes              TEXT,
    CHECK ((weapon_id IS NOT NULL) + (armor_id IS NOT NULL) + (equipment_id IS NOT NULL) = 1)
);

CREATE INDEX idx_class_level     ON class_level(class_id, level);
CREATE INDEX idx_inventory_sheet ON character_inventory(character_sheet_id);

-- Each class's progression at every level; sub-classes inherit their base
-- class's rows (the Basic rules only define the four base classes).
CREATE VIEW class_progression AS
    SELECT cc.name AS class, lv.level, lv.title, lv.experience_points,
           lv.hit_dice, lv.spells,
           CASE WHEN cc.parent_class_id IS NULL THEN 'own'
                ELSE 'inherited from ' || src.name END AS source
    FROM character_class cc
    JOIN class_level lv ON lv.class_id = COALESCE(cc.parent_class_id, cc.id)
    JOIN character_class src ON src.id = lv.class_id;

-- A character's carried weight vs. capacity (Strength x race modifier), plus
-- the movement rate after encumbrance (full if within capacity, half if over
-- capacity up to max, none if over the maximum).
CREATE VIEW character_load AS
    SELECT
        cs.id, cs.character_name,
        r.name AS race,
        str.score AS strength,
        COALESCE(inv.total_weight, 0) AS weight_carried_lbs,
        CAST(sc.base_capacity_lbs * COALESCE(r.carry_capacity_modifier, 1.0) AS INTEGER)
            AS capacity_lbs,
        CAST(sc.max_capacity_lbs * COALESCE(r.carry_capacity_modifier, 1.0) AS INTEGER)
            AS max_capacity_lbs,
        CASE WHEN COALESCE(inv.total_weight, 0) >
                  CAST(sc.base_capacity_lbs * COALESCE(r.carry_capacity_modifier, 1.0) AS INTEGER)
             THEN 1 ELSE 0 END AS encumbered,
        COALESCE(cs.base_movement_ft, r.base_movement_ft, 120) AS base_movement_ft,
        CASE
            WHEN COALESCE(inv.total_weight, 0) <=
                 CAST(sc.base_capacity_lbs * COALESCE(r.carry_capacity_modifier, 1.0) AS INTEGER)
                THEN COALESCE(cs.base_movement_ft, r.base_movement_ft, 120)
            WHEN COALESCE(inv.total_weight, 0) <=
                 CAST(sc.max_capacity_lbs * COALESCE(r.carry_capacity_modifier, 1.0) AS INTEGER)
                THEN COALESCE(cs.base_movement_ft, r.base_movement_ft, 120) / 2
            ELSE 0
        END AS effective_movement_ft
    FROM character_sheet cs
    LEFT JOIN race r ON r.id = cs.race_id
    LEFT JOIN ability sa ON sa.name = 'Strength'
    LEFT JOIN character_ability str
           ON str.character_sheet_id = cs.id AND str.ability_id = sa.id
    LEFT JOIN strength_capacity sc ON sc.strength_score = str.score
    LEFT JOIN (
        SELECT ci.character_sheet_id AS cid,
               SUM(COALESCE(w.weight_lbs, a.weight_lbs, e.weight_lbs, 0) * ci.quantity)
                   AS total_weight
        FROM character_inventory ci
        LEFT JOIN weapon w    ON w.id = ci.weapon_id
        LEFT JOIN armor a     ON a.id = ci.armor_id
        LEFT JOIN equipment e ON e.id = ci.equipment_id
        GROUP BY ci.character_sheet_id
    ) inv ON inv.cid = cs.id;


-- =====================================================================
-- Environments and the wandering-monster table. Encounters are driven by
-- ENVIRONMENT + average PARTY LEVEL (not dungeon depth). monster_environment
-- is a view derived from wandering_monster, so a monster's "common
-- environments" always match where it actually appears in encounters.
-- =====================================================================

DROP VIEW  IF EXISTS monster_environment;
DROP TABLE IF EXISTS wandering_monster;
DROP TABLE IF EXISTS environment;

CREATE TABLE environment (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    description TEXT
);

CREATE TABLE wandering_monster (
    id              INTEGER PRIMARY KEY,
    environment_id  INTEGER NOT NULL REFERENCES environment(id) ON DELETE CASCADE,
    monster_id      INTEGER NOT NULL REFERENCES monsters(id) ON DELETE CASCADE,
    party_level_min INTEGER NOT NULL DEFAULT 1,   -- appears for parties of this average level...
    party_level_max INTEGER NOT NULL DEFAULT 3,   -- ...up to this average level
    number_min      INTEGER,                      -- number appearing
    number_max      INTEGER,
    weight          INTEGER DEFAULT 1,            -- relative chance of being the encounter
    notes           TEXT
);

CREATE INDEX idx_wm_env ON wandering_monster(environment_id, party_level_min, party_level_max);

-- A monster's common environments = where it appears in the wandering table.
CREATE VIEW monster_environment AS
    SELECT DISTINCT wm.monster_id, wm.environment_id,
           m.name AS monster, e.name AS environment
    FROM wandering_monster wm
    JOIN monsters m    ON m.id = wm.monster_id
    JOIN environment e ON e.id = wm.environment_id;


-- =====================================================================
-- Rules / reference tables (faithful to the 1978 text). These are the game
-- engine's math: ability adjustments, attack (to-hit) matrices, saving
-- throws, monster XP, thief skills, turning undead, magic-user spell
-- learning, reactions, languages, coinage and gem/jewelry values.
-- =====================================================================

DROP TABLE IF EXISTS ability_adjustment;
DROP TABLE IF EXISTS attack_table;
DROP TABLE IF EXISTS saving_throw;
DROP TABLE IF EXISTS monster_xp;
DROP TABLE IF EXISTS thief_skill;
DROP TABLE IF EXISTS turn_undead;
DROP TABLE IF EXISTS magic_user_intelligence;
DROP TABLE IF EXISTS reaction_table;
DROP TABLE IF EXISTS language;
DROP TABLE IF EXISTS coin;
DROP TABLE IF EXISTS treasure_value;

-- Bonuses/penalties by ability score (p.6). applies_to is the ability the row
-- governs: 'Prime Requisite' (earned-XP %), 'Constitution' (per hit die),
-- 'Dexterity' (missile attacks).
CREATE TABLE ability_adjustment (
    id         INTEGER PRIMARY KEY,
    applies_to TEXT NOT NULL,
    score_low  INTEGER,
    score_high INTEGER,
    modifier   INTEGER,
    effect     TEXT
);

-- To-hit matrices (pp.18-19), long form: roll needed for an attacker to hit a
-- given Armor Class. attacker_kind is 'character' or 'monster'.
CREATE TABLE attack_table (
    id             INTEGER PRIMARY KEY,
    attacker_kind  TEXT NOT NULL CHECK (attacker_kind IN ('character', 'monster')),
    attacker_label TEXT NOT NULL,   -- e.g. '1st-3rd Level Character' or HD band '3+ to 4'
    sort_order     INTEGER,
    target_ac      INTEGER NOT NULL,
    roll_needed    INTEGER NOT NULL
);

CREATE INDEX idx_attack ON attack_table(attacker_kind, attacker_label, target_ac);

-- Saving throw numbers by class group, levels 1-3 (p.14).
CREATE TABLE saving_throw (
    id               INTEGER PRIMARY KEY,
    class_group      TEXT NOT NULL UNIQUE,
    level_min        INTEGER DEFAULT 1,
    level_max        INTEGER DEFAULT 3,
    vs_spell_staff   INTEGER,
    vs_magic_wand    INTEGER,
    vs_death_poison  INTEGER,
    vs_stone         INTEGER,
    vs_dragon_breath INTEGER
);

-- Experience awarded for defeating a monster, by Hit Dice (p.12).
CREATE TABLE monster_xp (
    id                    INTEGER PRIMARY KEY,
    hit_dice              TEXT NOT NULL,
    value                 INTEGER NOT NULL,
    special_ability_bonus INTEGER
);

-- Thief skill chances by level (tiers A/B/C = levels 1/2/3) (p.13).
CREATE TABLE thief_skill (
    id          INTEGER PRIMARY KEY,
    level       INTEGER NOT NULL,
    skill       TEXT NOT NULL,
    chance_pct  INTEGER,   -- NULL for non-percentage skills (e.g. hear noise)
    chance_text TEXT
);

-- Cleric turning of undead (p.12). result is a number to roll (2d6), 'T'
-- (automatic) or 'no effect'.
CREATE TABLE turn_undead (
    id           INTEGER PRIMARY KEY,
    cleric_level INTEGER NOT NULL,
    undead_type  TEXT NOT NULL,
    undead_order INTEGER,
    result       TEXT NOT NULL
);

-- How many spells a magic-user can know, by Intelligence (p.13).
CREATE TABLE magic_user_intelligence (
    id          INTEGER PRIMARY KEY,
    int_low     INTEGER,
    int_high    INTEGER,
    pct_to_know INTEGER,
    min_spells  INTEGER,
    max_spells  TEXT       -- a number or 'All'
);

-- 2d6 reaction table (p.12).
CREATE TABLE reaction_table (
    id       INTEGER PRIMARY KEY,
    roll_low INTEGER,
    roll_high INTEGER,
    reaction TEXT
);

-- Languages (p.9). kind: common / alignment / racial / monster.
CREATE TABLE language (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL UNIQUE,
    kind        TEXT,
    description TEXT
);

-- Coinage and exchange (p.34), value expressed in gold pieces.
CREATE TABLE coin (
    id           INTEGER PRIMARY KEY,
    name         TEXT NOT NULL UNIQUE,
    abbreviation TEXT,
    value_gp     REAL,
    notes        TEXT
);

-- Gem and jewelry base values (p.34).
CREATE TABLE treasure_value (
    id       INTEGER PRIMARY KEY,
    kind     TEXT NOT NULL,   -- 'gem' | 'jewelry'
    roll_low INTEGER,
    roll_high INTEGER,
    value_gp INTEGER,
    dice     TEXT,
    notes    TEXT
);
