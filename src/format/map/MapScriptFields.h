#pragma once

#include "format/map/MapScript.h"

namespace geck {

/**
 * @brief Single source of truth for a MapScript's fixed trailer block.
 *
 * After the per-type head (pid, next_script, and the SPATIAL/TIMER-only timer/radius
 * fields), every script record carries this fixed run of 32-bit fields. MapReader and
 * MapWriter both traverse it here so the layout can't drift between read and write.
 * `visit` is called once per field, in order, with a reference to it — mutable when
 * reading, const when writing. All fields are 32-bit, read/written big-endian
 * regardless of signedness. (The head + type-conditional fields stay in the
 * reader/writer because they also carry direction-specific error handling.)
 */
template <class Script, class Visit>
void visitMapScriptTrailerFields(Script& script, Visit&& visit) {
    visit(script.flags);
    visit(script.script_id);
    visit(script.unknown5);
    visit(script.script_oid);
    visit(script.local_var_offset);
    visit(script.local_var_count);
    visit(script.unknown9);
    visit(script.unknown10);
    visit(script.unknown11);
    visit(script.unknown12);
    visit(script.unknown13);
    visit(script.unknown14);
    visit(script.unknown15);
    visit(script.unknown16);
}

} // namespace geck
