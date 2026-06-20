#include "gns/Module.h"
#include "gns/Database.h"   // gns::DbError
#include "sqlite3.h"

#include <algorithm>
#include <cstdint>
#include <cstring>

// .gnsmod is a standalone SQLite file. We open our own sqlite3 handle here (rather
// than the read-only gns::Database helper) because writing needs parameterized
// inserts, BLOB binds, and a transaction.

namespace gns {

// ---- Module helpers ---------------------------------------------------------

int Module::nextMapId() const {
    int n = 0;
    for (const auto& m : maps) n = std::max(n, m.id);
    return n + 1;
}

int Module::nextAreaId() const {
    int n = 0;
    for (const auto& m : maps)
        for (const auto& a : m.areas) n = std::max(n, a.id);
    return n + 1;
}

int Module::nextControlPointId() const {
    int n = 0;
    for (const auto& cp : controlPoints) n = std::max(n, cp.id);
    return n + 1;
}

int Module::nextObjectId() const {
    int n = 0;
    for (const auto& m : maps)
        for (const auto& o : m.objects) n = std::max(n, o.id);
    return n + 1;
}

Map* Module::mapById(int id) {
    for (auto& m : maps)
        if (m.id == id) return &m;
    return nullptr;
}

Area* Module::areaById(int id) {
    for (auto& m : maps)
        for (auto& a : m.areas)
            if (a.id == id) return &a;
    return nullptr;
}

// ---- sqlite helpers ---------------------------------------------------------

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

    Stmt& bind(int v)             { sqlite3_bind_int(s, idx++, v); return *this; }
    Stmt& bind(double v)          { sqlite3_bind_double(s, idx++, v); return *this; }
    Stmt& bind(const std::string& v) {
        sqlite3_bind_text(s, idx++, v.c_str(), -1, SQLITE_TRANSIENT); return *this;
    }
    Stmt& bindBlob(const std::vector<int>& v) {
        sqlite3_bind_blob(s, idx++, v.data(),
                          static_cast<int>(v.size() * sizeof(int)), SQLITE_TRANSIENT);
        return *this;
    }
    void run() {                      // for INSERT/UPDATE
        if (sqlite3_step(s) != SQLITE_DONE) fail(db, "step failed");
        sqlite3_reset(s);
        sqlite3_clear_bindings(s);
        idx = 1;
    }
};

// Column readers for SELECTs.
int colInt(sqlite3_stmt* s, int i) { return sqlite3_column_int(s, i); }
double colDouble(sqlite3_stmt* s, int i) { return sqlite3_column_double(s, i); }
std::string colText(sqlite3_stmt* s, int i) {
    const unsigned char* t = sqlite3_column_text(s, i);
    return t ? reinterpret_cast<const char*>(t) : "";
}
std::vector<int> colIntBlob(sqlite3_stmt* s, int i) {
    const void* data = sqlite3_column_blob(s, i);
    int bytes = sqlite3_column_bytes(s, i);
    std::vector<int> out(static_cast<size_t>(bytes) / sizeof(int));
    if (data && bytes > 0) std::memcpy(out.data(), data, static_cast<size_t>(bytes));
    return out;
}

const char* kSchema = R"sql(
CREATE TABLE module (
    id            INTEGER PRIMARY KEY CHECK (id = 1),
    name          TEXT, author TEXT, summary TEXT,
    start_map_id  INTEGER, start_area_id INTEGER, end_area_id INTEGER
);
CREATE TABLE maps (
    id        INTEGER PRIMARY KEY,
    name      TEXT,
    grid_w    INTEGER, grid_h INTEGER,
    overlay_w INTEGER, overlay_h INTEGER,
    cells     BLOB,
    cell_area BLOB
);
CREATE TABLE areas (
    id              INTEGER PRIMARY KEY,
    map_id          INTEGER,
    label           TEXT, name TEXT, color INTEGER,
    dm_text         TEXT, player_text TEXT,
    monster_chance  INTEGER, monster_type TEXT,
    treasure_chance INTEGER, treasure_type TEXT,
    trap_chance     INTEGER, trap_desc TEXT,
    lock_chance     INTEGER, lock_desc TEXT,
    hidden_chance   INTEGER, hidden_desc TEXT,
    artwork_path    TEXT
);
CREATE TABLE control_points (
    id INTEGER PRIMARY KEY,
    name TEXT, description TEXT,
    map_id INTEGER, area_id INTEGER
);
CREATE TABLE area_prerequisites (
    area_id          INTEGER,
    control_point_id INTEGER
);
CREATE TABLE map_objects (
    id     INTEGER PRIMARY KEY,
    map_id INTEGER,
    type   INTEGER,
    x      REAL, y REAL
);
)sql";

} // namespace

// ---- save -------------------------------------------------------------------

void saveModule(const Module& mod, const std::string& path) {
    Conn c;
    int rc = sqlite3_open_v2(path.c_str(), &c.db,
                             SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
    if (rc != SQLITE_OK) fail(c.db, "cannot create '" + path + "'");

    // Start clean: drop any prior content so save is a full overwrite.
    exec(c.db, "PRAGMA foreign_keys=OFF;");
    exec(c.db,
         "DROP TABLE IF EXISTS map_objects;"
         "DROP TABLE IF EXISTS area_prerequisites;"
         "DROP TABLE IF EXISTS control_points;"
         "DROP TABLE IF EXISTS areas;"
         "DROP TABLE IF EXISTS maps;"
         "DROP TABLE IF EXISTS module;");
    exec(c.db, kSchema);
    exec(c.db, ("PRAGMA user_version=" + std::to_string(kModuleFormatVersion) + ";").c_str());
    exec(c.db, "BEGIN;");

    {
        Stmt s(c.db, "INSERT INTO module(id,name,author,summary,"
                     "start_map_id,start_area_id,end_area_id) VALUES(1,?,?,?,?,?,?);");
        s.bind(mod.name).bind(mod.author).bind(mod.summary)
         .bind(mod.startMapId).bind(mod.startAreaId).bind(mod.endAreaId);
        s.run();
    }

    {
        Stmt mapStmt(c.db, "INSERT INTO maps(id,name,grid_w,grid_h,overlay_w,overlay_h,"
                           "cells,cell_area) VALUES(?,?,?,?,?,?,?,?);");
        Stmt areaStmt(c.db,
            "INSERT INTO areas(id,map_id,label,name,color,dm_text,player_text,"
            "monster_chance,monster_type,treasure_chance,treasure_type,"
            "trap_chance,trap_desc,lock_chance,lock_desc,hidden_chance,hidden_desc,"
            "artwork_path) VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        Stmt prereqStmt(c.db,
            "INSERT INTO area_prerequisites(area_id,control_point_id) VALUES(?,?);");
        Stmt objStmt(c.db,
            "INSERT INTO map_objects(id,map_id,type,x,y) VALUES(?,?,?,?,?);");

        for (const auto& m : mod.maps) {
            mapStmt.bind(m.id).bind(m.name).bind(m.gridW).bind(m.gridH)
                   .bind(m.overlayW).bind(m.overlayH).bindBlob(m.cells).bindBlob(m.cellArea);
            mapStmt.run();

            for (const auto& o : m.objects) {
                objStmt.bind(o.id).bind(m.id).bind(o.type)
                       .bind((double)o.x).bind((double)o.y);
                objStmt.run();
            }

            for (const auto& a : m.areas) {
                areaStmt.bind(a.id).bind(m.id).bind(a.label).bind(a.name)
                        .bind(static_cast<int>(a.color))
                        .bind(a.dmText).bind(a.playerText)
                        .bind(a.monsterChancePct).bind(a.monsterType)
                        .bind(a.treasureChancePct).bind(a.treasureType)
                        .bind(a.trapChancePct).bind(a.trapDescription)
                        .bind(a.lockChancePct).bind(a.lockDescription)
                        .bind(a.hiddenChancePct).bind(a.hiddenDescription)
                        .bind(a.artworkPath);
                areaStmt.run();

                for (int cpId : a.prerequisiteControlPointIds) {
                    prereqStmt.bind(a.id).bind(cpId);
                    prereqStmt.run();
                }
            }
        }
    }

    {
        Stmt s(c.db, "INSERT INTO control_points(id,name,description,map_id,area_id) "
                     "VALUES(?,?,?,?,?);");
        for (const auto& cp : mod.controlPoints) {
            s.bind(cp.id).bind(cp.name).bind(cp.description).bind(cp.mapId).bind(cp.areaId);
            s.run();
        }
    }

    exec(c.db, "COMMIT;");
}

// ---- load -------------------------------------------------------------------

Module loadModule(const std::string& path) {
    Conn c;
    if (sqlite3_open_v2(path.c_str(), &c.db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        fail(c.db, "cannot open '" + path + "'");

    Module mod;

    {
        Stmt s(c.db, "SELECT name,author,summary,start_map_id,start_area_id,end_area_id "
                     "FROM module WHERE id=1;");
        if (sqlite3_step(s.s) != SQLITE_ROW) fail(c.db, "module row missing");
        mod.name        = colText(s.s, 0);
        mod.author      = colText(s.s, 1);
        mod.summary     = colText(s.s, 2);
        mod.startMapId  = colInt(s.s, 3);
        mod.startAreaId = colInt(s.s, 4);
        mod.endAreaId   = colInt(s.s, 5);
    }

    {
        Stmt s(c.db, "SELECT id,name,grid_w,grid_h,overlay_w,overlay_h,cells,cell_area "
                     "FROM maps ORDER BY id;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            Map m;
            m.id       = colInt(s.s, 0);
            m.name     = colText(s.s, 1);
            m.gridW    = colInt(s.s, 2);
            m.gridH    = colInt(s.s, 3);
            m.overlayW = colInt(s.s, 4);
            m.overlayH = colInt(s.s, 5);
            m.cells    = colIntBlob(s.s, 6);
            m.cellArea = colIntBlob(s.s, 7);
            mod.maps.push_back(std::move(m));
        }
    }

    {
        Stmt s(c.db, "SELECT id,map_id,label,name,color,dm_text,player_text,"
                     "monster_chance,monster_type,treasure_chance,treasure_type,"
                     "trap_chance,trap_desc,lock_chance,lock_desc,hidden_chance,"
                     "hidden_desc,artwork_path FROM areas ORDER BY id;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            Area a;
            a.id                = colInt(s.s, 0);
            int mapId           = colInt(s.s, 1);
            a.label             = colText(s.s, 2);
            a.name              = colText(s.s, 3);
            a.color             = static_cast<std::uint32_t>(colInt(s.s, 4));
            a.dmText            = colText(s.s, 5);
            a.playerText        = colText(s.s, 6);
            a.monsterChancePct  = colInt(s.s, 7);
            a.monsterType       = colText(s.s, 8);
            a.treasureChancePct = colInt(s.s, 9);
            a.treasureType      = colText(s.s, 10);
            a.trapChancePct     = colInt(s.s, 11);
            a.trapDescription   = colText(s.s, 12);
            a.lockChancePct     = colInt(s.s, 13);
            a.lockDescription   = colText(s.s, 14);
            a.hiddenChancePct   = colInt(s.s, 15);
            a.hiddenDescription = colText(s.s, 16);
            a.artworkPath       = colText(s.s, 17);
            if (Map* m = mod.mapById(mapId)) m->areas.push_back(std::move(a));
        }
    }

    {
        Stmt s(c.db, "SELECT area_id,control_point_id FROM area_prerequisites;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0)))
                a->prerequisiteControlPointIds.push_back(colInt(s.s, 1));
        }
    }

    {
        Stmt s(c.db, "SELECT id,name,description,map_id,area_id "
                     "FROM control_points ORDER BY id;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            ControlPoint cp;
            cp.id          = colInt(s.s, 0);
            cp.name        = colText(s.s, 1);
            cp.description = colText(s.s, 2);
            cp.mapId       = colInt(s.s, 3);
            cp.areaId      = colInt(s.s, 4);
            mod.controlPoints.push_back(std::move(cp));
        }
    }

    // map_objects was added in format v2; tolerate older files that lack it.
    try {
        Stmt s(c.db, "SELECT id,map_id,type,x,y FROM map_objects ORDER BY id;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            MapObject o;
            o.id   = colInt(s.s, 0);
            int mapId = colInt(s.s, 1);
            o.type = colInt(s.s, 2);
            o.x    = (float)colDouble(s.s, 3);
            o.y    = (float)colDouble(s.s, 4);
            if (Map* m = mod.mapById(mapId)) m->objects.push_back(o);
        }
    } catch (const DbError&) {
        // no map_objects table — leave objects empty
    }

    return mod;
}

} // namespace gns
