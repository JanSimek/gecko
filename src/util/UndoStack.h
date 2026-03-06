#pragma once

#include <functional>
#include <vector>
#include <string>

namespace geck {

struct UndoCommand {
    std::string description;
    std::function<void()> undo;
    std::function<void()> redo;
};

class UndoStack {
public:
    explicit UndoStack(size_t maxCommands = 100)
        : _maxCommands(maxCommands) { }

    bool canUndo() const { return !_undo.empty(); }
    bool canRedo() const { return !_redo.empty(); }
    std::string lastUndoLabel() const { return _undo.empty() ? std::string() : _undo.back().description; }
    std::string lastRedoLabel() const { return _redo.empty() ? std::string() : _redo.back().description; }

    void push(UndoCommand cmd) {
        if (!cmd.undo || !cmd.redo) {
            return;
        }
        _undo.push_back(std::move(cmd));
        _redo.clear();
        if (_undo.size() > _maxCommands) {
            _undo.erase(_undo.begin());
        }
    }

    bool undo() {
        if (_undo.empty())
            return false;
        auto cmd = std::move(_undo.back());
        _undo.pop_back();
        cmd.undo();
        _redo.push_back(std::move(cmd));
        return true;
    }

    bool redo() {
        if (_redo.empty())
            return false;
        auto cmd = std::move(_redo.back());
        _redo.pop_back();
        cmd.redo();
        _undo.push_back(std::move(cmd));
        return true;
    }

    void clear() {
        _undo.clear();
        _redo.clear();
    }

    void setMaxCommands(size_t maxCommands) { _maxCommands = maxCommands; }
    size_t maxCommands() const { return _maxCommands; }

private:
    std::vector<UndoCommand> _undo;
    std::vector<UndoCommand> _redo;
    size_t _maxCommands;
};

} // namespace geck
