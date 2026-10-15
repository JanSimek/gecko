#include "ssl/IntProgram.h"

#include "reader/ReaderExceptions.h"

#include <algorithm>
#include <array>
#include <bit>
#include <format>
#include <set>
#include <stdexcept>
#include <string_view>

namespace geck::ssl {

namespace {

    struct OpcodeName {
        uint16_t index;
        const char* name;
        int pops;
        int pushes;
    };

    constexpr auto kOpcodeNames = std::to_array<OpcodeName>({
#include "ssl/IntOpcodes.inc"
    });

    // Layout constants from fallout2-ce interpreter.cc programCreateByPath / interpreter.h.
    constexpr std::size_t PROCEDURE_TABLE_OFFSET = 42; // program->procedures = data + 42
    constexpr std::size_t PROCEDURE_RECORD_SIZE = 24;  // sizeof(Procedure): six int32 fields
    constexpr uint16_t OPCODE_INDEX_MASK = 0x3FF;      // the dispatcher indexes handlers by opcode & 0x3FF
    constexpr uint16_t OPCODE_PUSH_INDEX = 0x001;
    constexpr uint16_t OPCODE_FETCH_EXTERNAL_INDEX = 0x014;
    constexpr uint16_t OPCODE_STORE_EXTERNAL_INDEX = 0x015;
    constexpr uint16_t OPCODE_EXPORT_VARIABLE_INDEX = 0x016;
    constexpr uint16_t VALUE_TYPE_INT = 0xC001;
    constexpr uint16_t VALUE_TYPE_FLOAT = 0xA001;
    constexpr uint16_t VALUE_TYPE_STRING = 0x9001;
    constexpr uint16_t VALUE_TYPE_DYNAMIC_STRING = 0x9801;

    const std::array<const OpcodeName*, 1024>& opcodeTable() {
        static const std::array<const OpcodeName*, 1024> table = [] {
            std::array<const OpcodeName*, 1024> t{ };
            for (const auto& entry : kOpcodeNames) {
                t[entry.index] = &entry;
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

    // The opcodes that pop a name the engine reads from the identifier table (fallout2-ce interpreter.cc
    // programGetIdentifier), not the static strings: an imported or exported variable's.
    bool popsIdentifier(uint16_t opcode) {
        const uint16_t index = opcode & OPCODE_INDEX_MASK;
        return (opcode & 0x8000) != 0 && (index == OPCODE_FETCH_EXTERNAL_INDEX || index == OPCODE_STORE_EXTERNAL_INDEX || index == OPCODE_EXPORT_VARIABLE_INDEX);
    }

    /// Where names are read from: identifiers are addressed from the identifier table's length field,
    /// static strings from 4 bytes past theirs.
    struct NameTables {
        std::size_t identifiers;
        std::size_t staticStrings;
    };

    /// Sets a push's operand (and literal) from its 32-bit `value`. `namesIdentifier`: the next
    /// instruction imports or exports a variable, so a string is the variable's identifier.
    void decodePush(const Bytes& bytes, IntInstruction& instruction, int32_t value, bool namesIdentifier, const NameTables& names) {
        switch (instruction.opcode) {
            case VALUE_TYPE_INT:
                instruction.operand = std::to_string(value);
                instruction.literal = value;
                break;
            case VALUE_TYPE_FLOAT:
                instruction.operand = std::format("{}", std::bit_cast<float>(static_cast<uint32_t>(value)));
                break;
            case VALUE_TYPE_STRING:
                if (namesIdentifier) {
                    instruction.operand = quoted(bytes.cString(names.identifiers + checkedOffset(value, "identifier offset"), "identifier"));
                    instruction.note = "identifier";
                } else {
                    instruction.operand = quoted(bytes.cString(names.staticStrings + 4 + checkedOffset(value, "string offset"), "static string"));
                }
                break;
            case VALUE_TYPE_DYNAMIC_STRING:
                instruction.operand = std::format("dynamic string #{}", value);
                break;
            default:
                instruction.operand = std::to_string(value);
                break;
        }
    }

    /// The GVAR_* a global-variable read or write names, when its index is the literal pushed for it.
    std::string globalVariableNote(std::string_view opcode, const std::vector<int32_t>& literalPushes,
        const std::vector<std::string>* globalVarNames) {
        // op_set_global_var pops the value, then the index (fallout2-ce interpreter_extra.cc).
        std::optional<int32_t> index;
        if (opcode == "op_get_global_var" && !literalPushes.empty()) {
            index = literalPushes.back();
        } else if (opcode == "op_set_global_var" && literalPushes.size() >= 2) {
            index = literalPushes[literalPushes.size() - 2];
        }
        if (!index.has_value() || globalVarNames == nullptr || *index < 0 || static_cast<std::size_t>(*index) >= globalVarNames->size()) {
            return { };
        }
        return (*globalVarNames)[static_cast<std::size_t>(*index)];
    }

    void disassembleRange(const Bytes& bytes, std::size_t begin, std::size_t end, const NameTables& names,
        const std::vector<std::string>* globalVarNames, std::vector<IntInstruction>& code) {
        const auto& opcodes = opcodeTable();
        // Integer literals pushed immediately before the current instruction, oldest first. Only an
        // uninterrupted run of pushes is trusted to still be on top of the stack.
        std::vector<int32_t> literalPushes;

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
            if (const OpcodeName* known = opcodes[index]; known != nullptr) {
                instruction.name = known->name;
                instruction.pops = known->pops;
                instruction.pushes = known->pushes;
            } else {
                instruction.name = std::format("unknown_0x{:04X}", instruction.opcode);
            }

            if (index == OPCODE_PUSH_INDEX) {
                instruction.pops = 0;
                instruction.pushes = 1;
                if (ip + 4 > end) {
                    instruction.note = "operand truncated by the end of the procedure";
                    code.push_back(std::move(instruction));
                    break;
                }
                const int32_t value = bytes.be32(ip, "push operand");
                ip += 4;
                decodePush(bytes, instruction, value, ip + 2 <= end && popsIdentifier(bytes.be16(ip, "opcode")), names);
                if (instruction.literal.has_value()) {
                    literalPushes.push_back(*instruction.literal);
                } else {
                    literalPushes.clear();
                }
            } else {
                instruction.note = globalVariableNote(instruction.name, literalPushes, globalVarNames);
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

    const NameTables names{ identifiers, staticStrings };
    const auto disassembleFrom = [&bytes, &boundaries, &names, globalVarNames](int32_t offset, const char* what,
                                     std::vector<IntInstruction>& code) {
        const auto begin = static_cast<std::size_t>(offset);
        const std::size_t end = *boundaries.upper_bound(begin);
        bytes.require(begin, end - begin, what);
        disassembleRange(bytes, begin, end, names, globalVarNames, code);
    };
    for (auto& procedure : program.procedures) {
        disassembleFrom(procedure.bodyOffset, "procedure body", procedure.code);
        if (procedure.conditionOffset > 0) {
            disassembleFrom(procedure.conditionOffset, "procedure condition", procedure.condition);
        }
    }
    return program;
}

} // namespace geck::ssl
