#include "gns/AtomicDb.h"
#include "gns/Database.h"   // gns::DbError
#include "sqlite3.h"

#include <cstdio>

#ifdef _WIN32
#include <windows.h>
#endif

namespace gns {

namespace {

void execOrThrow(sqlite3* db, const char* sql) {
    char* err = nullptr;
    if (sqlite3_exec(db, sql, nullptr, nullptr, &err) != SQLITE_OK) {
        std::string msg = err ? err : "exec failed";
        sqlite3_free(err);
        throw DbError(std::string("exec failed: ") + msg);
    }
}

bool fileExists(const std::string& path) {
#ifdef _WIN32
    return GetFileAttributesA(path.c_str()) != INVALID_FILE_ATTRIBUTES;
#else
    if (FILE* f = std::fopen(path.c_str(), "rb")) { std::fclose(f); return true; }
    return false;
#endif
}

// Move `tmp` onto `path`, replacing it atomically and keeping the prior file as `<path>.bak`.
// On a first-time save (no existing `path`) it is a plain move with no backup.
void replaceFile(const std::string& tmp, const std::string& path) {
    const std::string bak = path + ".bak";
#ifdef _WIN32
    if (fileExists(path)) {
        // ReplaceFile swaps tmp in for path atomically, preserving path's attributes/ACLs
        // and saving the old contents to bak. Requires path to already exist.
        if (!ReplaceFileA(path.c_str(), tmp.c_str(), bak.c_str(),
                          REPLACEFILE_WRITE_THROUGH | REPLACEFILE_IGNORE_MERGE_ERRORS,
                          nullptr, nullptr)) {
            std::remove(tmp.c_str());
            throw DbError("could not replace '" + path + "' (Win32 error " +
                          std::to_string(GetLastError()) + ")");
        }
    } else if (!MoveFileExA(tmp.c_str(), path.c_str(), MOVEFILE_WRITE_THROUGH)) {
        std::remove(tmp.c_str());
        throw DbError("could not write '" + path + "' (Win32 error " +
                      std::to_string(GetLastError()) + ")");
    }
#else
    if (fileExists(path)) {
        std::remove(bak.c_str());
        std::rename(path.c_str(), bak.c_str());   // best-effort backup
    }
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        throw DbError("could not write '" + path + "'");
    }
#endif
}

} // namespace

void writeDatabaseAtomically(const std::string& path,
                             const std::function<void(sqlite3*)>& build) {
    const std::string tmp = path + ".tmp";
    std::remove(tmp.c_str());          // clear any stale temp from a previous crash

    sqlite3* db = nullptr;
    if (sqlite3_open_v2(tmp.c_str(), &db,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK) {
        std::string msg = db ? sqlite3_errmsg(db) : "no connection";
        sqlite3_close(db);
        std::remove(tmp.c_str());
        throw DbError("cannot create '" + tmp + "': " + msg);
    }

    try {
        execOrThrow(db, "BEGIN IMMEDIATE;");
        build(db);                     // CREATE schema + PRAGMA user_version + INSERTs
        execOrThrow(db, "COMMIT;");
    } catch (...) {
        sqlite3_close(db);             // rolls back the open transaction
        std::remove(tmp.c_str());      // discard the partial temp; original is untouched
        throw;
    }

    // Fully flush and close before touching the filesystem: Windows cannot replace a file
    // that still has an open handle, and we want the bytes on disk before the swap.
    if (sqlite3_close(db) != SQLITE_OK) {
        std::remove(tmp.c_str());
        throw DbError("failed to finalize '" + tmp + "'");
    }

    replaceFile(tmp, path);
}

} // namespace gns
