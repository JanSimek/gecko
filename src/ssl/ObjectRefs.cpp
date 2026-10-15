#include "ssl/ObjectRefs.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <string_view>
#include <utility>

namespace geck::ssl {

namespace {

    std::string lower(std::string_view text) {
        std::string out(text);
        std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return out;
    }

    // The opcodes that pop a PID, and the argument it is (the engine pops it first, so it is last).
    constexpr std::array<std::pair<std::string_view, std::size_t>, 6> PID_ARGUMENT{ {
        { "create_object_sid", 0 },
        { "obj_is_carrying_obj_pid", 1 },
        { "obj_carrying_pid_obj", 1 },
        { "tile_contains_obj_pid", 2 },
        { "tile_contains_pid_obj", 2 },
        { "party_member_obj", 0 },
    } };

    bool isObjPid(const Expr* expr) {
        const auto* call = expr != nullptr ? std::get_if<Call>(&expr->node) : nullptr;
        return call != nullptr && lower(call->callee) == "obj_pid";
    }

    // Visits every live expression of `unit`, with its procedure.
    template <typename Visit>
    void forEachLive(const ScriptUnit& unit, const CallGraph& graph, Visit&& visit) {
        for (const Procedure& procedure : unit.program().procedures) {
            if (procedure.body.has_value() && graph.isLive(procedure.name)) {
                graph.forEachLiveExpression(procedure, [&procedure, &visit](const Expr& expr) { visit(procedure, expr); });
            }
        }
    }

} // namespace

std::vector<PidRef> pidRefs(const ScriptUnit& unit, const CallGraph& graph) {
    std::vector<PidRef> found;
    const auto add = [&unit, &found](const Procedure& procedure, const Expr& at, int64_t pid, std::string via) {
        found.push_back({ pid, std::move(via), procedure.name, unit.writtenAt(at.range) });
    };
    forEachLive(unit, graph, [&graph, &add](const Procedure& procedure, const Expr& expr) {
        if (const auto* call = std::get_if<Call>(&expr.node)) {
            const std::string callee = lower(call->callee);
            const auto it = std::ranges::find(PID_ARGUMENT, std::string_view(callee), &std::pair<std::string_view, std::size_t>::first);
            if (it != PID_ARGUMENT.end() && it->second < call->args.size() && call->args[it->second]) {
                if (const auto pid = graph.folder().fold(*call->args[it->second]); pid.has_value()) {
                    add(procedure, expr, *pid, callee);
                }
            }
            return;
        }
        const auto* binary = std::get_if<Binary>(&expr.node);
        if (binary == nullptr || (binary->op != "==" && binary->op != "!=") || !binary->left || !binary->right) {
            return;
        }
        // obj_pid(x) == PID, either way round.
        const Expr* other = isObjPid(binary->left.get()) ? binary->right.get() : isObjPid(binary->right.get()) ? binary->left.get()
                                                                                                               : nullptr;
        if (other != nullptr) {
            if (const auto pid = graph.folder().fold(*other); pid.has_value()) {
                add(procedure, expr, *pid, "obj_pid");
            }
        }
    });
    return found;
}

std::vector<MapRef> mapRefs(const ScriptUnit& unit, const CallGraph& graph) {
    std::vector<MapRef> found;
    forEachLive(unit, graph, [&unit, &graph, &found](const Procedure& procedure, const Expr& expr) {
        const auto* call = std::get_if<Call>(&expr.node);
        if (call == nullptr || lower(call->callee) != "load_map" || call->args.size() != 2 || !call->args[0]) {
            return;
        }
        MapRef ref;
        if (const auto* text = std::get_if<StringLiteral>(&call->args[0]->node)) {
            ref.name = text->value;
        } else {
            ref.index = graph.folder().fold(*call->args[0]);
        }
        if (call->args[1]) {
            ref.entrance = graph.folder().fold(*call->args[1]);
        }
        ref.procedure = procedure.name;
        ref.location = unit.writtenAt(expr.range);
        found.push_back(std::move(ref));
    });
    return found;
}

} // namespace geck::ssl
