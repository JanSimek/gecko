#include "cli/IntDisassembler.h"

#include "reader/ReaderExceptions.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <set>
#include <stdexcept>

namespace geck::cli {

namespace {

    struct OpcodeName {
        uint16_t index;
        const char* name;
    };

    constexpr auto kOpcodeNames = std::to_array<OpcodeName>({
#include "cli/IntOpcodes.inc"
    });

    // Layout constants from fallout2-ce interpreter.cc programCreateByPath / interpreter.h.
    constexpr std::size_t PROCEDURE_TABLE_OFFSET = 42; // program->procedures = data + 42
    constexpr std::size_t PROCEDURE_RECORD_SIZE = 24;  // sizeof(Procedure): six int32 fields
    constexpr uint16_t OPCODE_INDEX_MASK = 0x3FF;      // the dispatcher indexes handlers by opcode & 0x3FF
    constexpr uint16_t OPCODE_PUSH_INDEX = 0x001;
    constexpr uint16_t VALUE_TYPE_INT = 0xC001;
    constexpr uint16_t VALUE_TYPE_FLOAT = 0xA001;
    constexpr uint16_t VALUE_TYPE_STRING = 0x9001;
    constexpr uint16_t VALUE_TYPE_DYNAMIC_STRING = 0x9801;

    const std::array<const char*, 1024>& opcodeNameTable() {
        static const std::array<const char*, 1024> table = [] {
            std::array<const char*, 1024> t{};
            for (const auto& entry : kOpcodeNames) {
                t[entry.index] = entry.name;
            }
            return t;
        }();
        return table;
    }

    class Bytes {
    public:
        explicit Bytes(const std::vector<uint8_t>& data)
            : _data(data) {
        }

        void require(std::size_t offset, std::size_t length, const char* what) const {
            if (offset > _data.size() || length > _data.size() - offset) {
                throw ParseException(std::format("{} at offset {} runs past the end of the {}-byte file", what,
                    offset, _data.size()));
            }
        }

        int32_t be32(std::size_t offset, const char* what) const {
            require(offset, 4, what);
            return static_cast<int32_t>((uint32_t{ _data[offset] } << 24) | (uint32_t{ _data[offset + 1] } << 16)
                | (uint32_t{ _data[offset + 2] } << 8) | uint32_t{ _data[offset + 3] });
        }

        uint16_t be16(std::size_t offset, const char* what) const {
            require(offset, 2, what);
            return static_cast<uint16_t>((_data[offset] << 8) | _data[offset + 1]);
        }

        std::string cString(std::size_t offset, const char* what) const {
            require(offset, 1, what);
            const auto begin = _data.begin() + static_cast<std::ptrdiff_t>(offset);
            const auto end = std::find(begin, _data.end(), uint8_t{ 0 });
            return { begin, end };
        }

        std::size_t size() const { return _data.size(); }

    private:
        const std::vector<uint8_t>& _data;
    };

    std::size_t checkedOffset(int32_t value, const char* what) {
        if (value < 0) {
            throw ParseException(std::format("{} is negative ({})", what, value));
        }
        return static_cast<std::size_t>(value);
    }

    std::string quoted(const std::string& text) {
        std::string out = "\"";
        for (const char c : text) {
            if (c == '"' || c == '\\') {
                out += '\\';
            }
            out += c;
        }
        return out + "\"";
    }

    void disassembleRange(const Bytes& bytes, std::size_t begin, std::size_t end, std::size_t staticStrings,
        const std::vector<std::string>* globalVarNames, std::vector<IntInstruction>& code) {
        const auto& names = opcodeNameTable();
        // Integer literals pushed immediately before the current instruction, oldest first. Only an
        // uninterrupted run of pushes is trusted to still be on top of the stack.
        std::vector<int32_t> literalPushes;
        auto globalName = [globalVarNames](int32_t index) -> std::string {
            if (globalVarNames == nullptr || index < 0 || static_cast<std::size_t>(index) >= globalVarNames->size()) {
                return {};
            }
            return (*globalVarNames)[static_cast<std::size_t>(index)];
        };

        std::size_t ip = begin;
        while (ip + 2 <= end) {
            IntInstruction instruction;
            instruction.offset = ip;
            instruction.opcode = bytes.be16(ip, "opcode");
            ip += 2;

            // The interpreter rejects a word without the opcode bit ("Bad opcode"); report it as data.
            if ((instruction.opcode & 0x8000) == 0) {
                instruction.name = "<data>";
                instruction.operand = std::format("0x{:04X}", instruction.opcode);
                literalPushes.clear();
                code.push_back(std::move(instruction));
                continue;
            }

            const uint16_t index = instruction.opcode & OPCODE_INDEX_MASK;
            instruction.name = names[index] != nullptr ? names[index] : std::format("unknown_0x{:04X}", instruction.opcode);

            if (index == OPCODE_PUSH_INDEX) {
                if (ip + 4 > end) {
                    instruction.note = "operand truncated by the end of the procedure";
                    code.push_back(std::move(instruction));
                    break;
                }
                const int32_t value = bytes.be32(ip, "push operand");
                ip += 4;
                switch (instruction.opcode) {
                    case VALUE_TYPE_INT:
                        instruction.operand = std::to_string(value);
                        literalPushes.push_back(value);
                        break;
                    case VALUE_TYPE_FLOAT:
                        instruction.operand = std::format("{}", std::bit_cast<float>(static_cast<uint32_t>(value)));
                        literalPushes.clear();
                        break;
                    case VALUE_TYPE_STRING:
                        instruction.operand = quoted(bytes.cString(staticStrings + 4 + checkedOffset(value, "string offset"),
                            "static string"));
                        literalPushes.clear();
                        break;
                    case VALUE_TYPE_DYNAMIC_STRING:
                        instruction.operand = std::format("dynamic string #{}", value);
                        literalPushes.clear();
                        break;
                    default:
                        instruction.operand = std::to_string(value);
                        literalPushes.clear();
                        break;
                }
            } else {
                // op_set_global_var pops the value, then the index (fallout2-ce interpreter_extra.cc).
                if (instruction.name == "op_get_global_var" && !literalPushes.empty()) {
                    instruction.note = globalName(literalPushes.back());
                } else if (instruction.name == "op_set_global_var" && literalPushes.size() >= 2) {
                    instruction.note = globalName(literalPushes[literalPushes.size() - 2]);
                }
                literalPushes.clear();
            }
            code.push_back(std::move(instruction));
        }
    }

} // namespace

IntProgram disassembleInt(const std::vector<uint8_t>& data, const std::vector<std::string>* globalVarNames) {
    const Bytes bytes(data);
    IntProgram program;

    const int32_t count = bytes.be32(PROCEDURE_TABLE_OFFSET, "procedure count");
    if (count < 0) {
        throw ParseException(std::format("procedure count is negative ({})", count));
    }
    const std::size_t table = PROCEDURE_TABLE_OFFSET + 4;
    bytes.require(table, static_cast<std::size_t>(count) * PROCEDURE_RECORD_SIZE, "procedure table");

    // identifiers = procedures + 4 + 24 * count, starting with its own length; names are addressed from
    // that length field. Static strings follow, addressed from 4 bytes past their length field.
    const std::size_t identifiers = table + static_cast<std::size_t>(count) * PROCEDURE_RECORD_SIZE;
    const std::size_t staticStrings = identifiers + 4 + checkedOffset(bytes.be32(identifiers, "identifier table length"), "identifier table length");
    // The engine never reads the static-string table's length (strings are addressed from 4 bytes past
    // it), and a script without string literals stores -1 there, as the shipped ecplant.int does.
    const int32_t staticStringsLength = bytes.be32(staticStrings, "static string table length");
    program.codeStart = staticStrings + 4 + (staticStringsLength > 0 ? static_cast<std::size_t>(staticStringsLength) : 0);
    bytes.require(program.codeStart, 0, "code section");

    std::set boundaries{ bytes.size() };
    for (int32_t i = 0; i < count; ++i) {
        const std::size_t record = table + static_cast<std::size_t>(i) * PROCEDURE_RECORD_SIZE;
        IntProcedure procedure;
        const int32_t nameOffset = bytes.be32(record, "procedure name offset");
        procedure.flags = bytes.be32(record + 4, "procedure flags");
        procedure.time = bytes.be32(record + 8, "procedure time");
        procedure.conditionOffset = bytes.be32(record + 12, "procedure condition offset");
        procedure.bodyOffset = bytes.be32(record + 16, "procedure body offset");
        procedure.argCount = bytes.be32(record + 20, "procedure argument count");
        procedure.name = bytes.cString(identifiers + checkedOffset(nameOffset, "procedure name offset"), "procedure name");
        boundaries.insert(checkedOffset(procedure.bodyOffset, "procedure body offset"));
        if (procedure.conditionOffset > 0) {
            boundaries.insert(static_cast<std::size_t>(procedure.conditionOffset));
        }
        program.procedures.push_back(std::move(procedure));
    }

    const auto disassembleFrom = [&bytes, &boundaries, staticStrings, globalVarNames](int32_t offset, const char* what,
                                     std::vector<IntInstruction>& code) {
        const auto begin = static_cast<std::size_t>(offset);
        const std::size_t end = *boundaries.upper_bound(begin);
        bytes.require(begin, end - begin, what);
        disassembleRange(bytes, begin, end, staticStrings, globalVarNames, code);
    };
    for (auto& procedure : program.procedures) {
        disassembleFrom(procedure.bodyOffset, "procedure body", procedure.code);
        if (procedure.conditionOffset > 0) {
            disassembleFrom(procedure.conditionOffset, "procedure condition", procedure.condition);
        }
    }
    return program;
}

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
