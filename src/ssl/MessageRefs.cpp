#include "ssl/MessageRefs.h"

#include "reader/TextParsing.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace geck::ssl {

namespace {

    using text::toLower;

    // The message opcodes, with where their list and message arguments sit (fallout2-ce
    // interpreter_extra.cc: opGetMessageString, opGsayReply, opGsayMessage, opGsayOption, opGiqOption).
    struct MessageOpcode {
        std::string_view name;
        std::size_t listArg;
        std::size_t minArgs;
    };
    constexpr std::array<MessageOpcode, 5> OPCODES{ {
        { "message_str", 0, 2 },
        { "gsay_reply", 0, 2 },
        { "gsay_message", 0, 3 },
        { "gsay_option", 0, 4 },
        { "giq_option", 1, 5 },
    } };

} // namespace

bool isBuiltInMessage(int list, int number) {
    return (list == 0 && number == 0) || (list == -1 && number == -1) || (list == -2 && number == -2);
}

std::vector<MessageRef> messageRefs(const ScriptUnit& unit, const CallGraph& graph) {
    std::vector<MessageRef> found;
    for (const Procedure& procedure : unit.program().procedures) {
        if (!procedure.body.has_value() || !graph.isLive(procedure.name)) {
            continue;
        }
        graph.forEachLiveExpression(procedure, [&unit, &graph, &procedure, &found](const Expr& expr) {
            const auto* call = std::get_if<Call>(&expr.node);
            if (call == nullptr) {
                return;
            }
            const std::string name = toLower(call->callee);
            const auto opcode = std::ranges::find(OPCODES, name, &MessageOpcode::name);
            if (opcode == OPCODES.end() || call->args.size() < opcode->minArgs) {
                return;
            }
            const Expr* list = call->args[opcode->listArg].get();
            const Expr* number = call->args[opcode->listArg + 1].get();
            MessageRef ref;
            ref.opcode = name;
            if (const auto value = list ? graph.folder().fold(*list) : std::nullopt; value.has_value()) {
                ref.list = static_cast<int>(*value);
            }
            if (const auto value = number ? graph.folder().fold(*number) : std::nullopt; value.has_value()) {
                ref.number = static_cast<int>(*value);
            }
            ref.procedure = procedure.name;
            ref.location = unit.writtenAt(expr.range);
            found.push_back(std::move(ref));
        });
    }
    return found;
}

} // namespace geck::ssl
