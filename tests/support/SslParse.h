#pragma once

#include <string>

#include "ssl/ScriptUnit.h"
#include "ssl/Source.h"

namespace geck::test {

/// Parses `text` as a lone in-memory "script.ssl".
inline ssl::ScriptUnit parseSsl(const std::string& text) {
    ssl::MemorySourceProvider files;
    files.add("script.ssl", text);
    return ssl::ScriptUnit::load(files, "script.ssl");
}

} // namespace geck::test
