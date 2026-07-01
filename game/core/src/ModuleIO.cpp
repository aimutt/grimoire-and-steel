#include "gns/Module.h"
#include "gns/Database.h"   // gns::DbError
#include "gns/AtomicDb.h"   // gns::writeDatabaseAtomically
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

int Module::nextTextId() const {
    int n = 0;
    for (const auto& m : maps)
        for (const auto& t : m.texts) n = std::max(n, t.id);
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
    start_map_id  INTEGER, start_area_id INTEGER, end_area_id INTEGER,
    cover_art     TEXT,
    splash_music  TEXT, default_music TEXT
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
    artwork_path    TEXT,
    fill_enabled    INTEGER,
    label_auto      INTEGER,
    is_shop         INTEGER,
    music           TEXT,
    hidden          INTEGER
);
CREATE TABLE area_images (
    area_id    INTEGER,
    slot       INTEGER,
    path       TEXT,
    direction  INTEGER,
    is_default INTEGER
);
CREATE TABLE area_monsters (
    area_id INTEGER,
    type    TEXT,
    count   INTEGER
);
CREATE TABLE area_treasures (
    area_id INTEGER,
    type    TEXT,
    chance  INTEGER
);
CREATE TABLE area_shop_items (
    area_id     INTEGER,
    name        TEXT,
    description TEXT,
    cost        INTEGER,
    stock       INTEGER,
    image       TEXT,
    image_id    TEXT
);
CREATE TABLE area_transitions (
    area_id        INTEGER,
    target_area_id INTEGER,
    label          TEXT
);
CREATE TABLE control_points (
    id INTEGER PRIMARY KEY,
    name TEXT, description TEXT,
    map_id INTEGER, area_id INTEGER,
    kind INTEGER,
    x REAL, y REAL
);
CREATE TABLE area_prerequisites (
    area_id          INTEGER,
    control_point_id INTEGER
);
CREATE TABLE map_objects (
    id     INTEGER PRIMARY KEY,
    map_id INTEGER,
    type   INTEGER,
    x      REAL, y REAL,
    rot    REAL,
    scale  REAL
);
CREATE TABLE map_texts (
    id     INTEGER PRIMARY KEY,
    map_id INTEGER,
    x      REAL, y REAL,
    text   TEXT,
    color  INTEGER,
    size   REAL
);
)sql";

} // namespace

// ---- save -------------------------------------------------------------------

void saveModule(const Module& mod, const std::string& path) {
  // Written to a temp file inside one transaction, then atomically swapped in (see
  // writeDatabaseAtomically). A failure here never damages the existing file: the temp is
  // discarded and `path` is left untouched. Because the target is always a fresh file there
  // is no destructive DROP — we just CREATE the schema and INSERT.
  writeDatabaseAtomically(path, [&](sqlite3* db) {
    exec(db, kSchema);
    exec(db, ("PRAGMA user_version=" + std::to_string(kModuleFormatVersion) + ";").c_str());

    {
        Stmt s(db, "INSERT INTO module(id,name,author,summary,"
                     "start_map_id,start_area_id,end_area_id,cover_art,splash_music,default_music) "
                     "VALUES(1,?,?,?,?,?,?,?,?,?);");
        s.bind(mod.name).bind(mod.author).bind(mod.summary)
         .bind(mod.startMapId).bind(mod.startAreaId).bind(mod.endAreaId).bind(mod.coverArtPath)
         .bind(mod.splashMusicPath).bind(mod.defaultMusicPath);
        s.run();
    }

    {
        Stmt mapStmt(db, "INSERT INTO maps(id,name,grid_w,grid_h,overlay_w,overlay_h,"
                           "cells,cell_area) VALUES(?,?,?,?,?,?,?,?);");
        Stmt areaStmt(db,
            "INSERT INTO areas(id,map_id,label,name,color,dm_text,player_text,"
            "monster_chance,monster_type,treasure_chance,treasure_type,"
            "trap_chance,trap_desc,lock_chance,lock_desc,hidden_chance,hidden_desc,"
            "artwork_path,fill_enabled,label_auto,is_shop,music,hidden) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        Stmt prereqStmt(db,
            "INSERT INTO area_prerequisites(area_id,control_point_id) VALUES(?,?);");
        Stmt monsterStmt(db,
            "INSERT INTO area_monsters(area_id,type,count) VALUES(?,?,?);");
        Stmt treasureStmt(db,
            "INSERT INTO area_treasures(area_id,type,chance) VALUES(?,?,?);");
        Stmt shopStmt(db,
            "INSERT INTO area_shop_items(area_id,name,description,cost,stock,image,image_id) "
            "VALUES(?,?,?,?,?,?,?);");
        Stmt imageStmt(db,
            "INSERT INTO area_images(area_id,slot,path,direction,is_default) VALUES(?,?,?,?,?);");
        Stmt transStmt(db,
            "INSERT INTO area_transitions(area_id,target_area_id,label) VALUES(?,?,?);");
        Stmt objStmt(db,
            "INSERT INTO map_objects(id,map_id,type,x,y,rot,scale) VALUES(?,?,?,?,?,?,?);");
        Stmt textStmt(db,
            "INSERT INTO map_texts(id,map_id,x,y,text,color,size) VALUES(?,?,?,?,?,?,?);");

        for (const auto& m : mod.maps) {
            mapStmt.bind(m.id).bind(m.name).bind(m.gridW).bind(m.gridH)
                   .bind(m.overlayW).bind(m.overlayH).bindBlob(m.cells).bindBlob(m.cellArea);
            mapStmt.run();

            for (const auto& o : m.objects) {
                objStmt.bind(o.id).bind(m.id).bind(o.type)
                       .bind((double)o.x).bind((double)o.y).bind((double)o.rotationDeg)
                       .bind((double)o.scale);
                objStmt.run();
            }

            for (const auto& tx : m.texts) {
                textStmt.bind(tx.id).bind(m.id).bind((double)tx.x).bind((double)tx.y)
                        .bind(tx.text).bind(static_cast<int>(tx.color)).bind((double)tx.sizePx);
                textStmt.run();
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
                        .bind(a.artworkPath).bind(a.fillEnabled ? 1 : 0).bind(a.labelAuto ? 1 : 0)
                        .bind(a.isShop ? 1 : 0).bind(a.musicPath).bind(a.hidden ? 1 : 0);
                areaStmt.run();

                for (size_t i = 0; i < a.images.size(); ++i) {
                    imageStmt.bind(a.id).bind((int)i).bind(a.images[i].path)
                             .bind(a.images[i].direction).bind((int)i == a.defaultImage ? 1 : 0);
                    imageStmt.run();
                }

                for (int cpId : a.prerequisiteControlPointIds) {
                    prereqStmt.bind(a.id).bind(cpId);
                    prereqStmt.run();
                }

                for (const auto& am : a.monsters) {
                    monsterStmt.bind(a.id).bind(am.type).bind(am.count);
                    monsterStmt.run();
                }

                for (const auto& at : a.treasures) {
                    treasureStmt.bind(a.id).bind(at.type).bind(at.chancePct);
                    treasureStmt.run();
                }

                for (const auto& si : a.shopItems) {
                    shopStmt.bind(a.id).bind(si.name).bind(si.description)
                            .bind(si.costGp).bind(si.stock).bind(si.imagePath).bind(si.imageId);
                    shopStmt.run();
                }

                for (const auto& tr : a.transitions) {
                    transStmt.bind(a.id).bind(tr.targetAreaId).bind(tr.label);
                    transStmt.run();
                }
            }
        }
    }

    {
        Stmt s(db, "INSERT INTO control_points(id,name,description,map_id,area_id,kind,x,y) "
                   "VALUES(?,?,?,?,?,?,?,?);");
        for (const auto& cp : mod.controlPoints) {
            s.bind(cp.id).bind(cp.name).bind(cp.description).bind(cp.mapId).bind(cp.areaId)
             .bind(cp.kind).bind((double)cp.x).bind((double)cp.y);
            s.run();
        }
    }
  });
}

// ---- load -------------------------------------------------------------------

Module loadModule(const std::string& path) {
    Conn c;
    if (sqlite3_open_v2(path.c_str(), &c.db, SQLITE_OPEN_READONLY, nullptr) != SQLITE_OK)
        fail(c.db, "cannot open '" + path + "'");

    Module mod;

    // module gained cover_art in v8 and splash_music/default_music in v12. Try the newest
    // layout first, falling back tier by tier so older files still load.
    //   opt 2 = v12 (cover_art + splash_music + default_music), 1 = v8 (cover_art), 0 = v7.
    auto loadModuleRow = [&](int opt) {
        std::string sql = "SELECT name,author,summary,start_map_id,start_area_id,end_area_id";
        if (opt >= 1) sql += ",cover_art";
        if (opt >= 2) sql += ",splash_music,default_music";
        sql += " FROM module WHERE id=1;";
        Stmt s(c.db, sql.c_str());
        if (sqlite3_step(s.s) != SQLITE_ROW) fail(c.db, "module row missing");
        mod.name        = colText(s.s, 0);
        mod.author      = colText(s.s, 1);
        mod.summary     = colText(s.s, 2);
        mod.startMapId  = colInt(s.s, 3);
        mod.startAreaId = colInt(s.s, 4);
        mod.endAreaId   = colInt(s.s, 5);
        if (opt >= 1) mod.coverArtPath = colText(s.s, 6);
        if (opt >= 2) { mod.splashMusicPath = colText(s.s, 7); mod.defaultMusicPath = colText(s.s, 8); }
    };
    for (int opt = 2; ; --opt) {
        try { loadModuleRow(opt); break; }
        catch (const DbError&) { if (opt == 0) throw; }
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

    // The areas table grew optional trailing columns over time: fill_enabled (v6),
    // label_auto (v9), is_shop (v10), music (v12). `opt` = how many of those trailing columns
    // the file has; we try the newest layout first and fall back one column at a time so older
    // files still load (older rows default fillEnabled=true, labelAuto=false, isShop=false,
    // music="").
    static const char* kAreaOptCols[] = {"fill_enabled", "label_auto", "is_shop", "music", "hidden"};
    auto loadAreas = [&](int opt) {
        std::string sql =
            "SELECT id,map_id,label,name,color,dm_text,player_text,"
            "monster_chance,monster_type,treasure_chance,treasure_type,"
            "trap_chance,trap_desc,lock_chance,lock_desc,hidden_chance,"
            "hidden_desc,artwork_path";
        for (int i = 0; i < opt; ++i) { sql += ","; sql += kAreaOptCols[i]; }
        sql += " FROM areas ORDER BY id;";
        Stmt s(c.db, sql.c_str());
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
            a.fillEnabled       = opt >= 1 ? (colInt(s.s, 18) != 0) : true;
            a.labelAuto         = opt >= 2 ? (colInt(s.s, 19) != 0) : false;
            a.isShop            = opt >= 3 ? (colInt(s.s, 20) != 0) : false;
            a.musicPath         = opt >= 4 ? colText(s.s, 21) : "";
            a.hidden            = opt >= 5 ? (colInt(s.s, 22) != 0) : false;
            if (Map* m = mod.mapById(mapId)) m->areas.push_back(std::move(a));
        }
    };
    for (int opt = 5; ; --opt) {
        try { loadAreas(opt); break; }
        catch (const DbError&) {
            for (auto& m : mod.maps) m.areas.clear();   // partial read from failed attempt
            if (opt == 0) throw;                         // genuinely unreadable areas table
        }
    }

    // area_monsters was added in v6; tolerate older files that lack it.
    try {
        Stmt s(c.db, "SELECT area_id,type,count FROM area_monsters;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0)))
                a->monsters.push_back(AreaMonster{colText(s.s, 1), colInt(s.s, 2)});
        }
    } catch (const DbError&) {
        // no area_monsters table — leave lists empty, migration below fills them
    }

    // area_transitions was added in v7; tolerate older files that lack it.
    try {
        Stmt s(c.db, "SELECT area_id,target_area_id,label FROM area_transitions;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0)))
                a->transitions.push_back(AreaTransition{colInt(s.s, 1), colText(s.s, 2)});
        }
    } catch (const DbError&) {
        // no area_transitions table — leave lists empty
    }

    // area_treasures was added in v10; tolerate older files that lack it.
    try {
        Stmt s(c.db, "SELECT area_id,type,chance FROM area_treasures;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0)))
                a->treasures.push_back(AreaTreasure{colText(s.s, 1), colInt(s.s, 2)});
        }
    } catch (const DbError&) {
        // no area_treasures table — leave lists empty, migration below fills them
    }

    // area_shop_items was added in v10 (name,cost); v11 added description, stock, image; v13
    // added image_id. `level` 2 = newest, 1 = v11, 0 = v10; fall back tier by tier.
    auto loadShop = [&](int level) {
        const char* sql =
            level >= 2 ? "SELECT area_id,name,description,cost,stock,image,image_id FROM area_shop_items;"
            : level >= 1 ? "SELECT area_id,name,description,cost,stock,image FROM area_shop_items;"
                         : "SELECT area_id,name,cost FROM area_shop_items;";
        Stmt s(c.db, sql);
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            Area* a = mod.areaById(colInt(s.s, 0));
            if (!a) continue;
            ShopItem si;
            si.name = colText(s.s, 1);
            if (level >= 1) {
                si.description = colText(s.s, 2);
                si.costGp      = colInt(s.s, 3);
                si.stock       = colInt(s.s, 4);
                si.imagePath   = colText(s.s, 5);
                if (level >= 2) si.imageId = colText(s.s, 6);
            } else {
                si.costGp = colInt(s.s, 2);
            }
            a->shopItems.push_back(std::move(si));
        }
    };
    for (int level = 2; ; --level) {
        try { loadShop(level); break; }
        catch (const DbError&) {
            for (auto& m : mod.maps) for (auto& a : m.areas) a.shopItems.clear();
            if (level == 0) break;   // no table at all — leave empty
        }
    }

    // area_images was added in v13; tolerate older files. Migration (below) synthesizes a
    // single default image from the legacy artwork_path when no rows exist.
    try {
        Stmt s(c.db, "SELECT area_id,slot,path,direction,is_default FROM area_images ORDER BY area_id,slot;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0))) {
                if (colInt(s.s, 4) != 0) a->defaultImage = (int)a->images.size();
                a->images.push_back(AreaImage{colText(s.s, 2), colInt(s.s, 3)});
            }
        }
    } catch (const DbError&) {
        // no area_images table — migration below handles it
    }
    for (auto& m : mod.maps)
        for (auto& a : m.areas)
            if (a.images.empty() && !a.artworkPath.empty()) {
                a.images.push_back(AreaImage{a.artworkPath, -1});
                a.defaultImage = 0;
            }

    // Migrate legacy single monster/treasure into the lists so old modules behave the same.
    for (auto& m : mod.maps)
        for (auto& a : m.areas) {
            if (a.monsters.empty() && !a.monsterType.empty())
                a.monsters.push_back(AreaMonster{a.monsterType, 1});
            if (a.treasures.empty() && !a.treasureType.empty())
                a.treasures.push_back(AreaTreasure{a.treasureType, a.treasureChancePct});
        }

    {
        Stmt s(c.db, "SELECT area_id,control_point_id FROM area_prerequisites;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0)))
                a->prerequisiteControlPointIds.push_back(colInt(s.s, 1));
        }
    }

    // control_points gained x,y in v4 and a kind column in v5. Try the newest layout
    // first, then fall back column-set by column-set (legacy files keep the -1 sentinel
    // so the editor renders them at area centroids, and default kind = 0 = Control Point).
    auto loadControlPoints = [&](bool withKind, bool withXy) {
        const char* sql =
            withKind ? "SELECT id,name,description,map_id,area_id,kind,x,y FROM control_points ORDER BY id;"
            : withXy ? "SELECT id,name,description,map_id,area_id,x,y FROM control_points ORDER BY id;"
                     : "SELECT id,name,description,map_id,area_id FROM control_points ORDER BY id;";
        Stmt s(c.db, sql);
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            ControlPoint cp;
            cp.id          = colInt(s.s, 0);
            cp.name        = colText(s.s, 1);
            cp.description = colText(s.s, 2);
            cp.mapId       = colInt(s.s, 3);
            cp.areaId      = colInt(s.s, 4);
            if (withKind) {
                cp.kind = colInt(s.s, 5);
                cp.x = (float)colDouble(s.s, 6); cp.y = (float)colDouble(s.s, 7);
            } else if (withXy) {
                cp.x = (float)colDouble(s.s, 5); cp.y = (float)colDouble(s.s, 6);
            }
            mod.controlPoints.push_back(std::move(cp));
        }
    };
    try {
        loadControlPoints(true, true);                 // v5
    } catch (const DbError&) {
        mod.controlPoints.clear();                     // partial read from the failed attempt
        try {
            loadControlPoints(false, true);            // v4
        } catch (const DbError&) {
            mod.controlPoints.clear();
            loadControlPoints(false, false);           // v3 and older
        }
    }

    // map_objects was added in v2, gained rot in v3, and scale in v13. `level` 2 = with scale,
    // 1 = with rot, 0 = neither; fall back tier by tier, then tolerate no table at all.
    auto loadObjects = [&](int level) {
        const char* sql =
            level >= 2 ? "SELECT id,map_id,type,x,y,rot,scale FROM map_objects ORDER BY id;"
            : level >= 1 ? "SELECT id,map_id,type,x,y,rot FROM map_objects ORDER BY id;"
                         : "SELECT id,map_id,type,x,y FROM map_objects ORDER BY id;";
        Stmt s(c.db, sql);
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            MapObject o;
            o.id   = colInt(s.s, 0);
            int mapId = colInt(s.s, 1);
            o.type = colInt(s.s, 2);
            o.x    = (float)colDouble(s.s, 3);
            o.y    = (float)colDouble(s.s, 4);
            o.rotationDeg = level >= 1 ? (float)colDouble(s.s, 5) : 0.0f;
            o.scale = level >= 2 ? (float)colDouble(s.s, 6) : 1.0f;
            if (o.scale <= 0.0f) o.scale = 1.0f;
            if (Map* m = mod.mapById(mapId)) m->objects.push_back(o);
        }
    };
    for (int level = 2; ; --level) {
        try { loadObjects(level); break; }
        catch (const DbError&) {
            for (auto& m : mod.maps) m.objects.clear();
            if (level == 0) break;   // no map_objects table (v1)
        }
    }

    // map_texts was added in v4; tolerate older files that lack it.
    try {
        Stmt s(c.db, "SELECT id,map_id,x,y,text,color,size FROM map_texts ORDER BY id;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            MapText tx;
            tx.id    = colInt(s.s, 0);
            int mapId = colInt(s.s, 1);
            tx.x     = (float)colDouble(s.s, 2);
            tx.y     = (float)colDouble(s.s, 3);
            tx.text  = colText(s.s, 4);
            tx.color = static_cast<std::uint32_t>(colInt(s.s, 5));
            tx.sizePx = (float)colDouble(s.s, 6);
            if (Map* m = mod.mapById(mapId)) m->texts.push_back(std::move(tx));
        }
    } catch (const DbError&) {
        // no map_texts table — leave texts empty
    }

    return mod;
}

} // namespace gns
