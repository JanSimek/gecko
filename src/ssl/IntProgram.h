#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace geck::ssl {

/// One decoded instruction of a compiled Fallout 2 script. `offset` is the instruction pointer the
/// engine's interpreter sees for it (the start of its 16-bit opcode), so it matches the jump targets
/// that scripts push as integer operands.
struct IntInstruction {
    std::size_t offset = 0;
    uint16_t opcode = 0;
    std::string name;               ///< engine name (op_* where the engine registration carries one)
    std::string operand;            ///< inline operand of a push: integer, float, or a quoted static string
    std::string note;               ///< annotation, e.g. the GVAR_* a literal global-variable index names
    std::optional<int32_t> literal; ///< the value of an integer push
    /// How many values it pops off the data stack and pushes (0 or 1), as its engine handler does;
    /// -1 when that can't be told (control flow, a variable argument count). See IntOpcodes.inc.
    int pops = -1;
    int pushes = -1;
};

struct IntProcedure {
    std::string name;
    int32_t flags = 0;
    int32_t time = 0;
    int32_t conditionOffset = 0;
    int32_t bodyOffset = 0;
    int32_t argCount = 0;
    std::vector<IntInstruction> code;
    /// A conditional procedure's condition (PROCEDURE_FLAG_CONDITIONAL), evaluated by the interpreter
    /// before the body runs; empty when conditionOffset is 0.
    std::vector<IntInstruction> condition;
};

struct IntProgram {
    std::vector<IntProcedure> procedures;
    std::size_t codeStart = 0; ///< first byte after the identifier and static-string tables
};

/// Disassemble a compiled .int script using the layout fallout2-ce's programCreateByPath and
/// interpreter loop read. A procedure's body, and its condition when it has one, each run from their
/// offset to the next body or condition offset in the file. Pass `globalVarNames` (vault13.gam order) to
/// annotate global-variable reads and writes whose index is a literal push. Throws ParseException (a
/// std::runtime_error) on a table or offset that points outside the file.
IntProgram disassembleInt(const std::vector<uint8_t>& bytes, const std::vector<std::string>* globalVarNames = nullptr);

} // namespace geck::ssl
