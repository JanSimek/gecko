#include "ssl/CallRefs.h"

#include <algorithm>
#include <cctype>

namespace geck::ssl {

namespace {

    std::string lower(std::string_view text) {
        std::string out(text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    bool isSelf(const Expr* expr) {
        const auto* name = expr != nullptr ? std::get_if<Name>(&expr->node) : nullptr;
        return name != nullptr && lower(name->name) == "self_obj";
    }

} // namespace

std::vector<CallRef> callRefs(const ScriptUnit& unit, const CallGraph& graph, std::initializer_list<std::string_view> opcodes) {
    std::vector<CallRef> found;
    for (const Procedure& procedure : unit.program().procedures) {
        if (!procedure.body.has_value() || !graph.isLive(procedure.name)) {
            continue;
        }
        graph.forEachLiveExpression(procedure, [&unit, &graph, &procedure, &found, &opcodes](const Expr& expr) {
            const auto* call = std::get_if<Call>(&expr.node);
            if (call == nullptr) {
                return;
            }
            const std::string name = lower(call->callee);
            if (std::ranges::find(opcodes, std::string_view(name)) == opcodes.end()) {
                return;
            }
            CallRef ref;
            ref.opcode = name;
            for (const ExprPtr& arg : call->args) {
                ref.args.push_back(arg ? graph.folder().fold(*arg) : std::nullopt);
                ref.self.push_back(isSelf(arg.get()));
            }
            ref.procedure = procedure.name;
            ref.location = unit.writtenAt(expr.range);
            found.push_back(std::move(ref));
        });
    }
    return found;
}

} // namespace geck::ssl
