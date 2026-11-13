#pragma once

#include <string>

#include "format/party/PartyTxt.h"

namespace geck {

/// Parse Fallout 2's `data/party.txt`. Like the engine, members are read from `[Party Member 0]` up to
/// the first index with no section or no `party_member_pid`, and the level fields are read only when
/// `level_up_every` is present. Option values are lowercased, as the engine matches them. Never throws.
PartyTxt parsePartyTxt(const std::string& text);

} // namespace geck
