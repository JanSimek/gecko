#pragma once

#include <istream>
#include <string>

#include "format/ai/AiPacket.h"

namespace geck {

/// Parse Fallout 2's `data/ai.txt` — an INI file with one `[Section]` per AI packet (the section
/// header is the packet name) and `key=value` lines; `;` begins a full-line comment. Only the
/// behaviour-relevant keys are read; a section without a `packet_num` is skipped. Never throws —
/// returns whatever parsed (an empty AiTxt if the input is empty/garbage), so a missing file just
/// means AI is reported as the raw packet number.
AiTxt parseAiTxt(std::istream& in);
AiTxt parseAiTxt(const std::string& text);

} // namespace geck
