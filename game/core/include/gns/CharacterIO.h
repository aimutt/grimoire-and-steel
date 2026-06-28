#pragma once
#include "gns/Character.h"
#include <string>

// .gnschar is a standalone SQLite file holding one player character, distinct from
// the read-only gns.db reference database and from authored .gnsmod modules. A
// character persists across modules because its AP is cumulative.
//
// Like ModuleIO, the on-disk PRAGMA user_version is kCharacterFormatVersion and the
// loader is tolerant: append new columns only (never reorder/remove) and bump the
// version + this header comment + a gns_tests round-trip check when fields are added.

namespace gns {

constexpr int kCharacterFormatVersion = 1;

// Write `c` to `path`, fully overwriting any existing file. Throws gns::DbError.
void saveCharacter(const Character& c, const std::string& path);

// Read a character from `path`. Throws gns::DbError if it cannot be read.
Character loadCharacter(const std::string& path);

} // namespace gns
