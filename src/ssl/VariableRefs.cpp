#include "ssl/VariableRefs.h"

#include "reader/TextParsing.h"

#include <algorithm>
#include <string_view>

namespace geck::ssl {

namespace {

    using text::equalsIgnoreCase;

    // The opcodes that read and write one family of variables: `get(index)` and `set(index, value)`.
    struct Accessors {
        std::string_view get;
        std::string_view set;
    };

    std::vector<VariableRef> refs(const ScriptUnit& unit, const CallGraph& graph, const Accessors& accessors) {
        std::vector<VariableRef> found;
        for (const Procedure& procedure : unit.program().procedures) {
            if (!procedure.body.has_value() || !graph.isLive(procedure.name)) {
                continue;
            }
            graph.forEachLiveExpression(procedure, [&unit, &graph, &accessors, &procedure, &found](const Expr& expr) {
                const auto* call = std::get_if<Call>(&expr.node);
                if (call == nullptr || call->args.empty() || !call->args.front()) {
                    return;
                }
                const bool get = equalsIgnoreCase(call->callee, accessors.get) && call->args.size() == 1;
                const bool set = equalsIgnoreCase(call->callee, accessors.set) && call->args.size() == 2;
                if (!get && !set) {
                    return;
                }
                const Expr& argument = *call->args.front();
                VariableRef ref;
                ref.kind = set ? VariableRef::Kind::Set : VariableRef::Kind::Get;
                if (const auto index = graph.folder().fold(argument); index.has_value()) {
                    ref.index = static_cast<int>(*index);
                }
                ref.name = unit.argumentMacro(expr, argument);
                if (const auto* name = std::get_if<Name>(&argument.node); name != nullptr && ref.name.empty()) {
                    ref.name = name->name; // the macro is not defined: its name is all there is
                }
                ref.procedure = procedure.name;
                ref.location = unit.writtenAt(expr.range);
                for (const Macro* macro : unit.macroChain(expr.range)) {
                    ref.macros.emplace_back(macro->name);
                }
                found.push_back(std::move(ref));
            });
        }
        return found;
    }

} // namespace

std::vector<VariableRef> globalVariableRefs(const ScriptUnit& unit, const CallGraph& graph) {
    return refs(unit, graph, { "global_var", "set_global_var" });
}

std::vector<VariableRef> localVariableRefs(const ScriptUnit& unit, const CallGraph& graph) {
    return refs(unit, graph, { "local_var", "set_local_var" });
}

} // namespace geck::ssl
