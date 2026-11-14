#pragma once

namespace geck {

/// What the map's right-click menu asks the mod tools to do with the one selected object
/// (EditorWidget::modAssetActionRequested, handled by MainWindow through ModToolsRunner).
enum class ModAssetAction {
    MirroredCopy = 0, ///< a left-right mirrored copy of a scenery piece, as a new piece
    ResizedCopy = 1,  ///< a bigger or smaller copy of a scenery piece, as a new piece
    NameAndText = 2,  ///< the name and examine text the game shows for the object's proto
};

} // namespace geck
