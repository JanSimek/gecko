#include "ui/tools/ToolRegistry.h"

#include <algorithm>

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

bool ToolRegistry::unregisterTool(std::string_view id) {
    if (_activeToolId == id) {
        clearActiveTool();
    }
    return _tools.erase(std::string(id)) > 0;
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

std::vector<std::string> ToolRegistry::toolIds() const {
    std::vector<std::string> ids;
    ids.reserve(_tools.size());
    for (const auto& [id, tool] : _tools) {
        (void)tool;
        ids.push_back(id);
    }
    std::ranges::sort(ids);
    return ids;
}

} // namespace geck
