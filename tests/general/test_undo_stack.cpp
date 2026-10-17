#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

#include "ui/core/EditorSession.h"
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

TEST_CASE("EditorSession::resetToEmptyMap drops the old map's undo history", "[undo]") {
    geck::EditorSession session;
    session.resetToEmptyMap();
    int undone = 0;
    session.undoStack().push({ .description = "Place", .undo = [&] { ++undone; }, .redo = [] {} });
    REQUIRE(session.undoStack().canUndo());

    // File > New: replaying a command recorded against the previous map would write its
    // objects and tiles onto the blank one.
    session.resetToEmptyMap();

    CHECK_FALSE(session.undoStack().canUndo());
    CHECK_FALSE(session.undoStack().undo());
    CHECK(undone == 0);
}

TEST_CASE("UndoStack folds a push into the newest entry with the same merge key", "[undo]") {
    UndoStack stack;
    int value = 0;
    auto setTo = [&value](int from, int to, std::string key) {
        return UndoCommand{ .description = "Set",
            .undo = [&value, from] { value = from; },
            .redo = [&value, to] { value = to; },
            .mergeKey = std::move(key) };
    };

    value = 1;
    stack.push(setTo(0, 1, "field"));
    value = 2;
    stack.push(setTo(1, 2, "field"));
    value = 3;
    stack.push(setTo(2, 3, "other"));

    REQUIRE(stack.undo()); // "other"
    CHECK(value == 2);
    REQUIRE(stack.undo()); // both "field" pushes in one step
    CHECK(value == 0);
    CHECK_FALSE(stack.canUndo());
    REQUIRE(stack.redo());
    CHECK(value == 2);

    // With a redo entry pending ("other"), the push starts a new step instead of folding.
    REQUIRE(stack.canRedo());
    value = 5;
    stack.push(setTo(2, 5, "field"));
    REQUIRE(stack.undo());
    CHECK(value == 2);
}
