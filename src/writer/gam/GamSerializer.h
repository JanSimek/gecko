#pragma once

#include "format/gam/Gam.h"

#include <string>

namespace geck::writer {

/// Serialise a @ref Gam back to bytes. Every line is emitted from its raw content joined with `\n` (plus
/// a trailing `\n` iff `finalNewline`), so an unedited document is reproduced exactly (modulo CRLF->LF).
/// The engine writes LF.
std::string serializeGam(const Gam& doc);

} // namespace geck::writer
