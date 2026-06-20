#include "gns/Database.h"
#include "sqlite3.h"

namespace gns {

Database::Database(const std::string& path, bool readOnly) {
    int flags = readOnly ? SQLITE_OPEN_READONLY
                         : (SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE);
    int rc = sqlite3_open_v2(path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "out of memory";
        sqlite3_close(db_);
        db_ = nullptr;
        throw DbError("cannot open '" + path + "': " + msg);
    }
    sqlite3_exec(db_, "PRAGMA foreign_keys=ON;", nullptr, nullptr, nullptr);
}

Database::~Database() {
    if (db_) sqlite3_close(db_);
}

std::vector<Row> Database::query(const std::string& sql) {
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        throw DbError(std::string("prepare failed: ") + sqlite3_errmsg(db_));
    }
    std::vector<Row> rows;
    int cols = sqlite3_column_count(stmt);
    int rc;
    while ((rc = sqlite3_step(stmt)) == SQLITE_ROW) {
        Row row;
        row.reserve(cols);
        for (int i = 0; i < cols; ++i) {
            const unsigned char* txt = sqlite3_column_text(stmt, i);
            row.emplace_back(txt ? reinterpret_cast<const char*>(txt) : "");
        }
        rows.push_back(std::move(row));
    }
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw DbError(std::string("step failed: ") + sqlite3_errmsg(db_));
    }
    return rows;
}

std::string Database::scalar(const std::string& sql) {
    auto rows = query(sql);
    if (rows.empty() || rows[0].empty()) return "";
    return rows[0][0];
}

long long Database::scalarInt(const std::string& sql) {
    std::string s = scalar(sql);
    return s.empty() ? 0 : std::stoll(s);
}

} // namespace gns
