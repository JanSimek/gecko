#pragma once

#include "format/map/MapObject.h"

namespace geck {

/**
 * @brief Single source of truth for a MapObject's common-field block.
 *
 * Every object record begins with this fixed sequence of 32-bit fields, before
 * its type-specific tail. MapReader and MapWriter both traverse it through this
 * one function, so the on-disk layout cannot drift between read and write: add or
 * reorder a common field here and both directions stay in sync.
 *
 * `visit` is invoked once per field, in order, with a reference to that field —
 * mutable when reading (Obj = MapObject), const when writing (Obj = const
 * MapObject). All fields are 32-bit, so a caller reads/writes each as a big-endian
 * 32-bit word regardless of its signedness.
 */
template <class Obj, class Visit>
void visitMapObjectCommonFields(Obj& object, Visit&& visit) {
    visit(object.unknown0);
    visit(object.position);
    visit(object.x);
    visit(object.y);
    visit(object.sx);
    visit(object.sy);
    visit(object.frame_number);
    visit(object.direction);
    visit(object.frm_pid);
    visit(object.flags);
    visit(object.elevation);
    visit(object.pro_pid);
    visit(object.critter_index);
    visit(object.light_radius);
    visit(object.light_intensity);
    visit(object.outline_color);
    visit(object.map_scripts_pid);
    visit(object.script_id);
    visit(object.objects_in_inventory);
    visit(object.max_inventory_size);
    visit(object.unknown10);
    visit(object.unknown11);
}

} // namespace geck
