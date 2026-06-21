#pragma once

#include <istream>
#include <string>

#include "format/quests/QuestsTxt.h"

namespace geck {

/// Parse Fallout 2's `data/quests.txt` — one quest per line, five integers separated by any of
/// space/tab/comma, `#` starting a comment (matching the engine's `questInit` tokenizer). Lines with
/// fewer than five integers are skipped. Never throws — returns whatever parsed.
QuestsTxt parseQuestsTxt(std::istream& in);
QuestsTxt parseQuestsTxt(const std::string& text);

} // namespace geck
