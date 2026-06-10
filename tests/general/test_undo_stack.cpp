#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "util/UndoStack.h"

using namespace geck;

namespace {

// A command that appends "undo:<desc>" / "redo:<desc>" to a shared log so tests
// can assert exactly which closures ran and in what order.
UndoCommand loggingCommand(const std::string& desc, std::vector<std::string>& log) {
    UndoCommand cmd;
    cmd.description = desc;
    cmd.undo = [&log, desc]() { log.push_back("undo:" + desc); };
    cmd.redo = [&log, desc]() { log.push_back("redo:" + desc); };
    return cmd;
}

} // namespace

TEST_CASE("UndoStack runs the right closures in LIFO order", "[undo]") {
    UndoStack stack;
    std::vector<std::string> log;

    CHECK_FALSE(stack.canUndo());
    CHECK_FALSE(stack.canRedo());
    CHECK_FALSE(stack.undo()); // nothing to undo
    CHECK_FALSE(stack.redo()); // nothing to redo

    stack.push(loggingCommand("A", log));
    stack.push(loggingCommand("B", log));

    CHECK(stack.canUndo());
    CHECK(stack.lastUndoLabel() == "B");

    REQUIRE(stack.undo()); // B (last in)
    REQUIRE(stack.undo()); // A
    CHECK_FALSE(stack.undo());

    CHECK(stack.canRedo());
    CHECK(stack.lastRedoLabel() == "A");

    REQUIRE(stack.redo()); // A (last undone)
    REQUIRE(stack.redo()); // B
    CHECK_FALSE(stack.redo());

    CHECK(log == std::vector<std::string>{ "undo:B", "undo:A", "redo:A", "redo:B" });
}

TEST_CASE("UndoStack push invalidates the redo stack", "[undo]") {
    UndoStack stack;
    std::vector<std::string> log;

    stack.push(loggingCommand("A", log));
    REQUIRE(stack.undo());
    CHECK(stack.canRedo());

    stack.push(loggingCommand("B", log)); // a new edit drops the redo history
    CHECK_FALSE(stack.canRedo());
    CHECK(stack.lastRedoLabel().empty());
}

TEST_CASE("UndoStack rejects commands missing an undo or redo closure", "[undo]") {
    UndoStack stack;

    UndoCommand noUndo;
    noUndo.description = "noUndo";
    noUndo.redo = []() {};
    stack.push(std::move(noUndo));
    CHECK_FALSE(stack.canUndo());

    UndoCommand noRedo;
    noRedo.description = "noRedo";
    noRedo.undo = []() {};
    stack.push(std::move(noRedo));
    CHECK_FALSE(stack.canUndo());
}

TEST_CASE("UndoStack evicts the oldest command past maxCommands", "[undo]") {
    UndoStack stack(2);
    std::vector<std::string> log;

    stack.push(loggingCommand("A", log));
    stack.push(loggingCommand("B", log));
    stack.push(loggingCommand("C", log)); // evicts A (oldest)

    REQUIRE(stack.undo()); // C
    REQUIRE(stack.undo()); // B
    CHECK_FALSE(stack.undo()); // A was evicted, not reachable

    CHECK(log == std::vector<std::string>{ "undo:C", "undo:B" });
}

TEST_CASE("UndoStack clear empties both stacks", "[undo]") {
    UndoStack stack;
    std::vector<std::string> log;

    stack.push(loggingCommand("A", log));
    REQUIRE(stack.undo());
    stack.clear();

    CHECK_FALSE(stack.canUndo());
    CHECK_FALSE(stack.canRedo());
}
