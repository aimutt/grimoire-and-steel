#include "gns/CharacterIO.h"
#include "gns/Database.h"   // gns::DbError
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
    background        TEXT, goal TEXT, personality TEXT, notes TEXT
);
CREATE TABLE character_training (name TEXT);
CREATE TABLE character_spell (name TEXT);
)sql";

} // namespace

// ---- save -------------------------------------------------------------------

void saveCharacter(const Character& c, const std::string& path) {
    Conn conn;
    if (sqlite3_open_v2(path.c_str(), &conn.db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        fail(conn.db, "cannot create '" + path + "'");

    exec(conn.db, "PRAGMA foreign_keys=OFF;");
    exec(conn.db,
         "DROP TABLE IF EXISTS character_spell;"
         "DROP TABLE IF EXISTS character_training;"
         "DROP TABLE IF EXISTS character;");
    exec(conn.db, kSchema);
    exec(conn.db, ("PRAGMA user_version=" + std::to_string(kCharacterFormatVersion) + ";").c_str());
    exec(conn.db, "BEGIN;");

    {
        Stmt s(conn.db,
            "INSERT INTO character(id,name,player_name,kin,calling,level,"
            "might,grace,wits,spirit,max_life,life,defense,ap,strain,"
            "armor_name,shield,weapon_name,weapon_damage_die,weapon_bonus,"
            "background,goal,personality,notes) "
            "VALUES(1,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        s.bind(c.name).bind(c.playerName).bind(c.kin).bind(c.calling).bind(c.level)
         .bind(c.traits.might).bind(c.traits.grace).bind(c.traits.wits).bind(c.traits.spirit)
         .bind(c.maxLife).bind(c.life).bind(c.defense).bind(c.ap).bind(c.strain)
         .bind(c.armorName).bind(c.shield ? 1 : 0)
         .bind(c.weaponName).bind(c.weaponDamageDie).bind(c.weaponBonus)
         .bind(c.background).bind(c.goal).bind(c.personality).bind(c.notes);
        s.run();
    }
    {
        Stmt s(conn.db, "INSERT INTO character_training(name) VALUES(?);");
        for (const auto& t : c.trainings) { s.bind(t); s.run(); }
    }
    {
        Stmt s(conn.db, "INSERT INTO character_spell(name) VALUES(?);");
        for (const auto& sp : c.spells) { s.bind(sp); s.run(); }
    }

    exec(conn.db, "COMMIT;");
}

// ---- load -------------------------------------------------------------------

Character loadCharacter(const std::string& path) {
    Conn conn;
    if (sqlite3_open_v2(path.c_str(), &conn.db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        fail(conn.db, "cannot open '" + path + "'");

    Character c;
    {
        Stmt s(conn.db,
            "SELECT name,player_name,kin,calling,level,"
            "might,grace,wits,spirit,max_life,life,defense,ap,strain,"
            "armor_name,shield,weapon_name,weapon_damage_die,weapon_bonus,"
            "background,goal,personality,notes FROM character WHERE id=1;");
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

    return c;
}

} // namespace gns
