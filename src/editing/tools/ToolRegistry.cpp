#include "editing/tools/ToolRegistry.h"

namespace geck {

bool ToolRegistry::registerTool(std::unique_ptr<ITool> tool) {
    if (!tool || tool->id().empty()) {
        return false;
    }
    const std::string id(tool->id());
    if (_tools.contains(id)) {
        return false;
    }
    _tools.try_emplace(id, std::move(tool));
    return true;
}

ITool* ToolRegistry::tool(std::string_view id) {
    const auto it = _tools.find(id);
    return it == _tools.end() ? nullptr : it->second.get();
}

const ITool* ToolRegistry::tool(std::string_view id) const {
    const auto it = _tools.find(id);
    return it == _tools.end() ? nullptr : it->second.get();
}

bool ToolRegistry::setActiveTool(std::string_view id) {
    auto it = _tools.find(id);
    if (it == _tools.end()) {
        return false;
    }
    if (_activeToolId == id) {
        return true;
    }
    if (ITool* current = activeTool()) {
        current->onDeactivate();
    }
    _activeToolId = it->first;
    it->second->onActivate();
    return true;
}

void ToolRegistry::clearActiveTool() {
    if (ITool* current = activeTool()) {
        current->onDeactivate();
    }
    _activeToolId.clear();
}

ITool* ToolRegistry::activeTool() {
    if (_activeToolId.empty()) {
        return nullptr;
    }
    return tool(_activeToolId);
}

const ITool* ToolRegistry::activeTool() const {
    if (_activeToolId.empty()) {
        return nullptr;
    }
    return tool(_activeToolId);
}

} // namespace geck
