#pragma once

#include <map>
#include <set>
#include <string>
#include <string_view>

#include "ssl/AstWalk.h"
#include "ssl/ConstantFolder.h"

namespace geck::ssl {

/// Which procedures refer to which, and which of them end up in the compiled script.
///
/// A reference is a call (`foo(x)`, `call foo`) or a procedure named as a value (`giq_option(..,
/// Node002, ..)`, `@tile_cb_func`), resolved the way sslc resolves a name: a parameter or local variable of the
/// procedure shadows a procedure of the same name. Names compare case-insensitively. Like sslc's
/// optimizer (optimize.c), a branch of an `if` whose condition folds to a constant that rules it out
/// refers to nothing (see ConstantFolder), e.g. define.h's `if (CALL_PICKUP == R_CALL_THROUGH) then
/// call old_pickup_p_proc`, or a test of a global the script never assigns.
class CallGraph {
public:
    explicit CallGraph(const Program& program);

    /// The procedures `procedure` refers to, lowercased.
    const std::set<std::string>& references(std::string_view procedure) const;

    /// Whether sslc compiles the procedure into the .int under its default optimisation (optimize.c,
    /// DecendUnusedProcedures): it is kept when it is an entry point, or a kept procedure refers to it.
    /// Entry points are the procedures the engine calls by name (fallout2-ce scripts.cc
    /// gScriptProcNames, plus Fallout 1's desc_p_proc), and timed, conditional and exported
    /// procedures. Code in a procedure that is not live never runs.
    bool isLive(std::string_view procedure) const;

    /// Every expression in `procedure` that can run: its body and condition, without the dead branch
    /// of an `if` on a constant. (Whether the procedure itself runs is isLive.)
    void forEachLiveExpression(const Procedure& procedure, const ExprVisitor& visit) const;

    const ConstantFolder& folder() const { return _folder; }

private:
    ConstantFolder _folder;
    std::map<std::string, std::set<std::string>> _localNames; // per procedure: names that hide globals
    std::map<std::string, std::set<std::string>> _references;
    std::set<std::string> _live;
};

/// The procedure names the engine calls (fallout2-ce scripts.cc gScriptProcNames), lowercased.
bool isEngineProcedure(std::string_view name);

} // namespace geck::ssl
