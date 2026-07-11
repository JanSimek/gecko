#pragma once

#include "ui/tools/ITool.h"

#include <cstddef>
#include <memory>
#include <functional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace geck {

/// Owns the registered tools and tracks which one is active.
///
/// Lifecycle notes for tool authors: setActiveTool() on the already-active id is an
/// idempotent no-op (onActivate does NOT re-fire), and unregisterTool() destroys the tool
/// immediately — it must never be called from inside one of that tool's own event
/// handlers (the object would be deleted mid-callback).
class ToolRegistry {
public:
    /// Takes ownership. Returns false (and destroys the tool) on a null tool, an empty
    /// id, or a duplicate id.
    bool registerTool(std::unique_ptr<ITool> tool);
    bool unregisterTool(std::string_view id);

    ITool* tool(std::string_view id);
    const ITool* tool(std::string_view id) const;

    bool setActiveTool(std::string_view id);
    void clearActiveTool();

    ITool* activeTool();
    const ITool* activeTool() const;
    std::string activeToolId() const { return _activeToolId; }
    std::vector<std::string> toolIds() const;

private:
    struct TransparentStringHash {
        using is_transparent = void;

        std::size_t operator()(std::string_view value) const noexcept {
            return std::hash<std::string_view>{}(value);
        }
    };

    std::unordered_map<std::string, std::unique_ptr<ITool>, TransparentStringHash, std::equal_to<>> _tools;
    std::string _activeToolId;
};

} // namespace geck
