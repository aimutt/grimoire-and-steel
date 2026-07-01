#pragma once
#include <functional>
#include <string>

struct sqlite3;

namespace gns {

// Build a standalone SQLite file (.gnsmod / .gnschar) *atomically* and *non-destructively*.
//
// The database is written to a sibling temporary file inside a single transaction, then the
// temporary file replaces `path` in one atomic step (preserving the previous version as
// `<path>.bak`). If `build` throws — or the process is interrupted before the replace — the
// temporary file is discarded and the existing `path` is left completely untouched.
//
// This is the guardrail against the old in-place save bug: saveModule/saveCharacter used to
// DROP the existing tables before writing, so a save that failed part-way destroyed the only
// copy of the file. Routing every write through here makes that failure mode impossible.
//
// `build` receives an open, writable handle with a transaction already begun; it should
// CREATE the schema, set `PRAGMA user_version`, and INSERT the rows. It must NOT issue
// BEGIN/COMMIT/ROLLBACK itself (the helper owns the transaction). Throws DbError on failure.
void writeDatabaseAtomically(const std::string& path,
                             const std::function<void(sqlite3*)>& build);

} // namespace gns
