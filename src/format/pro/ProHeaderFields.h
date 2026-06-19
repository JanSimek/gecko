#pragma once

#include "format/pro/Pro.h"

namespace geck {

/**
 * @brief Single source of truth for the PRO file's fixed common header.
 *
 * Every proto begins with these six 32-bit fields. ProReader and ProWriter both
 * traverse them through this one function so the header layout can't drift between
 * read and write. `visit` is called once per field, in order, with a reference to
 * it — mutable when reading, const when writing. All fields are 32-bit, so each is
 * read/written as a big-endian 32-bit word regardless of signedness.
 */
template <class ProT, class Visit>
void visitProHeaderFields(ProT& pro, Visit&& visit) {
    visit(pro.header.PID);
    visit(pro.header.message_id);
    visit(pro.header.FID);
    visit(pro.header.light_distance);
    visit(pro.header.light_intensity);
    visit(pro.header.flags);
}

} // namespace geck
