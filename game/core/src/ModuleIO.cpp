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

std::vector<std::pair<std::string, bool>> collectGrantedItems(const Module& mod) {
    std::vector<std::pair<std::string, bool>> out;
    for (const auto& m : mod.maps)
        for (const auto& a : m.areas)
            for (const auto& ctx : a.contexts)
                for (const auto& ch : ctx.choices) {
                    const std::string& nm = ch.grantItem.name;
                    if (nm.empty()) continue;
                    bool seen = false;
                    for (const auto& p : out) if (p.first == nm) { seen = true; break; }
                    if (!seen) out.emplace_back(nm, ch.grantItem.dropable);
                }
    return out;
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
    hidden          INTEGER,
    choice_prompt   TEXT
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
CREATE TABLE area_choices (
    area_id    INTEGER,
    ord        INTEGER,
    label      TEXT,
    journal    TEXT,
    set_flag   TEXT,
    cp_id      INTEGER,
    gold       INTEGER,
    grant_item TEXT,
    take_item  TEXT
);
CREATE TABLE area_alt_texts (
    area_id INTEGER,
    ord     INTEGER,
    flag    TEXT,
    text    TEXT
);
CREATE TABLE module_variables (
    ord           INTEGER,
    name          TEXT,
    type          INTEGER,
    default_value TEXT
);
CREATE TABLE area_contexts (
    area_id         INTEGER,
    ctx_ord         INTEGER,
    name            TEXT,
    dm_text         TEXT, player_text TEXT,
    monster_chance  INTEGER, monster_type TEXT,
    treasure_chance INTEGER, treasure_type TEXT,
    trap_chance     INTEGER, trap_desc TEXT,
    lock_chance     INTEGER, lock_desc TEXT,
    hidden_chance   INTEGER, hidden_desc TEXT,
    artwork_path    TEXT,
    default_image   INTEGER,
    music           TEXT,
    is_shop         INTEGER,
    choice_prompt   TEXT
);
CREATE TABLE context_conditions (
    area_id  INTEGER, ctx_ord INTEGER, ord INTEGER,
    var_name TEXT, op INTEGER, value TEXT
);
CREATE TABLE context_monsters (
    area_id INTEGER, ctx_ord INTEGER,
    type    TEXT, count INTEGER
);
CREATE TABLE context_treasures (
    area_id INTEGER, ctx_ord INTEGER,
    type    TEXT, chance INTEGER
);
CREATE TABLE context_images (
    area_id INTEGER, ctx_ord INTEGER, slot INTEGER,
    path    TEXT, direction INTEGER, is_default INTEGER
);
CREATE TABLE context_shop_items (
    area_id     INTEGER, ctx_ord INTEGER,
    name        TEXT, description TEXT, cost INTEGER, stock INTEGER,
    image       TEXT, image_id TEXT
);
CREATE TABLE context_transitions (
    area_id INTEGER, ctx_ord INTEGER,
    target_area_id INTEGER, label TEXT
);
CREATE TABLE context_choices (
    area_id    INTEGER, ctx_ord INTEGER, ord INTEGER,
    label      TEXT, journal TEXT, set_flag TEXT,
    cp_id      INTEGER, gold INTEGER, grant_item TEXT, take_item TEXT,
    grant_item_desc TEXT, grant_item_image TEXT, grant_item_path TEXT, grant_item_qty INTEGER,
    deactivate_area INTEGER, delete_context INTEGER,
    grant_slot INTEGER, grant_damage_die TEXT, grant_defense_bonus INTEGER, grant_weapon_bonus INTEGER,
    grant_dropable INTEGER
);
CREATE TABLE context_choice_dropable_sets (
    area_id INTEGER, ctx_ord INTEGER, choice_ord INTEGER, ord INTEGER,
    item_name TEXT, dropable INTEGER
);
CREATE TABLE context_choice_mutations (
    area_id    INTEGER, ctx_ord INTEGER, choice_ord INTEGER, ord INTEGER,
    var_name   TEXT, op INTEGER, value TEXT
);
CREATE TABLE context_shop_item_mutations (
    area_id  INTEGER, ctx_ord INTEGER, item_ord INTEGER,
    kind     INTEGER,           /* 0 = onAcquire, 1 = onUnacquire */
    ord      INTEGER,
    var_name TEXT, op INTEGER, value TEXT
);
CREATE TABLE context_choice_grant_mutations (
    area_id  INTEGER, ctx_ord INTEGER, choice_ord INTEGER,
    kind     INTEGER,           /* 0 = onAcquire, 1 = onUnacquire */
    ord      INTEGER,
    var_name TEXT, op INTEGER, value TEXT
);
CREATE TABLE context_alt_texts (
    area_id INTEGER, ctx_ord INTEGER, ord INTEGER,
    flag    TEXT, text TEXT
);
CREATE TABLE context_characters (
    area_id INTEGER, ctx_ord INTEGER, ord INTEGER, foe INTEGER,
    name TEXT, player_name TEXT, kin TEXT, calling TEXT, level INTEGER,
    might INTEGER, grace INTEGER, wits INTEGER, spirit INTEGER,
    max_life INTEGER, life INTEGER, defense INTEGER, ap INTEGER, strain INTEGER,
    armor_name TEXT, shield INTEGER,
    weapon_name TEXT, weapon_damage_die TEXT, weapon_bonus INTEGER,
    background TEXT, goal TEXT, personality TEXT, notes TEXT,
    portrait TEXT, gold INTEGER
);
CREATE TABLE context_character_training (area_id INTEGER, ctx_ord INTEGER, char_ord INTEGER, name TEXT);
CREATE TABLE context_character_spell    (area_id INTEGER, ctx_ord INTEGER, char_ord INTEGER, name TEXT);
CREATE TABLE context_character_item (
    area_id INTEGER, ctx_ord INTEGER, char_ord INTEGER,
    name TEXT, description TEXT, image_id TEXT, image_path TEXT, quantity INTEGER, value INTEGER
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
CREATE TABLE area_enter_mutations (
    area_id  INTEGER, ord INTEGER,
    var_name TEXT, op INTEGER, value TEXT
);
CREATE TABLE area_exit_mutations (
    area_id  INTEGER, ord INTEGER,
    var_name TEXT, op INTEGER, value TEXT
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
        Stmt s(db, "INSERT INTO module_variables(ord,name,type,default_value) VALUES(?,?,?,?);");
        for (size_t i = 0; i < mod.variables.size(); ++i) {
            const auto& v = mod.variables[i];
            s.bind((int)i).bind(v.name).bind(static_cast<int>(v.type)).bind(v.defaultValue);
            s.run();
        }
    }

    {
        Stmt mapStmt(db, "INSERT INTO maps(id,name,grid_w,grid_h,overlay_w,overlay_h,"
                           "cells,cell_area) VALUES(?,?,?,?,?,?,?,?);");
        Stmt areaStmt(db,
            "INSERT INTO areas(id,map_id,label,name,color,dm_text,player_text,"
            "monster_chance,monster_type,treasure_chance,treasure_type,"
            "trap_chance,trap_desc,lock_chance,lock_desc,hidden_chance,hidden_desc,"
            "artwork_path,fill_enabled,label_auto,is_shop,music,hidden,choice_prompt) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
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
        Stmt choiceStmt(db,
            "INSERT INTO area_choices(area_id,ord,label,journal,set_flag,cp_id,gold,grant_item,take_item) "
            "VALUES(?,?,?,?,?,?,?,?,?);");
        Stmt altTextStmt(db,
            "INSERT INTO area_alt_texts(area_id,ord,flag,text) VALUES(?,?,?,?);");
        Stmt ctxStmt(db,
            "INSERT INTO area_contexts(area_id,ctx_ord,name,dm_text,player_text,"
            "monster_chance,monster_type,treasure_chance,treasure_type,"
            "trap_chance,trap_desc,lock_chance,lock_desc,hidden_chance,hidden_desc,"
            "artwork_path,default_image,music,is_shop,choice_prompt) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        Stmt ctxCondStmt(db,
            "INSERT INTO context_conditions(area_id,ctx_ord,ord,var_name,op,value) "
            "VALUES(?,?,?,?,?,?);");
        Stmt ctxMonsterStmt(db,
            "INSERT INTO context_monsters(area_id,ctx_ord,type,count) VALUES(?,?,?,?);");
        Stmt ctxTreasureStmt(db,
            "INSERT INTO context_treasures(area_id,ctx_ord,type,chance) VALUES(?,?,?,?);");
        Stmt ctxImageStmt(db,
            "INSERT INTO context_images(area_id,ctx_ord,slot,path,direction,is_default) "
            "VALUES(?,?,?,?,?,?);");
        Stmt ctxShopStmt(db,
            "INSERT INTO context_shop_items(area_id,ctx_ord,name,description,cost,stock,image,image_id) "
            "VALUES(?,?,?,?,?,?,?,?);");
        Stmt ctxTransStmt(db,
            "INSERT INTO context_transitions(area_id,ctx_ord,target_area_id,label) VALUES(?,?,?,?);");
        Stmt ctxChoiceStmt(db,
            "INSERT INTO context_choices(area_id,ctx_ord,ord,label,journal,set_flag,cp_id,gold,grant_item,take_item,"
            "grant_item_desc,grant_item_image,grant_item_path,grant_item_qty,deactivate_area,delete_context,"
            "grant_slot,grant_damage_die,grant_defense_bonus,grant_weapon_bonus,grant_dropable) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        Stmt ctxChoiceMutStmt(db,
            "INSERT INTO context_choice_mutations(area_id,ctx_ord,choice_ord,ord,var_name,op,value) "
            "VALUES(?,?,?,?,?,?,?);");
        Stmt ctxDropSetStmt(db,
            "INSERT INTO context_choice_dropable_sets(area_id,ctx_ord,choice_ord,ord,item_name,dropable) "
            "VALUES(?,?,?,?,?,?);");
        Stmt areaEnterMutStmt(db,
            "INSERT INTO area_enter_mutations(area_id,ord,var_name,op,value) VALUES(?,?,?,?,?);");
        Stmt areaExitMutStmt(db,
            "INSERT INTO area_exit_mutations(area_id,ord,var_name,op,value) VALUES(?,?,?,?,?);");
        Stmt ctxShopMutStmt(db,
            "INSERT INTO context_shop_item_mutations(area_id,ctx_ord,item_ord,kind,ord,var_name,op,value) "
            "VALUES(?,?,?,?,?,?,?,?);");
        Stmt ctxGrantMutStmt(db,
            "INSERT INTO context_choice_grant_mutations(area_id,ctx_ord,choice_ord,kind,ord,var_name,op,value) "
            "VALUES(?,?,?,?,?,?,?,?);");
        Stmt ctxAltStmt(db,
            "INSERT INTO context_alt_texts(area_id,ctx_ord,ord,flag,text) VALUES(?,?,?,?,?);");
        Stmt ctxCharStmt(db,
            "INSERT INTO context_characters(area_id,ctx_ord,ord,foe,name,player_name,kin,calling,level,"
            "might,grace,wits,spirit,max_life,life,defense,ap,strain,armor_name,shield,"
            "weapon_name,weapon_damage_die,weapon_bonus,background,goal,personality,notes,portrait,gold) "
            "VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?);");
        Stmt ctxCharTrainStmt(db,
            "INSERT INTO context_character_training(area_id,ctx_ord,char_ord,name) VALUES(?,?,?,?);");
        Stmt ctxCharSpellStmt(db,
            "INSERT INTO context_character_spell(area_id,ctx_ord,char_ord,name) VALUES(?,?,?,?);");
        Stmt ctxCharItemStmt(db,
            "INSERT INTO context_character_item(area_id,ctx_ord,char_ord,name,description,image_id,"
            "image_path,quantity,value) VALUES(?,?,?,?,?,?,?,?,?);");
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
                        .bind(a.isShop ? 1 : 0).bind(a.musicPath).bind(a.hidden ? 1 : 0)
                        .bind(a.choicePrompt);
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

                for (size_t i = 0; i < a.onEnter.size(); ++i) {
                    const auto& mu = a.onEnter[i];
                    areaEnterMutStmt.bind(a.id).bind((int)i).bind(mu.varName).bind(mu.op).bind(mu.value);
                    areaEnterMutStmt.run();
                }
                for (size_t i = 0; i < a.onExit.size(); ++i) {
                    const auto& mu = a.onExit[i];
                    areaExitMutStmt.bind(a.id).bind((int)i).bind(mu.varName).bind(mu.op).bind(mu.value);
                    areaExitMutStmt.run();
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

                for (size_t i = 0; i < a.choices.size(); ++i) {
                    const auto& ch = a.choices[i];
                    choiceStmt.bind(a.id).bind((int)i).bind(ch.label).bind(ch.journalEntry)
                              .bind(ch.setFlag).bind(ch.completeControlPointId).bind(ch.goldDelta)
                              .bind(ch.grantItemName).bind(ch.takeItemName);
                    choiceStmt.run();
                }

                for (size_t i = 0; i < a.altTexts.size(); ++i) {
                    altTextStmt.bind(a.id).bind((int)i).bind(a.altTexts[i].requiredFlag)
                               .bind(a.altTexts[i].text);
                    altTextStmt.run();
                }

                // Per-area Contexts (v15) — the source of truth for all branchable content.
                for (size_t ci = 0; ci < a.contexts.size(); ++ci) {
                    const AreaContext& ctx = a.contexts[ci];
                    int co = (int)ci;
                    ctxStmt.bind(a.id).bind(co).bind(ctx.name)
                           .bind(ctx.dmText).bind(ctx.playerText)
                           .bind(ctx.monsterChancePct).bind(ctx.monsterType)
                           .bind(ctx.treasureChancePct).bind(ctx.treasureType)
                           .bind(ctx.trapChancePct).bind(ctx.trapDescription)
                           .bind(ctx.lockChancePct).bind(ctx.lockDescription)
                           .bind(ctx.hiddenChancePct).bind(ctx.hiddenDescription)
                           .bind(ctx.artworkPath).bind(ctx.defaultImage).bind(ctx.musicPath)
                           .bind(ctx.isShop ? 1 : 0).bind(ctx.choicePrompt);
                    ctxStmt.run();

                    for (size_t k = 0; k < ctx.conditions.size(); ++k) {
                        const auto& cc = ctx.conditions[k];
                        ctxCondStmt.bind(a.id).bind(co).bind((int)k)
                                   .bind(cc.varName).bind(cc.op).bind(cc.value);
                        ctxCondStmt.run();
                    }
                    for (const auto& am : ctx.monsters) {
                        ctxMonsterStmt.bind(a.id).bind(co).bind(am.type).bind(am.count);
                        ctxMonsterStmt.run();
                    }
                    for (const auto& at : ctx.treasures) {
                        ctxTreasureStmt.bind(a.id).bind(co).bind(at.type).bind(at.chancePct);
                        ctxTreasureStmt.run();
                    }
                    for (size_t k = 0; k < ctx.images.size(); ++k) {
                        ctxImageStmt.bind(a.id).bind(co).bind((int)k).bind(ctx.images[k].path)
                                    .bind(ctx.images[k].direction)
                                    .bind((int)k == ctx.defaultImage ? 1 : 0);
                        ctxImageStmt.run();
                    }
                    for (size_t si_i = 0; si_i < ctx.shopItems.size(); ++si_i) {
                        const auto& si = ctx.shopItems[si_i];
                        ctxShopStmt.bind(a.id).bind(co).bind(si.name).bind(si.description)
                                   .bind(si.costGp).bind(si.stock).bind(si.imagePath).bind(si.imageId);
                        ctxShopStmt.run();
                        for (size_t mi = 0; mi < si.onAcquire.size(); ++mi) {
                            const auto& mu = si.onAcquire[mi];
                            ctxShopMutStmt.bind(a.id).bind(co).bind((int)si_i).bind(0).bind((int)mi)
                                          .bind(mu.varName).bind(mu.op).bind(mu.value);
                            ctxShopMutStmt.run();
                        }
                        for (size_t mi = 0; mi < si.onUnacquire.size(); ++mi) {
                            const auto& mu = si.onUnacquire[mi];
                            ctxShopMutStmt.bind(a.id).bind(co).bind((int)si_i).bind(1).bind((int)mi)
                                          .bind(mu.varName).bind(mu.op).bind(mu.value);
                            ctxShopMutStmt.run();
                        }
                    }
                    for (const auto& tr : ctx.transitions) {
                        ctxTransStmt.bind(a.id).bind(co).bind(tr.targetAreaId).bind(tr.label);
                        ctxTransStmt.run();
                    }
                    for (size_t k = 0; k < ctx.choices.size(); ++k) {
                        const auto& ch = ctx.choices[k];
                        ctxChoiceStmt.bind(a.id).bind(co).bind((int)k).bind(ch.label)
                                     .bind(ch.journalEntry).bind(ch.setFlag)
                                     .bind(ch.completeControlPointId).bind(ch.goldDelta)
                                     .bind(ch.grantItem.name).bind(ch.takeItemName)
                                     .bind(ch.grantItem.description).bind(ch.grantItem.imageId)
                                     .bind(ch.grantItem.imagePath).bind(ch.grantItem.quantity)
                                     .bind(ch.deactivateArea ? 1 : 0).bind(ch.deleteContext ? 1 : 0)
                                     .bind(ch.grantItem.slot).bind(ch.grantItem.damageDie)
                                     .bind(ch.grantItem.defenseBonus).bind(ch.grantItem.weaponBonus)
                                     .bind(ch.grantItem.dropable ? 1 : 0);
                        ctxChoiceStmt.run();
                        for (size_t mi = 0; mi < ch.mutations.size(); ++mi) {
                            const auto& mu = ch.mutations[mi];
                            ctxChoiceMutStmt.bind(a.id).bind(co).bind((int)k).bind((int)mi)
                                            .bind(mu.varName).bind(mu.op).bind(mu.value);
                            ctxChoiceMutStmt.run();
                        }
                        for (size_t di = 0; di < ch.dropableSets.size(); ++di) {
                            const auto& ds = ch.dropableSets[di];
                            ctxDropSetStmt.bind(a.id).bind(co).bind((int)k).bind((int)di)
                                          .bind(ds.first).bind(ds.second ? 1 : 0);
                            ctxDropSetStmt.run();
                        }
                        // The granted item's own acquire/loss hooks (kind 0/1).
                        for (size_t mi = 0; mi < ch.grantItem.onAcquire.size(); ++mi) {
                            const auto& mu = ch.grantItem.onAcquire[mi];
                            ctxGrantMutStmt.bind(a.id).bind(co).bind((int)k).bind(0).bind((int)mi)
                                           .bind(mu.varName).bind(mu.op).bind(mu.value);
                            ctxGrantMutStmt.run();
                        }
                        for (size_t mi = 0; mi < ch.grantItem.onUnacquire.size(); ++mi) {
                            const auto& mu = ch.grantItem.onUnacquire[mi];
                            ctxGrantMutStmt.bind(a.id).bind(co).bind((int)k).bind(1).bind((int)mi)
                                           .bind(mu.varName).bind(mu.op).bind(mu.value);
                            ctxGrantMutStmt.run();
                        }
                    }
                    for (size_t k = 0; k < ctx.altTexts.size(); ++k) {
                        ctxAltStmt.bind(a.id).bind(co).bind((int)k)
                                  .bind(ctx.altTexts[k].requiredFlag).bind(ctx.altTexts[k].text);
                        ctxAltStmt.run();
                    }
                    // Authored Context characters (v17): the full character build + a foe flag,
                    // with the same child-table shape as a .gnschar (trainings/spells/items).
                    for (size_t k = 0; k < ctx.characters.size(); ++k) {
                        const Character& pc = ctx.characters[k].character;
                        int chOrd = (int)k;
                        ctxCharStmt.bind(a.id).bind(co).bind(chOrd)
                                   .bind(ctx.characters[k].foe ? 1 : 0)
                                   .bind(pc.name).bind(pc.playerName).bind(pc.kin).bind(pc.calling).bind(pc.level)
                                   .bind(pc.traits.might).bind(pc.traits.grace).bind(pc.traits.wits).bind(pc.traits.spirit)
                                   .bind(pc.maxLife).bind(pc.life).bind(pc.defense).bind(pc.ap).bind(pc.strain)
                                   .bind(pc.armorName).bind(pc.shield ? 1 : 0)
                                   .bind(pc.weaponName).bind(pc.weaponDamageDie).bind(pc.weaponBonus)
                                   .bind(pc.background).bind(pc.goal).bind(pc.personality).bind(pc.notes)
                                   .bind(pc.portraitPath).bind(pc.gold);
                        ctxCharStmt.run();
                        for (const auto& t : pc.trainings) {
                            ctxCharTrainStmt.bind(a.id).bind(co).bind(chOrd).bind(t);
                            ctxCharTrainStmt.run();
                        }
                        for (const auto& sp : pc.spells) {
                            ctxCharSpellStmt.bind(a.id).bind(co).bind(chOrd).bind(sp);
                            ctxCharSpellStmt.run();
                        }
                        for (const auto& it : pc.inventory) {
                            ctxCharItemStmt.bind(a.id).bind(co).bind(chOrd).bind(it.name).bind(it.description)
                                           .bind(it.imageId).bind(it.imagePath)
                                           .bind(it.quantity < 1 ? 1 : it.quantity).bind(it.value);
                            ctxCharItemStmt.run();
                        }
                    }
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

    // module_variables was added in v15; tolerate older files that lack it.
    try {
        Stmt s(c.db, "SELECT name,type,default_value FROM module_variables ORDER BY ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            ModuleVariable v;
            v.name         = colText(s.s, 0);
            v.type         = static_cast<VarType>(colInt(s.s, 1));
            v.defaultValue = colText(s.s, 2);
            mod.variables.push_back(std::move(v));
        }
    } catch (const DbError&) {
        // no module_variables table — leave empty
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
    static const char* kAreaOptCols[] = {"fill_enabled", "label_auto", "is_shop", "music", "hidden",
                                         "choice_prompt"};
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
            a.choicePrompt      = opt >= 6 ? colText(s.s, 23) : "";
            if (Map* m = mod.mapById(mapId)) m->areas.push_back(std::move(a));
        }
    };
    for (int opt = 6; ; --opt) {
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

    // area_choices was added in v14; tolerate older files that lack it.
    try {
        Stmt s(c.db, "SELECT area_id,label,journal,set_flag,cp_id,gold,grant_item,take_item "
                     "FROM area_choices ORDER BY area_id,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0))) {
                AreaChoice ch;
                ch.label                 = colText(s.s, 1);
                ch.journalEntry          = colText(s.s, 2);
                ch.setFlag               = colText(s.s, 3);
                ch.completeControlPointId = colInt(s.s, 4);
                ch.goldDelta             = colInt(s.s, 5);
                ch.grantItemName         = colText(s.s, 6);
                ch.takeItemName          = colText(s.s, 7);
                a->choices.push_back(std::move(ch));
            }
        }
    } catch (const DbError&) {
        // no area_choices table — leave lists empty
    }

    // area_alt_texts was added in v14; tolerate older files that lack it.
    try {
        Stmt s(c.db, "SELECT area_id,flag,text FROM area_alt_texts ORDER BY area_id,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0)))
                a->altTexts.push_back(AreaConditionalText{colText(s.s, 1), colText(s.s, 2)});
        }
    } catch (const DbError&) {
        // no area_alt_texts table — leave lists empty
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

    // --- Per-area Contexts (v15) -------------------------------------------------------------
    // Load area_contexts (rows in ctx_ord order, so vector index == ctx_ord) and their child
    // tables. All are tolerated as absent so pre-v15 files fall through to the migration below.
    auto ctxAt = [&](int areaId, int ctxOrd) -> AreaContext* {
        Area* a = mod.areaById(areaId);
        if (!a || ctxOrd < 0 || ctxOrd >= (int)a->contexts.size()) return nullptr;
        return &a->contexts[ctxOrd];
    };
    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,name,dm_text,player_text,monster_chance,monster_type,"
                     "treasure_chance,treasure_type,trap_chance,trap_desc,lock_chance,lock_desc,"
                     "hidden_chance,hidden_desc,artwork_path,default_image,music,is_shop,choice_prompt "
                     "FROM area_contexts ORDER BY area_id,ctx_ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0))) {
                AreaContext ctx;
                ctx.name             = colText(s.s, 2);
                ctx.dmText           = colText(s.s, 3);
                ctx.playerText       = colText(s.s, 4);
                ctx.monsterChancePct = colInt(s.s, 5);
                ctx.monsterType      = colText(s.s, 6);
                ctx.treasureChancePct= colInt(s.s, 7);
                ctx.treasureType     = colText(s.s, 8);
                ctx.trapChancePct    = colInt(s.s, 9);
                ctx.trapDescription  = colText(s.s, 10);
                ctx.lockChancePct    = colInt(s.s, 11);
                ctx.lockDescription  = colText(s.s, 12);
                ctx.hiddenChancePct  = colInt(s.s, 13);
                ctx.hiddenDescription= colText(s.s, 14);
                ctx.artworkPath      = colText(s.s, 15);
                ctx.defaultImage     = colInt(s.s, 16);
                ctx.musicPath        = colText(s.s, 17);
                ctx.isShop           = colInt(s.s, 18) != 0;
                ctx.choicePrompt     = colText(s.s, 19);
                a->contexts.push_back(std::move(ctx));
            }
        }
    } catch (const DbError&) { /* no area_contexts — pre-v15; migration synthesizes below */ }

    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,ord,var_name,op,value FROM context_conditions "
                     "ORDER BY area_id,ctx_ord,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1)))
                ctx->conditions.push_back(ContextClause{colText(s.s, 3), colInt(s.s, 4), colText(s.s, 5)});
    } catch (const DbError&) {}

    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,type,count FROM context_monsters;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1)))
                ctx->monsters.push_back(AreaMonster{colText(s.s, 2), colInt(s.s, 3)});
    } catch (const DbError&) {}

    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,type,chance FROM context_treasures;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1)))
                ctx->treasures.push_back(AreaTreasure{colText(s.s, 2), colInt(s.s, 3)});
    } catch (const DbError&) {}

    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,slot,path,direction,is_default FROM context_images "
                     "ORDER BY area_id,ctx_ord,slot;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1))) {
                if (colInt(s.s, 5) != 0) ctx->defaultImage = (int)ctx->images.size();
                ctx->images.push_back(AreaImage{colText(s.s, 3), colInt(s.s, 4)});
            }
    } catch (const DbError&) {}

    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,name,description,cost,stock,image,image_id "
                     "FROM context_shop_items;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1))) {
                ShopItem si;
                si.name = colText(s.s, 2); si.description = colText(s.s, 3);
                si.costGp = colInt(s.s, 4); si.stock = colInt(s.s, 5);
                si.imagePath = colText(s.s, 6); si.imageId = colText(s.s, 7);
                ctx->shopItems.push_back(std::move(si));
            }
    } catch (const DbError&) {}

    // Shop-item acquire/loss mutations (v18): kind 0 = onAcquire, 1 = onUnacquire, keyed by item_ord.
    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,item_ord,kind,var_name,op,value "
                     "FROM context_shop_item_mutations ORDER BY area_id,ctx_ord,item_ord,kind,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1));
            int itemOrd = colInt(s.s, 2);
            if (ctx && itemOrd >= 0 && itemOrd < (int)ctx->shopItems.size()) {
                VarMutation mu{colText(s.s, 4), colInt(s.s, 5), colText(s.s, 6)};
                if (colInt(s.s, 3) == 0) ctx->shopItems[itemOrd].onAcquire.push_back(std::move(mu));
                else                     ctx->shopItems[itemOrd].onUnacquire.push_back(std::move(mu));
            }
        }
    } catch (const DbError&) {}

    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,target_area_id,label FROM context_transitions;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1)))
                ctx->transitions.push_back(AreaTransition{colInt(s.s, 2), colText(s.s, 3)});
    } catch (const DbError&) {}

    // context_choices arrived in v15 (grant_item is a bare name); v16 added the rich granted-item
    // columns (grant_item_desc/image/path/qty) + deactivate_area/delete_context. `level` 1 = v16,
    // 0 = v15; fall back a tier if the newer columns are missing. Either way the legacy grant_item
    // name migrates into grantItem.name so older/newer readers agree.
    // v19 appended the granted-item equip profile (grant_slot/damage_die/defense_bonus/weapon_bonus)
    // and grant_dropable. `level` 2 = v19, 1 = v16, 0 = v15; fall back a tier.
    auto loadChoices = [&](int level) {
        const char* sql =
            level >= 2 ? "SELECT area_id,ctx_ord,ord,label,journal,set_flag,cp_id,gold,grant_item,take_item,"
                         "grant_item_desc,grant_item_image,grant_item_path,grant_item_qty,"
                         "deactivate_area,delete_context,"
                         "grant_slot,grant_damage_die,grant_defense_bonus,grant_weapon_bonus,grant_dropable "
                         "FROM context_choices ORDER BY area_id,ctx_ord,ord;"
          : level >= 1 ? "SELECT area_id,ctx_ord,ord,label,journal,set_flag,cp_id,gold,grant_item,take_item,"
                         "grant_item_desc,grant_item_image,grant_item_path,grant_item_qty,"
                         "deactivate_area,delete_context "
                         "FROM context_choices ORDER BY area_id,ctx_ord,ord;"
                       : "SELECT area_id,ctx_ord,ord,label,journal,set_flag,cp_id,gold,grant_item,take_item "
                         "FROM context_choices ORDER BY area_id,ctx_ord,ord;";
        Stmt s(c.db, sql);
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1))) {
                AreaChoice ch;
                ch.label = colText(s.s, 3); ch.journalEntry = colText(s.s, 4);
                ch.setFlag = colText(s.s, 5); ch.completeControlPointId = colInt(s.s, 6);
                ch.goldDelta = colInt(s.s, 7);
                ch.grantItem.name = colText(s.s, 8);   // migrate legacy grant_item name into grantItem
                ch.takeItemName = colText(s.s, 9);
                if (level >= 1) {
                    ch.grantItem.description = colText(s.s, 10);
                    ch.grantItem.imageId    = colText(s.s, 11);
                    ch.grantItem.imagePath  = colText(s.s, 12);
                    ch.grantItem.quantity   = colInt(s.s, 13);
                    if (ch.grantItem.quantity < 1) ch.grantItem.quantity = 1;
                    ch.deactivateArea = colInt(s.s, 14) != 0;
                    ch.deleteContext  = colInt(s.s, 15) != 0;
                }
                if (level >= 2) {
                    ch.grantItem.slot         = colInt(s.s, 16);
                    ch.grantItem.damageDie    = colText(s.s, 17);
                    ch.grantItem.defenseBonus = colInt(s.s, 18);
                    ch.grantItem.weaponBonus  = colInt(s.s, 19);
                    ch.grantItem.dropable     = colInt(s.s, 20) != 0;
                }
                ctx->choices.push_back(std::move(ch));
            }
    };
    for (int level = 2; ; --level) {
        try { loadChoices(level); break; }
        catch (const DbError&) {
            for (auto& m : mod.maps) for (auto& a : m.areas)
                for (auto& ctx : a.contexts) ctx.choices.clear();
            if (level == 0) break;   // no table at all — leave empty
        }
    }
    // Choice "Set item dropable" effects (v19), keyed by (area,ctx_ord,choice_ord).
    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,choice_ord,item_name,dropable "
                     "FROM context_choice_dropable_sets ORDER BY area_id,ctx_ord,choice_ord,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1));
            int choiceOrd = colInt(s.s, 2);
            if (ctx && choiceOrd >= 0 && choiceOrd < (int)ctx->choices.size())
                ctx->choices[choiceOrd].dropableSets.emplace_back(colText(s.s, 3), colInt(s.s, 4) != 0);
        }
    } catch (const DbError&) {}

    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,choice_ord,ord,var_name,op,value "
                     "FROM context_choice_mutations ORDER BY area_id,ctx_ord,choice_ord,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1));
            int choiceOrd = colInt(s.s, 2);
            if (ctx && choiceOrd >= 0 && choiceOrd < (int)ctx->choices.size())
                ctx->choices[choiceOrd].mutations.push_back(
                    VarMutation{colText(s.s, 4), colInt(s.s, 5), colText(s.s, 6)});
        }
    } catch (const DbError&) {}

    // Granted-item acquire/loss mutations (v18): kind 0 = onAcquire, 1 = onUnacquire.
    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,choice_ord,kind,var_name,op,value "
                     "FROM context_choice_grant_mutations ORDER BY area_id,ctx_ord,choice_ord,kind,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1));
            int choiceOrd = colInt(s.s, 2);
            if (ctx && choiceOrd >= 0 && choiceOrd < (int)ctx->choices.size()) {
                VarMutation mu{colText(s.s, 4), colInt(s.s, 5), colText(s.s, 6)};
                if (colInt(s.s, 3) == 0) ctx->choices[choiceOrd].grantItem.onAcquire.push_back(std::move(mu));
                else                     ctx->choices[choiceOrd].grantItem.onUnacquire.push_back(std::move(mu));
            }
        }
    } catch (const DbError&) {}

    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,flag,text FROM context_alt_texts "
                     "ORDER BY area_id,ctx_ord,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1)))
                ctx->altTexts.push_back(AreaConditionalText{colText(s.s, 2), colText(s.s, 3)});
    } catch (const DbError&) {}

    // context_characters (v17): authored NPCs on a context. Tolerated as absent on older modules.
    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,ord,foe,name,player_name,kin,calling,level,"
                     "might,grace,wits,spirit,max_life,life,defense,ap,strain,armor_name,shield,"
                     "weapon_name,weapon_damage_die,weapon_bonus,background,goal,personality,notes,portrait,gold "
                     "FROM context_characters ORDER BY area_id,ctx_ord,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (AreaContext* ctx = ctxAt(colInt(s.s, 0), colInt(s.s, 1))) {
                AreaCharacter ac;
                ac.foe = colInt(s.s, 3) != 0;
                Character& pc = ac.character;
                pc.name = colText(s.s, 4); pc.playerName = colText(s.s, 5); pc.kin = colText(s.s, 6);
                pc.calling = colText(s.s, 7); pc.level = colInt(s.s, 8);
                pc.traits.might = colInt(s.s, 9); pc.traits.grace = colInt(s.s, 10);
                pc.traits.wits = colInt(s.s, 11); pc.traits.spirit = colInt(s.s, 12);
                pc.maxLife = colInt(s.s, 13); pc.life = colInt(s.s, 14); pc.defense = colInt(s.s, 15);
                pc.ap = colInt(s.s, 16); pc.strain = colInt(s.s, 17);
                pc.armorName = colText(s.s, 18); pc.shield = colInt(s.s, 19) != 0;
                pc.weaponName = colText(s.s, 20); pc.weaponDamageDie = colText(s.s, 21);
                pc.weaponBonus = colInt(s.s, 22);
                pc.background = colText(s.s, 23); pc.goal = colText(s.s, 24);
                pc.personality = colText(s.s, 25); pc.notes = colText(s.s, 26);
                pc.portraitPath = colText(s.s, 27); pc.gold = colInt(s.s, 28);
                ctx->characters.push_back(std::move(ac));
            }
    } catch (const DbError&) {}

    auto ctxCharAt = [&](int areaId, int ctxOrd, int charOrd) -> Character* {
        AreaContext* ctx = ctxAt(areaId, ctxOrd);
        if (!ctx || charOrd < 0 || charOrd >= (int)ctx->characters.size()) return nullptr;
        return &ctx->characters[charOrd].character;
    };
    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,char_ord,name FROM context_character_training ORDER BY rowid;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (Character* pc = ctxCharAt(colInt(s.s, 0), colInt(s.s, 1), colInt(s.s, 2)))
                pc->trainings.push_back(colText(s.s, 3));
    } catch (const DbError&) {}
    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,char_ord,name FROM context_character_spell ORDER BY rowid;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (Character* pc = ctxCharAt(colInt(s.s, 0), colInt(s.s, 1), colInt(s.s, 2)))
                pc->spells.push_back(colText(s.s, 3));
    } catch (const DbError&) {}
    try {
        Stmt s(c.db, "SELECT area_id,ctx_ord,char_ord,name,description,image_id,image_path,quantity,value "
                     "FROM context_character_item ORDER BY rowid;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (Character* pc = ctxCharAt(colInt(s.s, 0), colInt(s.s, 1), colInt(s.s, 2))) {
                InventoryItem it;
                it.name = colText(s.s, 3); it.description = colText(s.s, 4);
                it.imageId = colText(s.s, 5); it.imagePath = colText(s.s, 6);
                it.quantity = colInt(s.s, 7); if (it.quantity < 1) it.quantity = 1;
                it.value = colInt(s.s, 8);
                pc->inventory.push_back(std::move(it));
            }
    } catch (const DbError&) {}

    // Migration: any area with no contexts (pre-v15 file) gets one "default" context (empty
    // condition = always active) populated from its legacy fields. Then clear the legacy content
    // fields so a re-save writes contexts only.
    for (auto& m : mod.maps)
        for (auto& a : m.areas) {
            if (a.contexts.empty()) {
                AreaContext ctx;
                ctx.name             = "default";
                ctx.dmText           = a.dmText;
                ctx.playerText       = a.playerText;
                ctx.monsterChancePct = a.monsterChancePct;
                ctx.monsterType      = a.monsterType;
                ctx.monsters         = a.monsters;
                ctx.treasureChancePct= a.treasureChancePct;
                ctx.treasureType     = a.treasureType;
                ctx.treasures        = a.treasures;
                ctx.trapChancePct    = a.trapChancePct;
                ctx.trapDescription  = a.trapDescription;
                ctx.lockChancePct    = a.lockChancePct;
                ctx.lockDescription  = a.lockDescription;
                ctx.hiddenChancePct  = a.hiddenChancePct;
                ctx.hiddenDescription= a.hiddenDescription;
                ctx.artworkPath      = a.artworkPath;
                ctx.images           = a.images;
                ctx.defaultImage     = a.defaultImage;
                ctx.musicPath        = a.musicPath;
                ctx.isShop           = a.isShop;
                ctx.shopItems        = a.shopItems;
                ctx.transitions      = a.transitions;
                ctx.choicePrompt     = a.choicePrompt;
                ctx.choices          = a.choices;
                ctx.altTexts         = a.altTexts;
                a.contexts.push_back(std::move(ctx));
            }
            // Legacy content fields now live in contexts; clear so they aren't re-persisted.
            a.dmText.clear(); a.playerText.clear();
            a.monsterChancePct = 0; a.monsterType.clear(); a.monsters.clear();
            a.treasureChancePct = 0; a.treasureType.clear(); a.treasures.clear();
            a.trapChancePct = 0; a.trapDescription.clear();
            a.lockChancePct = 0; a.lockDescription.clear();
            a.hiddenChancePct = 0; a.hiddenDescription.clear();
            a.artworkPath.clear(); a.images.clear(); a.defaultImage = 0; a.musicPath.clear();
            a.isShop = false; a.shopItems.clear(); a.transitions.clear();
            a.choicePrompt.clear(); a.choices.clear(); a.altTexts.clear();
        }

    {
        Stmt s(c.db, "SELECT area_id,control_point_id FROM area_prerequisites;");
        while (sqlite3_step(s.s) == SQLITE_ROW) {
            if (Area* a = mod.areaById(colInt(s.s, 0)))
                a->prerequisiteControlPointIds.push_back(colInt(s.s, 1));
        }
    }

    // Area OnEnter/OnExit mutations (v18). Tolerated as absent on older modules.
    try {
        Stmt s(c.db, "SELECT area_id,var_name,op,value FROM area_enter_mutations ORDER BY area_id,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (Area* a = mod.areaById(colInt(s.s, 0)))
                a->onEnter.push_back(VarMutation{colText(s.s, 1), colInt(s.s, 2), colText(s.s, 3)});
    } catch (const DbError&) {}
    try {
        Stmt s(c.db, "SELECT area_id,var_name,op,value FROM area_exit_mutations ORDER BY area_id,ord;");
        while (sqlite3_step(s.s) == SQLITE_ROW)
            if (Area* a = mod.areaById(colInt(s.s, 0)))
                a->onExit.push_back(VarMutation{colText(s.s, 1), colInt(s.s, 2), colText(s.s, 3)});
    } catch (const DbError&) {}

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

    // Normalize legacy granted items: pre-v16 choices (incl. those migrated from v14 area_choices)
    // carry a bare grantItemName. Fold it into grantItem.name and clear the legacy field so the
    // rest of the code path reads a single source of truth.
    for (auto& m : mod.maps)
        for (auto& a : m.areas)
            for (auto& ctx : a.contexts)
                for (auto& ch : ctx.choices)
                    if (ch.grantItem.name.empty() && !ch.grantItemName.empty()) {
                        ch.grantItem.name = ch.grantItemName;
                        ch.grantItemName.clear();
                    }

    return mod;
}

} // namespace gns
