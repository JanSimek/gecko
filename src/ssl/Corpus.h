#pragma once

#include <filesystem>
#include <functional>
#include <vector>

#include "ssl/ScriptUnit.h"

namespace geck::ssl {

using ScriptVisitor = std::function<void(std::size_t index, const ScriptUnit& unit)>;

/// Load every script in `paths` through `provider`, spread over the machine's cores, and call
/// `visit` with each script's position in `paths`. Calls come from worker threads, at most one per
/// index, so a visitor that only writes slot `index` of a vector sized beforehand needs no lock.
/// `provider` must be safe to read from several threads. An exception from a load or a visit is
/// rethrown here once every worker has stopped.
void loadScripts(const SourceProvider& provider, const std::vector<std::filesystem::path>& paths, const ScriptVisitor& visit,
    const PreprocessOptions& options = {});

} // namespace geck::ssl
