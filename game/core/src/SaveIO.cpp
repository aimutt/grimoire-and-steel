#include "gns/SaveIO.h"
#include "gns/Database.h"   // gns::DbError
#include "gns/AtomicDb.h"   // gns::writeDatabaseAtomically
#include "sqlite3.h"

#include <string>

// .gnssav save/load. Mirrors ModuleIO.cpp / CharacterIO.cpp: a standalone SQLite file written
// with our own handle (parameterized inserts + a transaction), read tolerantly. The party's
// characters are serialized into an owner-keyed save_character table (+ child lists); the column
// set intentionally mirrors CharacterIO's `character` table -- keep the two in sync when the
// Character struct grows a persisted field.

namespace gns {

namespace {

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

struct Stmt {
    sqlite3* db = nullptr;
    sqlite3_stmt* s = nullptr;
    int idx = 1;

    Stmt(sqlite3* d, const char* sql) : db(d) {
        if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            fail(db, "prepare failed");
    }
    ~Stmt() { sqlite3_finalize(s); }

    Stmt& bind(int v) { sqlite3_bind_int(s, idx++, v); return *this; }
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
CREATE TABLE save_meta (
    id           INTEGER PRIMARY KEY CHECK (id = 1),
    module_path  TEXT,
    seed         TEXT,
    map_id       INTEGER, area_id INTEGER, turn_count INTEGER, mode INTEGER,
    cursor_x     INTEGER, cursor_y INTEGER, face_x INTEGER, face_y INTEGER,
    active_char  INTEGER,
    elapsed_minutes    INTEGER, paused INTEGER, minutes_since_rest INTEGER
);
CREATE TABLE save_control_points (cp_id INTEGER);
CREATE TABLE save_flags (flag TEXT);
CREATE TABLE save_resolved_contexts (area_id INTEGER, ctx_name TEXT);
CREATE TABLE save_globals (name TEXT, value TEXT);
CREATE TABLE save_journal (ord INTEGER, line TEXT);
CREATE TABLE save_character (
    owner             INTEGER,
    name              TEXT, player_name TEXT,
    kin               TEXT, calling TEXT, level INTEGER,
    might             INTEGER, grace INTEGER, wits INTEGER, spirit INTEGER,
    max_life          INTEGER, life INTEGER, defense INTEGER, ap INTEGER, strain INTEGER,
    armor_name        TEXT, shield INTEGER,
    weapon_name       TEXT, weapon_damage_die TEXT, weapon_bonus INTEGER,
    background        TEXT, goal TEXT, personality TEXT, notes TEXT,
    portrait          TEXT, gold INTEGER,
    armor_defense_bonus INTEGER
);
CREATE TABLE save_character_training (owner INTEGER, name TEXT);
CREATE TABLE save_character_spell    (owner INTEGER, name TEXT);
CREATE TABLE save_character_item     (owner INTEGER, name TEXT, description TEXT, image_id TEXT, image_path TEXT, quantity INTEGER, value INTEGER,
    slot INTEGER, damage_die TEXT, defense_bonus INTEGER, weapon_bonus INTEGER, dropable INTEGER);
CREATE TABLE save_character_item_mutation (
    owner    INTEGER, item_ord INTEGER,
    kind     INTEGER,           /* 0 = onAcquire, 1 = onUnacquire */
    ord      INTEGER,
    var_name TEXT, op INTEGER, value TEXT
);
CREATE TABLE save_deactivated_areas  (area_id INTEGER);
CREATE TABLE save_deleted_contexts   (area_id INTEGER, ctx_name TEXT);
CREATE TABLE save_granted_dropable   (name TEXT, dropable INTEGER);
)sql";

} // namespace

// ---- save -------------------------------------------------------------------

void saveGame(const std::string& path, const GameSave& save) {
  writeDatabaseAtomically(path, [&](sqlite3* db) {
    exec(db, kSchema);
    exec(db, ("PRAGMA user_version=" + std::to_string(kSaveFormatVersion) + ";").c_str());

    {
        Stmt s(db, "INSERT INTO save_meta(id,module_path,seed,map_id,area_id,turn_count,mode,"
                   "cursor_x,cursor_y,face_x,face_y,active_char,"
                   "elapsed_minutes,paused,minutes_since_rest) "
                   "VALUES(1,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        s.bind(save.modulePath).bind(std::to_string(save.seed))
         .bind(save.mapId).bind(save.areaId).bind(save.turnCount).bind(save.mode)
         .bind(save.cursorX).bind(save.cursorY).bind(save.faceX).bind(save.faceY)
         .bind(save.activeChar)
         .bind(save.elapsedMinutes).bind(save.paused).bind(save.minutesSinceRest);
        s.run();
    }
    {
        Stmt s(db, "INSERT INTO save_control_points(cp_id) VALUES(?);");
        for (int id : save.controlPoints) { s.bind(id); s.run(); }
    }
    {
        Stmt s(db, "INSERT INTO save_flags(flag) VALUES(?);");
        for (const auto& f : save.flags) { s.bind(f); s.run(); }
    }
    {
        Stmt s(db, "INSERT INTO save_resolved_contexts(area_id,ctx_name) VALUES(?,?);");
        for (const auto& rc : save.resolvedContexts) { s.bind(rc.first).bind(rc.second); s.run(); }
    }
    {
        Stmt s(db, "INSERT INTO save_globals(name,value) VALUES(?,?);");
        for (const auto& kv : save.globals) { s.bind(kv.first).bind(kv.second); s.run(); }
    }
    {
        Stmt s(db, "INSERT INTO save_deactivated_areas(area_id) VALUES(?);");
        for (int id : save.deactivatedAreas) { s.bind(id); s.run(); }
    }
    {
        Stmt s(db, "INSERT INTO save_deleted_contexts(area_id,ctx_name) VALUES(?,?);");
        for (const auto& dc : save.deletedContexts) { s.bind(dc.first).bind(dc.second); s.run(); }
    }
    {
        Stmt s(db, "INSERT INTO save_granted_dropable(name,dropable) VALUES(?,?);");
        for (const auto& kv : save.grantedDropable) { s.bind(kv.first).bind(kv.second ? 1 : 0); s.run(); }
    }
    {
        Stmt s(db, "INSERT INTO save_journal(ord,line) VALUES(?,?);");
        for (size_t i = 0; i < save.journal.size(); ++i) { s.bind((int)i).bind(save.journal[i]); s.run(); }
    }
    {
        Stmt s(db,
            "INSERT INTO save_character(owner,name,player_name,kin,calling,level,"
            "might,grace,wits,spirit,max_life,life,defense,ap,strain,"
            "armor_name,shield,weapon_name,weapon_damage_die,weapon_bonus,"
            "background,goal,personality,notes,portrait,gold,armor_defense_bonus) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        Stmt tr(db, "INSERT INTO save_character_training(owner,name) VALUES(?,?);");
        Stmt sp(db, "INSERT INTO save_character_spell(owner,name) VALUES(?,?);");
        Stmt it(db, "INSERT INTO save_character_item(owner,name,description,image_id,image_path,quantity,value,"
                    "slot,damage_die,defense_bonus,weapon_bonus,dropable) "
                    "VALUES(?,?,?,?,?,?,?,?,?,?,?,?);");
        Stmt im(db, "INSERT INTO save_character_item_mutation(owner,item_ord,kind,ord,var_name,op,value) "
                    "VALUES(?,?,?,?,?,?,?);");
        for (size_t i = 0; i < save.party.size(); ++i) {
            const Character& c = save.party[i];
            int owner = (int)i;
            s.bind(owner).bind(c.name).bind(c.playerName).bind(c.kin).bind(c.calling).bind(c.level)
             .bind(c.traits.might).bind(c.traits.grace).bind(c.traits.wits).bind(c.traits.spirit)
             .bind(c.maxLife).bind(c.life).bind(c.defense).bind(c.ap).bind(c.strain)
             .bind(c.armorName).bind(c.shield ? 1 : 0)
             .bind(c.weaponName).bind(c.weaponDamageDie).bind(c.weaponBonus)
             .bind(c.background).bind(c.goal).bind(c.personality).bind(c.notes).bind(c.portraitPath)
             .bind(c.gold).bind(c.armorDefenseBonus);
            s.run();
            for (const auto& t : c.trainings) { tr.bind(owner).bind(t); tr.run(); }
            for (const auto& x : c.spells)    { sp.bind(owner).bind(x); sp.run(); }
            for (size_t j = 0; j < c.inventory.size(); ++j) {
                const auto& x = c.inventory[j];
                it.bind(owner).bind(x.name).bind(x.description).bind(x.imageId).bind(x.imagePath)
                  .bind(x.quantity < 1 ? 1 : x.quantity).bind(x.value)
                  .bind(x.slot).bind(x.damageDie).bind(x.defenseBonus).bind(x.weaponBonus)
                  .bind(x.dropable ? 1 : 0);
                it.run();
                for (size_t k = 0; k < x.onAcquire.size(); ++k) {
                    const auto& m = x.onAcquire[k];
                    im.bind(owner).bind((int)j).bind(0).bind((int)k).bind(m.varName).bind(m.op).bind(m.value);
                    im.run();
                }
                for (size_t k = 0; k < x.onUnacquire.size(); ++k) {
                    const auto& m = x.onUnacquire[k];
                    im.bind(owner).bind((int)j).bind(1).bind((int)k).bind(m.varName).bind(m.op).bind(m.value);
                    im.run();
                }
            }
        }
    }
  });
}

// ---- load -------------------------------------------------------------------

GameSave loadGame(const std::string& path) {
    Conn conn;
    if (sqlite3_open_v2(path.c_str(), &conn.db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        fail(conn.db, "cannot open '" + path + "'");

    GameSave save;
    // save_meta gained the game-clock columns in v8; read them when present, else keep the defaults
    // (0) so older saves resume at the module's starting date/time.
    auto loadMeta = [&](bool withTime) {
        std::string sql = "SELECT module_path,seed,map_id,area_id,turn_count,mode,"
                          "cursor_x,cursor_y,face_x,face_y,active_char";
        if (withTime) sql += ",elapsed_minutes,paused,minutes_since_rest";
        sql += " FROM save_meta WHERE id=1;";
        Stmt s(conn.db, sql.c_str());
        if (sqlite3_step(s.s) != SQLITE_ROW) fail(conn.db, "save_meta row missing");
        save.modulePath = colText(s.s, 0);
        try { save.seed = std::stoull(colText(s.s, 1)); } catch (...) { save.seed = 0; }
        save.mapId      = colInt(s.s, 2);
        save.areaId     = colInt(s.s, 3);
        save.turnCount  = colInt(s.s, 4);
        save.mode       = colInt(s.s, 5);
        save.cursorX    = colInt(s.s, 6);
        save.cursorY    = colInt(s.s, 7);
        save.faceX      = colInt(s.s, 8);
        save.faceY      = colInt(s.s, 9);
        save.activeChar = colInt(s.s, 10);
        if (withTime) {
            save.elapsedMinutes   = colInt(s.s, 11);
            save.paused           = colInt(s.s, 12);
            save.minutesSinceRest = colInt(s.s, 13);
        }
    };
    try { loadMeta(true); }
    catch (const DbError&) { loadMeta(false); }
    {
        Stmt s(conn.db, "SELECT cp_id FROM save_control_points;");
        while (sqlite3_step(s.s) == SQLITE_ROW) save.controlPoints.insert(colInt(s.s, 0));
    }
    {
        Stmt s(conn.db, "SELECT flag FROM save_flags;");
        while (sqlite3_step(s.s) == SQLITE_ROW) save.flags.insert(colText(s.s, 0));
    }
    // save_resolved_contexts (per-context) replaced the v5 per-area save_resolved_areas in v6;
    // tolerate older saves that lack it (their per-area resolved flags are simply dropped, letting
    // contexts re-prompt once).
    try {
        Stmt s(conn.db, "SELECT area_id,ctx_name FROM save_resolved_contexts;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            save.resolvedContexts.insert({colInt(s.s, 0), colText(s.s, 1)});
    } catch (const DbError&) {}
    // save_globals was added in v2; tolerate older saves that lack it.
    try {
        Stmt s(conn.db, "SELECT name,value FROM save_globals;");
        while (sqlite3_step(s.s) == SQLITE_ROW) save.globals[colText(s.s, 0)] = colText(s.s, 1);
    } catch (const DbError&) {
        // no save_globals table — leave empty (re-seeded from module defaults on load)
    }
    {
        Stmt s(conn.db, "SELECT line FROM save_journal ORDER BY ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) save.journal.push_back(colText(s.s, 0));
    }
    // save_character gained armor_defense_bonus in v7; tolerate older saves that lack it by
    // retrying without the column.
    auto loadParty = [&](bool withArmorBonus) {
        std::string sql =
            "SELECT owner,name,player_name,kin,calling,level,"
            "might,grace,wits,spirit,max_life,life,defense,ap,strain,"
            "armor_name,shield,weapon_name,weapon_damage_die,weapon_bonus,"
            "background,goal,personality,notes,portrait,gold";
        if (withArmorBonus) sql += ",armor_defense_bonus";
        sql += " FROM save_character ORDER BY owner;";
        Stmt s(conn.db, sql.c_str());
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            Character c;
            c.name            = colText(s.s, 1);
            c.playerName      = colText(s.s, 2);
            c.kin             = colText(s.s, 3);
            c.calling         = colText(s.s, 4);
            c.level           = colInt(s.s, 5);
            c.traits.might    = colInt(s.s, 6);
            c.traits.grace    = colInt(s.s, 7);
            c.traits.wits     = colInt(s.s, 8);
            c.traits.spirit   = colInt(s.s, 9);
            c.maxLife         = colInt(s.s, 10);
            c.life            = colInt(s.s, 11);
            c.defense         = colInt(s.s, 12);
            c.ap              = colInt(s.s, 13);
            c.strain          = colInt(s.s, 14);
            c.armorName       = colText(s.s, 15);
            c.shield          = colInt(s.s, 16) != 0;
            c.weaponName      = colText(s.s, 17);
            c.weaponDamageDie = colText(s.s, 18);
            c.weaponBonus     = colInt(s.s, 19);
            c.background      = colText(s.s, 20);
            c.goal            = colText(s.s, 21);
            c.personality     = colText(s.s, 22);
            c.notes           = colText(s.s, 23);
            c.portraitPath    = colText(s.s, 24);
            c.gold            = colInt(s.s, 25);
            if (withArmorBonus) c.armorDefenseBonus = colInt(s.s, 26);
            save.party.push_back(std::move(c));
        }
    };
    try { loadParty(true); }
    catch (const DbError&) { save.party.clear(); loadParty(false); }
    // Child lists keyed by owner index (position in save.party).
    auto fillList = [&](const char* sql, std::vector<std::string> Character::* field) {
        Stmt s(conn.db, sql);
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            size_t owner = (size_t)colInt(s.s, 0);
            if (owner < save.party.size()) (save.party[owner].*field).push_back(colText(s.s, 1));
        }
    };
    fillList("SELECT owner,name FROM save_character_training ORDER BY rowid;", &Character::trainings);
    fillList("SELECT owner,name FROM save_character_spell ORDER BY rowid;",    &Character::spells);

    // save_character_item gained description/image_id/image_path/quantity in v3, `value` in v4, and
    // the equip profile (slot/damage_die/defense_bonus/weapon_bonus/dropable) in v7. level 3 = newest,
    // 2 = +value, 1 = rich (no value), 0 = name only.
    auto fillItems = [&](int level) {
        const char* sql =
            level >= 3 ? "SELECT owner,name,description,image_id,image_path,quantity,value,"
                         "slot,damage_die,defense_bonus,weapon_bonus,dropable FROM save_character_item ORDER BY rowid;"
            : level >= 2 ? "SELECT owner,name,description,image_id,image_path,quantity,value FROM save_character_item ORDER BY rowid;"
            : level >= 1 ? "SELECT owner,name,description,image_id,image_path,quantity FROM save_character_item ORDER BY rowid;"
                         : "SELECT owner,name FROM save_character_item ORDER BY rowid;";
        Stmt s(conn.db, sql);
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            size_t owner = (size_t)colInt(s.s, 0);
            if (owner >= save.party.size()) continue;
            InventoryItem it;
            it.name = colText(s.s, 1);
            if (level >= 1) {
                it.description = colText(s.s, 2);
                it.imageId     = colText(s.s, 3);
                it.imagePath   = colText(s.s, 4);
                it.quantity    = colInt(s.s, 5);
                if (it.quantity < 1) it.quantity = 1;
                if (level >= 2) it.value = colInt(s.s, 6);
                if (level >= 3) {
                    it.slot         = colInt(s.s, 7);
                    it.damageDie    = colText(s.s, 8);
                    it.defenseBonus = colInt(s.s, 9);
                    it.weaponBonus  = colInt(s.s, 10);
                    it.dropable     = colInt(s.s, 11) != 0;
                }
            }
            save.party[owner].inventory.push_back(std::move(it));
        }
    };
    for (int level = 3; ; --level) {
        try { fillItems(level); break; }
        catch (const DbError&) {
            for (auto& c : save.party) c.inventory.clear();
            if (level == 0) break;
        }
    }
    // Item acquire/loss mutations (v5), keyed by (owner,item_ord): kind 0 = acquire, 1 = unacquire.
    try {
        Stmt s(conn.db, "SELECT owner,item_ord,kind,var_name,op,value FROM save_character_item_mutation "
                        "ORDER BY owner,item_ord,kind,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            size_t owner = (size_t)colInt(s.s, 0);
            int itemOrd = colInt(s.s, 1);
            if (owner < save.party.size() && itemOrd >= 0 && itemOrd < (int)save.party[owner].inventory.size()) {
                VarMutation m{colText(s.s, 3), colInt(s.s, 4), colText(s.s, 5)};
                if (colInt(s.s, 2) == 0) save.party[owner].inventory[itemOrd].onAcquire.push_back(std::move(m));
                else                     save.party[owner].inventory[itemOrd].onUnacquire.push_back(std::move(m));
            }
        }
    } catch (const DbError&) {}

    // save_deactivated_areas + save_deleted_contexts were added in v3; tolerate older saves.
    try {
        Stmt s(conn.db, "SELECT area_id FROM save_deactivated_areas;");
        while (sqlite3_step(s.s) == SQLITE_ROW) save.deactivatedAreas.insert(colInt(s.s, 0));
    } catch (const DbError&) {}
    try {
        Stmt s(conn.db, "SELECT area_id,ctx_name FROM save_deleted_contexts;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            save.deletedContexts.insert({colInt(s.s, 0), colText(s.s, 1)});
    } catch (const DbError&) {}
    // save_granted_dropable (v7): runtime dropable status of module granted items, by name.
    try {
        Stmt s(conn.db, "SELECT name,dropable FROM save_granted_dropable;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            save.grantedDropable[colText(s.s, 0)] = colInt(s.s, 1) != 0;
    } catch (const DbError&) {}

    return save;
}

} // namespace gns
