#include "ssl/CallGraph.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <vector>

namespace geck::ssl {

namespace {

    std::string lower(std::string_view text) {
        std::string out(text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    // fallout2-ce scripts.cc gScriptProcNames, without its "none_x_bad" placeholders.
    constexpr std::array<std::string_view, 24> ENGINE_PROCEDURES{ "no_p_proc", "start", "spatial_p_proc",
        "description_p_proc", "pickup_p_proc", "drop_p_proc", "use_p_proc", "use_obj_on_p_proc", "use_skill_on_p_proc",
        "talk_p_proc", "critter_p_proc", "combat_p_proc", "damage_p_proc", "map_enter_p_proc", "map_exit_p_proc",
        "create_p_proc", "destroy_p_proc", "look_at_p_proc", "timed_event_p_proc", "map_update_p_proc", "push_p_proc",
        "is_dropping_p_proc", "combat_is_starting_p_proc", "combat_is_over_p_proc" };

    // Kept by sslc as well: Fallout 1's description handler. (Current sslc also protects node998 and
    // node999, but the compiler the RP was built with drops them when nothing calls them.)
    constexpr std::string_view FALLOUT1_DESCRIPTION = "desc_p_proc";

    // Every name a procedure declares for itself: parameters, local variables, foreach variables.
    std::set<std::string> localNames(const Procedure& procedure) {
        std::set<std::string> names;
        for (const Parameter& parameter : procedure.parameters) {
            names.insert(lower(parameter.name));
        }
        forEachStatement(*procedure.body, [&names](const Stmt& stmt) {
            if (const auto* locals = std::get_if<LocalVariables>(&stmt.node)) {
                for (const Variable& variable : locals->variables) {
                    names.insert(lower(variable.name));
                }
            } else if (const auto* loop = std::get_if<ForEach>(&stmt.node); loop != nullptr && loop->declares) {
                names.insert(lower(loop->value));
                if (!loop->key.empty()) {
                    names.insert(lower(loop->key));
                }
            }
        });
        return names;
    }

    // Calls `visit` for every expression that can run in `stmt`: the dead branch of an `if` on a
    // constant is left out.
    struct LiveScope {
        const ConstantFolder& folder;
        const std::set<std::string>& shadowed; // the procedure's own names, which hide globals
    };

    void liveExpressions(const LiveScope& scope, const Stmt& stmt, const ExprVisitor& visit) {
        forEachDirectExpression(stmt, visit);
        if (const auto* branch = std::get_if<If>(&stmt.node)) {
            const auto condition = branch->condition ? scope.folder.fold(*branch->condition, scope.shadowed) : std::nullopt;
            if (branch->then && condition.value_or(1) != 0) {
                liveExpressions(scope, *branch->then, visit);
            }
            if (branch->otherwise && condition.value_or(0) == 0) {
                liveExpressions(scope, *branch->otherwise, visit);
            }
            return;
        }
        forEachChildStatement(stmt, [&scope, &visit](const Stmt& child) { liveExpressions(scope, child, visit); });
    }

    // The name an expression refers to when it can name a procedure: a call's callee, a bare name, or
    // an `@name` reference.
    std::string referencedName(const Expr& expr) {
        if (const auto* call = std::get_if<Call>(&expr.node)) {
            return lower(call->callee);
        }
        if (const auto* name = std::get_if<Name>(&expr.node)) {
            return lower(name->name);
        }
        if (const auto* ref = std::get_if<ProcedureRef>(&expr.node)) {
            return lower(ref->name); // `@proc`: sslc keeps a procedure passed by reference
        }
        return {};
    }

} // namespace

bool isEngineProcedure(std::string_view name) {
    const std::string lowered = lower(name);
    return std::ranges::find(ENGINE_PROCEDURES, lowered) != ENGINE_PROCEDURES.end();
}

CallGraph::CallGraph(const Program& program)
    : _folder(program) {
    std::set<std::string> procedures;
    std::vector<std::string> roots;
    for (const Procedure& procedure : program.procedures) {
        const std::string name = lower(procedure.name);
        procedures.insert(name);
        const bool protectedName = isEngineProcedure(name) || name == FALLOUT1_DESCRIPTION;
        if (protectedName || procedure.linkage == Linkage::Export || procedure.timedDelay || procedure.condition) {
            roots.push_back(name);
        }
    }

    for (const Procedure& procedure : program.procedures) {
        if (!procedure.body.has_value()) {
            continue;
        }
        _localNames[lower(procedure.name)] = localNames(procedure);
        auto& refs = _references[lower(procedure.name)];
        const std::set<std::string>& shadowed = _localNames[lower(procedure.name)];
        forEachLiveExpression(procedure, [&procedures, &shadowed, &refs](const Expr& expr) {
            if (std::string name = referencedName(expr); procedures.contains(name) && !shadowed.contains(name)) {
                refs.insert(std::move(name));
            }
        });
    }

    // Everything reachable from an entry point.
    std::vector<std::string> pending(roots.begin(), roots.end());
    while (!pending.empty()) {
        std::string name = std::move(pending.back());
        pending.pop_back();
        if (!_live.insert(name).second) {
            continue;
        }
        if (const auto it = _references.find(name); it != _references.end()) {
            pending.insert(pending.end(), it->second.begin(), it->second.end());
        }
    }
}

const std::set<std::string>& CallGraph::references(std::string_view procedure) const {
    static const std::set<std::string> none;
    const auto it = _references.find(lower(procedure));
    return it == _references.end() ? none : it->second;
}

void CallGraph::forEachLiveExpression(const Procedure& procedure, const ExprVisitor& visit) const {
    if (procedure.condition) {
        forEachExpression(*procedure.condition, visit);
    }
    if (!procedure.body.has_value()) {
        return;
    }
    static const std::set<std::string> none;
    const auto names = _localNames.find(lower(procedure.name));
    const LiveScope scope{ _folder, names == _localNames.end() ? none : names->second };
    for (const StmtPtr& stmt : procedure.body->statements) {
        if (stmt) {
            liveExpressions(scope, *stmt, visit);
        }
    }
}

bool CallGraph::isLive(std::string_view procedure) const {
    return _live.contains(lower(procedure));
}

} // namespace geck::ssl
