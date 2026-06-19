#pragma once

#include <functional>
#include <string>
#include <vector>

#include "util/UndoStack.h"

namespace geck {

/**
 * @brief Funnels undoable commands onto an UndoStack, with batching.
 *
 * This is the single owner of "a command was recorded": every editing service
 * pushes through push(), and the stack-changed notification fires here. While a
 * batch is open (beginBatch/endBatch), commands are buffered and collapsed into
 * one UndoStack entry so a multi-edit operation (area paste, prefab stamp,
 * procedural fill) lands as a single undo step instead of one-per-hex. Batches
 * nest: only the outermost endBatch() flushes.
 *
 * The individual edits are applied immediately by their callers; the batcher only
 * governs how they are recorded for undo/redo.
 */
class UndoBatcher {
public:
    UndoBatcher(UndoStack& undoStack, std::function<void()> onStackChanged);

    /// Pushes a command (or buffers it while batching). Returns false for a
    /// command missing its undo/redo closures.
    bool push(UndoCommand cmd);

    void beginBatch(const std::string& description);

    /// Closes the matching beginBatch(). On the outermost close, buffered commands
    /// collapse into one pushed UndoCommand. Returns true iff a command was pushed.
    bool endBatch();

    [[nodiscard]] bool isBatching() const { return _batchDepth > 0; }

private:
    UndoStack& _undoStack;
    std::function<void()> _onStackChanged;

    int _batchDepth = 0;
    std::string _batchDescription;
    std::vector<UndoCommand> _batchedCommands;
};

} // namespace geck
