#include <catch2/catch_test_macros.hpp>
#include <cstddef>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "cli/IntDisassembler.h"

using namespace geck;

namespace {

void put32(std::vector<uint8_t>& bytes, std::size_t at, int32_t value) {
    const auto v = static_cast<uint32_t>(value);
    bytes[at] = static_cast<uint8_t>(v >> 24);
    bytes[at + 1] = static_cast<uint8_t>(v >> 16);
    bytes[at + 2] = static_cast<uint8_t>(v >> 8);
    bytes[at + 3] = static_cast<uint8_t>(v);
}

void push32(std::vector<uint8_t>& bytes, int32_t value) {
    bytes.resize(bytes.size() + 4);
    put32(bytes, bytes.size() - 4, value);
}

void push16(std::vector<uint8_t>& bytes, uint16_t value) {
    bytes.push_back(static_cast<uint8_t>(value >> 8));
    bytes.push_back(static_cast<uint8_t>(value));
}

void pushCString(std::vector<uint8_t>& bytes, std::string_view text) {
    bytes.insert(bytes.end(), text.begin(), text.end());
    bytes.push_back(0);
}

// A one-procedure program laid out as fallout2-ce's programCreateByPath reads it: 42 header bytes,
// the procedure table, the identifier table (names addressed from its length field), the static-string
// table (strings addressed 4 bytes past its length field), then code.
std::vector<uint8_t> buildProgram() {
    std::vector<uint8_t> bytes(42, 0);
    push32(bytes, 1);
    const std::size_t record = bytes.size();
    bytes.resize(bytes.size() + 24, 0);

    push32(bytes, 6);
    pushCString(bytes, "start");
    push32(bytes, 6);
    pushCString(bytes, "hello");

    const std::size_t body = bytes.size();
    push16(bytes, 0xC001);
    push32(bytes, 1);
    push16(bytes, 0x80C5); // op_get_global_var
    push16(bytes, 0xC001);
    push32(bytes, 0);
    push16(bytes, 0xC001);
    push32(bytes, 42);
    push16(bytes, 0x80C6); // op_set_global_var pops the value, then the index
    push16(bytes, 0x9001);
    push32(bytes, 0);
    push16(bytes, 0x8010); // OPCODE_EXIT_PROGRAM

    put32(bytes, record, 4); // "start" sits just past the identifier table's length field
    put32(bytes, record + 16, static_cast<int32_t>(body));
    return bytes;
}

} // namespace

TEST_CASE("disassembleInt decodes the procedure table, pushes and global-variable annotations", "[int_disassembler]") {
    const auto bytes = buildProgram();
    const std::vector<std::string> globals{ "GVAR_A", "GVAR_B" };
    const auto program = cli::disassembleInt(bytes, &globals);

    REQUIRE(program.procedures.size() == 1);
    const auto& procedure = program.procedures[0];
    CHECK(procedure.name == "start");
    REQUIRE(procedure.code.size() == 7);

    CHECK(procedure.code[0].name == "OPCODE_PUSH");
    CHECK(procedure.code[0].operand == "1");
    CHECK(procedure.code[1].name == "op_get_global_var");
    CHECK(procedure.code[1].note == "GVAR_B");
    CHECK(procedure.code[1].offset == procedure.code[0].offset + 6);
    CHECK(procedure.code[4].name == "op_set_global_var");
    CHECK(procedure.code[4].note == "GVAR_A");
    CHECK(procedure.code[5].operand == "\"hello\"");
    CHECK(procedure.code[6].name == "OPCODE_EXIT_PROGRAM");
}

TEST_CASE("disassembleInt names only literal global-variable indices", "[int_disassembler]") {
    // push 1, push 1, OPCODE_ADD, op_get_global_var: the index is computed, so it is left unnamed.
    std::vector<uint8_t> bytes(42, 0);
    push32(bytes, 1);
    const std::size_t record = bytes.size();
    bytes.resize(bytes.size() + 24, 0);
    push32(bytes, 6);
    pushCString(bytes, "start");
    push32(bytes, 0);
    const std::size_t body = bytes.size();
    push16(bytes, 0xC001);
    push32(bytes, 1);
    push16(bytes, 0xC001);
    push32(bytes, 1);
    push16(bytes, 0x8039); // OPCODE_ADD
    push16(bytes, 0x80C5); // op_get_global_var
    put32(bytes, record, 4);
    put32(bytes, record + 16, static_cast<int32_t>(body));

    const std::vector<std::string> globals{ "GVAR_A", "GVAR_B", "GVAR_C" };
    const auto program = cli::disassembleInt(bytes, &globals);
    REQUIRE(program.procedures.at(0).code.size() == 4);
    CHECK(program.procedures[0].code[2].name == "OPCODE_ADD");
    CHECK(program.procedures[0].code[3].name == "op_get_global_var");
    CHECK(program.procedures[0].code[3].note.empty());

    // Without a dictionary, even a literal index stays unnamed.
    const auto unnamed = cli::disassembleInt(buildProgram(), nullptr);
    CHECK(unnamed.procedures.at(0).code.at(1).note.empty());
}

TEST_CASE("disassembleInt accepts a script without static strings", "[int_disassembler]") {
    // Shipped scripts with no string literals store -1 as the static-string table length.
    std::vector<uint8_t> bytes(42, 0);
    push32(bytes, 1);
    const std::size_t record = bytes.size();
    bytes.resize(bytes.size() + 24, 0);
    push32(bytes, 6);
    pushCString(bytes, "start");
    push32(bytes, -1);
    const std::size_t body = bytes.size();
    push16(bytes, 0x8010); // OPCODE_EXIT_PROGRAM
    put32(bytes, record, 4);
    put32(bytes, record + 16, static_cast<int32_t>(body));

    const auto program = cli::disassembleInt(bytes);
    REQUIRE(program.procedures.at(0).code.size() == 1);
    CHECK(program.procedures[0].code[0].name == "OPCODE_EXIT_PROGRAM");
}

TEST_CASE("disassembleInt disassembles a conditional procedure's condition", "[int_disassembler]") {
    // The body (OPCODE_EXIT_PROGRAM) is followed by the condition (push 1, OPCODE_POP_RETURN). The
    // condition offset only bounds the body; the condition itself must still come back.
    std::vector<uint8_t> bytes(42, 0);
    push32(bytes, 1);
    const std::size_t record = bytes.size();
    bytes.resize(bytes.size() + 24, 0);
    push32(bytes, 6);
    pushCString(bytes, "start");
    push32(bytes, -1);
    const std::size_t body = bytes.size();
    push16(bytes, 0x8010); // OPCODE_EXIT_PROGRAM
    const std::size_t condition = bytes.size();
    push16(bytes, 0xC001);
    push32(bytes, 1);
    push16(bytes, 0x801C); // OPCODE_POP_RETURN
    put32(bytes, record, 4);
    put32(bytes, record + 4, 0x02); // PROCEDURE_FLAG_CONDITIONAL
    put32(bytes, record + 12, static_cast<int32_t>(condition));
    put32(bytes, record + 16, static_cast<int32_t>(body));

    const auto program = cli::disassembleInt(bytes);
    const auto& procedure = program.procedures.at(0);
    REQUIRE(procedure.code.size() == 1);
    CHECK(procedure.code[0].name == "OPCODE_EXIT_PROGRAM");
    REQUIRE(procedure.condition.size() == 2);
    CHECK(procedure.condition[0].name == "OPCODE_PUSH");
    CHECK(procedure.condition[0].operand == "1");
    CHECK(procedure.condition[1].name == "OPCODE_POP_RETURN");
}

TEST_CASE("disassembleInt rejects tables that point outside the file", "[int_disassembler]") {
    auto bytes = buildProgram();
    bytes.resize(50);
    CHECK_THROWS_AS(cli::disassembleInt(bytes), std::runtime_error);
}
