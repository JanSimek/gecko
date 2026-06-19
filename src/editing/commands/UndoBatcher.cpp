#include "UndoBatcher.h"

#include <memory>

namespace geck {

UndoBatcher::UndoBatcher(UndoStack& undoStack, std::function<void()> onStackChanged)
    : _undoStack(undoStack)
    , _onStackChanged(std::move(onStackChanged)) {
}

bool UndoBatcher::push(UndoCommand cmd) {
    if (!cmd.undo || !cmd.redo) {
        return false;
    }
    if (_batchDepth > 0) {
        // Buffer for collapse in endBatch(); the edit itself was already applied by
        // the register*() caller, so we do not run redo here.
        _batchedCommands.push_back(std::move(cmd));
        return true;
    }
    _undoStack.push(std::move(cmd));
    if (_onStackChanged) {
        _onStackChanged();
    }
    return true;
}

void UndoBatcher::beginBatch(const std::string& description) {
    if (_batchDepth == 0) {
        _batchDescription = description;
        _batchedCommands.clear();
    }
    ++_batchDepth;
}

bool UndoBatcher::endBatch() {
    if (_batchDepth == 0) {
        return false; // Unbalanced endBatch(); nothing to close.
    }

    --_batchDepth;
    if (_batchDepth > 0) {
        return false; // Inner nested close; defer the flush to the outermost endBatch().
    }

    if (_batchedCommands.empty()) {
        _batchDescription.clear();
        return false; // Nothing was recorded during the batch.
    }

    // Collapse the buffered commands into one entry: redo replays forward, undo
    // reverts in reverse. Both closures share one command list via shared_ptr so the
    // batch is stored once and survives repeated undo/redo cycles.
    auto commands = std::make_shared<std::vector<UndoCommand>>(std::move(_batchedCommands));
    _batchedCommands.clear(); // restore to a known-empty state after the move

    UndoCommand combined;
    combined.description = std::move(_batchDescription);
    _batchDescription.clear();
    combined.redo = [commands]() {
        for (const auto& cmd : *commands) {
            cmd.redo();
        }
    };
    combined.undo = [commands]() {
        for (auto it = commands->rbegin(); it != commands->rend(); ++it) {
            it->undo();
        }
    };

    // _batchDepth is now 0, so this pushes (rather than buffers) through the single
    // funnel that owns the undo stack and its stack-changed notification.
    return push(std::move(combined));
}

} // namespace geck
