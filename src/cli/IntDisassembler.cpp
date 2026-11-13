#include "cli/IntDisassembler.h"

#include <nlohmann/json.hpp>

#include <format>

namespace geck::cli {

using ssl::IntInstruction;
using ssl::IntProgram;

namespace {

    nlohmann::ordered_json codeLines(const std::vector<IntInstruction>& code) {
        auto lines = nlohmann::ordered_json::array();
        for (const auto& instruction : code) {
            std::string line = std::format("{:>6} {}", instruction.offset, instruction.name);
            if (!instruction.operand.empty()) {
                line += " " + instruction.operand;
            }
            if (!instruction.note.empty()) {
                line += " ; " + instruction.note;
            }
            lines.push_back(std::move(line));
        }
        return lines;
    }

} // namespace

nlohmann::ordered_json intProgramToJson(const IntProgram& program) {
    auto procedures = nlohmann::ordered_json::array();
    for (const auto& procedure : program.procedures) {
        const bool conditional = procedure.conditionOffset > 0;
        procedures.push_back({ { "name", procedure.name }, { "argCount", procedure.argCount }, { "flags", procedure.flags },
            { "bodyOffset", procedure.bodyOffset },
            { "conditionOffset", conditional ? nlohmann::ordered_json(procedure.conditionOffset) : nullptr },
            { "condition", conditional ? codeLines(procedure.condition) : nullptr }, { "code", codeLines(procedure.code) } });
    }
    return { { "codeStart", program.codeStart }, { "procedures", std::move(procedures) } };
}

} // namespace geck::cli
