#include "gns/CharacterIO.h"
#include "gns/Database.h"   // gns::DbError
#include "gns/AtomicDb.h"   // gns::writeDatabaseAtomically
#include "sqlite3.h"

#include <string>

// .gnschar save/load. Mirrors ModuleIO.cpp: a standalone SQLite file written with our
// own handle (parameterized inserts + a transaction), read tolerantly.

namespace gns {

namespace {

// RAII for the connection so every throw path closes it.
struct Conn {
    sqlite3* db = nullptr;
    ~Conn() { if (db) sqlite3_close(db); }
};

[[noreturn]] void fail(sqlite3* db, const std::string& what) {
    throw DbError(what + ": " + (db ? sqlite3_errmsg(db) : "no connection"));
}

void exec(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "exec failed";
        sqlite3_free(err);
        throw DbError(std::string("exec failed: ") + msg);
    }
}

// Prepared-statement wrapper with chained binders for terse inserts.
struct Stmt {
    sqlite3* db = nullptr;
    sqlite3_stmt* s = nullptr;
    int idx = 1;

    Stmt(sqlite3* d, const char* sql) : db(d) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            fail(db, "prepare failed");
    }
    ~Stmt() { sqlite3_finalize(s); }

    Stmt& bind(int v)    { sqlite3_bind_int(s, idx++, v); return *this; }
    Stmt& bind(const std::string& v) {
        sqlite3_bind_text(s, idx++, v.c_str(), -1, SQLITE_TRANSIENT); return *this;
    }
    void run() {
        if (sqlite3_step(s) != SQLITE_DONE) fail(db, "step failed");
        sqlite3_reset(s);
        sqlite3_clear_bindings(s);
        idx = 1;
    }
};

int colInt(sqlite3_stmt* s, int i) { return sqlite3_column_int(s, i); }
std::string colText(sqlite3_stmt* s, int i) {
    const unsigned char* t = sqlite3_column_text(s, i);
    return t ? reinterpret_cast<const char*>(t) : "";
}

const char* kSchema = R"sql(
CREATE TABLE character (
    id                INTEGER PRIMARY KEY CHECK (id = 1),
    name              TEXT, player_name TEXT,
    kin               TEXT, calling TEXT, level INTEGER,
    might             INTEGER, grace INTEGER, wits INTEGER, spirit INTEGER,
    max_life          INTEGER, life INTEGER, defense INTEGER, ap INTEGER, strain INTEGER,
    armor_name        TEXT, shield INTEGER,
    weapon_name       TEXT, weapon_damage_die TEXT, weapon_bonus INTEGER,
    background        TEXT, goal TEXT, personality TEXT, notes TEXT,
    portrait          TEXT,
    gold              INTEGER,
    armor_defense_bonus INTEGER
);
CREATE TABLE character_training (name TEXT);
CREATE TABLE character_spell (name TEXT);
CREATE TABLE character_item (name TEXT, description TEXT, image_id TEXT, image_path TEXT, quantity INTEGER, value INTEGER,
    slot INTEGER, damage_die TEXT, defense_bonus INTEGER, weapon_bonus INTEGER, dropable INTEGER);
CREATE TABLE character_item_mutation (
    item_ord INTEGER,
    kind     INTEGER,           /* 0 = onAcquire, 1 = onUnacquire */
    ord      INTEGER,
    var_name TEXT, op INTEGER, value TEXT
);
)sql";

} // namespace

// ---- save -------------------------------------------------------------------

void saveCharacter(const Character& c, const std::string& path) {
  // Atomic, non-destructive write: build a fresh temp file in one transaction, then swap it
  // in (keeping a .bak). A failure leaves the existing .gnschar untouched. See ModuleIO.cpp /
  // writeDatabaseAtomically for the shared guardrail.
  writeDatabaseAtomically(path, [&](sqlite3* db) {
    exec(db, kSchema);
    exec(db, ("PRAGMA user_version=" + std::to_string(kCharacterFormatVersion) + ";").c_str());

    {
        Stmt s(db,
            "INSERT INTO character(id,name,player_name,kin,calling,level,"
            "might,grace,wits,spirit,max_life,life,defense,ap,strain,"
            "armor_name,shield,weapon_name,weapon_damage_die,weapon_bonus,"
            "background,goal,personality,notes,portrait,gold,armor_defense_bonus) "
            "VALUES(1,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        s.bind(c.name).bind(c.playerName).bind(c.kin).bind(c.calling).bind(c.level)
         .bind(c.traits.might).bind(c.traits.grace).bind(c.traits.wits).bind(c.traits.spirit)
         .bind(c.maxLife).bind(c.life).bind(c.defense).bind(c.ap).bind(c.strain)
         .bind(c.armorName).bind(c.shield ? 1 : 0)
         .bind(c.weaponName).bind(c.weaponDamageDie).bind(c.weaponBonus)
         .bind(c.background).bind(c.goal).bind(c.personality).bind(c.notes).bind(c.portraitPath)
         .bind(c.gold).bind(c.armorDefenseBonus);
        s.run();
    }
    {
        Stmt s(db, "INSERT INTO character_training(name) VALUES(?);");
        for (const auto& t : c.trainings) { s.bind(t); s.run(); }
    }
    {
        Stmt s(db, "INSERT INTO character_spell(name) VALUES(?);");
        for (const auto& sp : c.spells) { s.bind(sp); s.run(); }
    }
    {
        Stmt s(db, "INSERT INTO character_item(name,description,image_id,image_path,quantity,value,"
                   "slot,damage_die,defense_bonus,weapon_bonus,dropable) "
                   "VALUES(?,?,?,?,?,?,?,?,?,?,?);");
        Stmt mu(db, "INSERT INTO character_item_mutation(item_ord,kind,ord,var_name,op,value) "
                    "VALUES(?,?,?,?,?,?);");
        for (size_t i = 0; i < c.inventory.size(); ++i) {
            const auto& it = c.inventory[i];
            s.bind(it.name).bind(it.description).bind(it.imageId).bind(it.imagePath)
             .bind(it.quantity < 1 ? 1 : it.quantity).bind(it.value)
             .bind(it.slot).bind(it.damageDie).bind(it.defenseBonus).bind(it.weaponBonus)
             .bind(it.dropable ? 1 : 0);
            s.run();
            for (size_t k = 0; k < it.onAcquire.size(); ++k) {
                const auto& m = it.onAcquire[k];
                mu.bind((int)i).bind(0).bind((int)k).bind(m.varName).bind(m.op).bind(m.value);
                mu.run();
            }
            for (size_t k = 0; k < it.onUnacquire.size(); ++k) {
                const auto& m = it.onUnacquire[k];
                mu.bind((int)i).bind(1).bind((int)k).bind(m.varName).bind(m.op).bind(m.value);
                mu.run();
            }
        }
    }
  });
}

// ---- load -------------------------------------------------------------------

Character loadCharacter(const std::string& path) {
    Conn conn;
    if (sqlite3_open_v2(path.c_str(), &conn.db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        fail(conn.db, "cannot open '" + path + "'");

    Character c;
    // `portrait` was added in v2, `gold` in v3, `armor_defense_bonus` in v7; loader `level`
    // 3 = newest (with armor bonus), 2 = +gold, 1 = +portrait, 0 = v1.
    auto loadRow = [&](int level) {
        bool withPortrait = level >= 1, withGold = level >= 2, withArmorBonus = level >= 3;
        std::string sql =
            "SELECT name,player_name,kin,calling,level,"
            "might,grace,wits,spirit,max_life,life,defense,ap,strain,"
            "armor_name,shield,weapon_name,weapon_damage_die,weapon_bonus,"
            "background,goal,personality,notes";
        if (withPortrait) sql += ",portrait";
        if (withGold) sql += ",gold";
        if (withArmorBonus) sql += ",armor_defense_bonus";
        sql += " FROM character WHERE id=1;";
        Stmt s(conn.db, sql.c_str());
        if (sqlite3_step(s.s) != SQLITE_ROW) fail(conn.db, "character row missing");
        c.name            = colText(s.s, 0);
        c.playerName      = colText(s.s, 1);
        c.kin             = colText(s.s, 2);
        c.calling         = colText(s.s, 3);
        c.level           = colInt(s.s, 4);
        c.traits.might    = colInt(s.s, 5);
        c.traits.grace    = colInt(s.s, 6);
        c.traits.wits     = colInt(s.s, 7);
        c.traits.spirit   = colInt(s.s, 8);
        c.maxLife         = colInt(s.s, 9);
        c.life            = colInt(s.s, 10);
        c.defense         = colInt(s.s, 11);
        c.ap              = colInt(s.s, 12);
        c.strain          = colInt(s.s, 13);
        c.armorName       = colText(s.s, 14);
        c.shield          = colInt(s.s, 15) != 0;
        c.weaponName      = colText(s.s, 16);
        c.weaponDamageDie = colText(s.s, 17);
        c.weaponBonus     = colInt(s.s, 18);
        c.background      = colText(s.s, 19);
        c.goal            = colText(s.s, 20);
        c.personality     = colText(s.s, 21);
        c.notes           = colText(s.s, 22);
        int col = 23;
        if (withPortrait) c.portraitPath = colText(s.s, col++);
        if (withGold) c.gold = colInt(s.s, col++);
        if (withArmorBonus) c.armorDefenseBonus = colInt(s.s, col++);
    };
    for (int level = 3; ; --level) {
        try { loadRow(level); break; }
        catch (const DbError&) { if (level == 0) throw; }
    }
    // Child lists live in their own tables; tolerate their absence in older files.
    try {
        Stmt s(conn.db, "SELECT name FROM character_training;");
        while (sqlite3_step(s.s) == SQLITE_ROW) c.trainings.push_back(colText(s.s, 0));
    } catch (const DbError&) {}
    try {
        Stmt s(conn.db, "SELECT name FROM character_spell;");
        while (sqlite3_step(s.s) == SQLITE_ROW) c.spells.push_back(colText(s.s, 0));
    } catch (const DbError&) {}
    // character_item gained description/image_id/image_path/quantity in v4, `value` in v5, and the
    // equip profile (slot/damage_die/defense_bonus/weapon_bonus/dropable) in v7. level 3 = newest,
    // 2 = +value, 1 = v4, 0 = v3 (name only); fall back a tier.
    auto loadItems = [&](int level) {
        const char* sql =
            level >= 3 ? "SELECT name,description,image_id,image_path,quantity,value,"
                         "slot,damage_die,defense_bonus,weapon_bonus,dropable FROM character_item;"
            : level >= 2 ? "SELECT name,description,image_id,image_path,quantity,value FROM character_item;"
            : level >= 1 ? "SELECT name,description,image_id,image_path,quantity FROM character_item;"
                         : "SELECT name FROM character_item;";
        Stmt s(conn.db, sql);
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            InventoryItem it;
            it.name = colText(s.s, 0);
            if (level >= 1) {
                it.description = colText(s.s, 1);
                it.imageId     = colText(s.s, 2);
                it.imagePath   = colText(s.s, 3);
                it.quantity    = colInt(s.s, 4);
                if (it.quantity < 1) it.quantity = 1;
                if (level >= 2) it.value = colInt(s.s, 5);
                if (level >= 3) {
                    it.slot         = colInt(s.s, 6);
                    it.damageDie    = colText(s.s, 7);
                    it.defenseBonus = colInt(s.s, 8);
                    it.weaponBonus  = colInt(s.s, 9);
                    it.dropable     = colInt(s.s, 10) != 0;
                }
            }
            c.inventory.push_back(std::move(it));
        }
    };
    for (int level = 3; ; --level) {
        try { loadItems(level); break; }
        catch (const DbError&) { c.inventory.clear(); if (level == 0) break; }
    }
    // Item acquire/loss mutations (v6), keyed by item_ord: kind 0 = onAcquire, 1 = onUnacquire.
    try {
        Stmt s(conn.db, "SELECT item_ord,kind,var_name,op,value FROM character_item_mutation "
                        "ORDER BY item_ord,kind,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            int itemOrd = colInt(s.s, 0);
            if (itemOrd >= 0 && itemOrd < (int)c.inventory.size()) {
                VarMutation m{colText(s.s, 2), colInt(s.s, 3), colText(s.s, 4)};
                if (colInt(s.s, 1) == 0) c.inventory[itemOrd].onAcquire.push_back(std::move(m));
                else                     c.inventory[itemOrd].onUnacquire.push_back(std::move(m));
            }
        }
    } catch (const DbError&) {}

    return c;
}

} // namespace gns
