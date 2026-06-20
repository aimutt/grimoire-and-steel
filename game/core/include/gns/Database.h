#pragma once
#include <string>
#include <vector>
#include <stdexcept>

struct sqlite3;

namespace gns {

using Row = std::vector<std::string>;   // one result row, columns as strings (NULL -> "")

// Thrown on any SQLite failure.
class DbError : public std::runtime_error {
public:
    explicit DbError(const std::string& what) : std::runtime_error(what) {}
};

// Minimal RAII wrapper over a SQLite connection. Read-only by default
// (gns.db is never modified by the engine).
class Database {
public:
    explicit Database(const std::string& path, bool readOnly = true);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Run a SELECT and return all rows (each column rendered as text).
    std::vector<Row> query(const std::string& sql);

    // First column of the first row, or "" if no rows.
    std::string scalar(const std::string& sql);

    // First column of the first row as an integer, or 0 if no rows.
    long long scalarInt(const std::string& sql);

    sqlite3* handle() const { return db_; }

private:
    sqlite3* db_ = nullptr;
};

} // namespace gns
