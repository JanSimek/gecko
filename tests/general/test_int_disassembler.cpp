#include <catch2/catch_test_macros.hpp>
#include <cstddef>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ssl/IntProgram.h"
#include "support/ByteWriter.h"
#include "support/CompiledScript.h"

using namespace geck;

namespace {

// A one-procedure program laid out as fallout2-ce's programCreateByPath reads it: 42 header bytes,
// the procedure table, the identifier table (names addressed from its length field), the static-string
// table (strings addressed 4 bytes past its length field), then code.
std::vector<uint8_t> buildProgram() {
    test::ByteWriter bytes;
    bytes.fill(0, 42);
    bytes.be32(1);
    const std::size_t record = bytes.size();
    bytes.fill(0, 24);

    bytes.be32(6);
    bytes.cstring("start");
    bytes.be32(6);
    bytes.cstring("hello");

    const std::size_t body = bytes.size();
    bytes.be16(0xC001);
    bytes.be32(1);
    bytes.be16(0x80C5); // op_get_global_var
    bytes.be16(0xC001);
    bytes.be32(0);
    bytes.be16(0xC001);
    bytes.be32(42);
    bytes.be16(0x80C6); // op_set_global_var pops the value, then the index
    bytes.be16(0x9001);
    bytes.be32(0);
    bytes.be16(0x8010); // OPCODE_EXIT_PROGRAM

    bytes.patchBe32(record, 4); // "start" sits just past the identifier table's length field
    bytes.patchBe32(record + 16, static_cast<uint32_t>(body));
    return bytes.data();
}

} // namespace

TEST_CASE("disassembleInt decodes the procedure table, pushes and global-variable annotations", "[int_disassembler]") {
    const auto bytes = buildProgram();
    const std::vector<std::string> globals{ "GVAR_A", "GVAR_B" };
    const auto program = ssl::disassembleInt(bytes, &globals);

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
    test::ByteWriter bytes;
    bytes.fill(0, 42);
    bytes.be32(1);
    const std::size_t record = bytes.size();
    bytes.fill(0, 24);
    bytes.be32(6);
    bytes.cstring("start");
    bytes.be32(0);
    const std::size_t body = bytes.size();
    bytes.be16(0xC001);
    bytes.be32(1);
    bytes.be16(0xC001);
    bytes.be32(1);
    bytes.be16(0x8039); // OPCODE_ADD
    bytes.be16(0x80C5); // op_get_global_var
    bytes.patchBe32(record, 4);
    bytes.patchBe32(record + 16, static_cast<uint32_t>(body));

    const std::vector<std::string> globals{ "GVAR_A", "GVAR_B", "GVAR_C" };
    const auto program = ssl::disassembleInt(bytes.data(), &globals);
    REQUIRE(program.procedures.at(0).code.size() == 4);
    CHECK(program.procedures[0].code[2].name == "OPCODE_ADD");
    CHECK(program.procedures[0].code[3].name == "op_get_global_var");
    CHECK(program.procedures[0].code[3].note.empty());

    // Without a dictionary, even a literal index stays unnamed.
    const auto unnamed = ssl::disassembleInt(buildProgram(), nullptr);
    CHECK(unnamed.procedures.at(0).code.at(1).note.empty());
}

TEST_CASE("disassembleInt accepts a script without static strings", "[int_disassembler]") {
    // Shipped scripts with no string literals store -1 as the static-string table length.
    test::ByteWriter bytes;
    bytes.fill(0, 42);
    bytes.be32(1);
    const std::size_t record = bytes.size();
    bytes.fill(0, 24);
    bytes.be32(6);
    bytes.cstring("start");
    bytes.be32(0xFFFFFFFF);
    const std::size_t body = bytes.size();
    bytes.be16(0x8010); // OPCODE_EXIT_PROGRAM
    bytes.patchBe32(record, 4);
    bytes.patchBe32(record + 16, static_cast<uint32_t>(body));

    const auto program = ssl::disassembleInt(bytes.data());
    REQUIRE(program.procedures.at(0).code.size() == 1);
    CHECK(program.procedures[0].code[0].name == "OPCODE_EXIT_PROGRAM");
}

TEST_CASE("disassembleInt disassembles a conditional procedure's condition", "[int_disassembler]") {
    // The body (OPCODE_EXIT_PROGRAM) is followed by the condition (push 1, OPCODE_POP_RETURN). The
    // condition offset only bounds the body; the condition itself must still come back.
    test::ByteWriter bytes;
    bytes.fill(0, 42);
    bytes.be32(1);
    const std::size_t record = bytes.size();
    bytes.fill(0, 24);
    bytes.be32(6);
    bytes.cstring("start");
    bytes.be32(0xFFFFFFFF);
    const std::size_t body = bytes.size();
    bytes.be16(0x8010); // OPCODE_EXIT_PROGRAM
    const std::size_t condition = bytes.size();
    bytes.be16(0xC001);
    bytes.be32(1);
    bytes.be16(0x801C); // OPCODE_POP_RETURN
    bytes.patchBe32(record, 4);
    bytes.patchBe32(record + 4, 0x02); // PROCEDURE_FLAG_CONDITIONAL
    bytes.patchBe32(record + 12, static_cast<uint32_t>(condition));
    bytes.patchBe32(record + 16, static_cast<uint32_t>(body));

    const auto program = ssl::disassembleInt(bytes.data());
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
    CHECK_THROWS_AS(ssl::disassembleInt(bytes), std::runtime_error);
}

TEST_CASE("disassembleInt reads an imported or exported variable's name from the identifier table", "[int_disassembler]") {
    // Vanilla wientdor.int: no static strings, and `push "<name>"; STORE_EXTERNAL` with the name's
    // offset into the identifier table, where the engine's programGetIdentifier reads it.
    test::ByteWriter code;
    test::pushInt(code, 1);
    code.be16(0x9001).be32(4); // the identifier at offset 4: the procedure's own name
    code.be16(0x8015);         // OPCODE_STORE_EXTERNAL
    const auto program = ssl::disassembleInt(test::compiledScript("Entrance_Door_Ptr", code.data()));
    REQUIRE(program.procedures.size() == 1);
    const auto& instructions = program.procedures[0].code;
    REQUIRE(instructions.size() == 3);
    CHECK(instructions[1].operand == "\"Entrance_Door_Ptr\"");
    CHECK(instructions[1].note == "identifier");
    CHECK(instructions[2].name == "OPCODE_STORE_EXTERNAL");
}
