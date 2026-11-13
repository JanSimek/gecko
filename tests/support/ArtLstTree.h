#pragma once

#include <string>

#include "support/DataTree.h"

namespace geck::test {

/// A mounted DataTree holding one minimal LST per art type, the lists FrmResolver and the frm tools
/// read the same way they read the shipped archives. No FRM bytes are written.
///
/// misc.lst keeps the cases off index 1, which FrmResolver::resolve treats as the scroll blocker:
/// index 0 is 'thing', 1 a filler, then the exit-grid family (ext2grd1 at index 2) so glob, list and
/// resolve have a real group.
class ArtLstTree : public DataTree {
public:
    explicit ArtLstTree(const std::string& name)
        : DataTree(name) {
        write("art/items/items.lst", "knife.frm\nrock.frm\n");
        write("art/critters/critters.lst", "hmwarr,Tribal Warrior\nnmwarr,Raider\n");
        write("art/scenery/scenery.lst", "tree1.frm\ntree2.frm\n");
        write("art/walls/walls.lst", "wall1.frm\nwall2.frm\nwall3.frm\n");
        write("art/tiles/tiles.lst", "floor.frm\ngrass.frm\n");
        write("art/misc/misc.lst", "thing.frm\nscrblk.frm\next2grd1.frm\next2grd2.frm\nexitgrd1.frm\n");
        mount();
    }
};

} // namespace geck::test
