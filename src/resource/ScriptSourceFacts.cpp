#include "resource/ScriptSourceFacts.h"

#include <algorithm>
#include <regex>

namespace geck::resource {

namespace {

    // Blanks out // and /* */ comments and the text of string literals while keeping every newline
    // and offset, so a match position still maps to its line. Neither can contain a call, and a "//"
    // inside a string is not a comment.
    std::string withoutComments(std::string_view source) {
        std::string out(source);
        enum class State { Code,
            Line,
            Block,
            String } state = State::Code;
        for (std::size_t i = 0; i < out.size(); ++i) {
            const char c = out[i];
            const char next = i + 1 < out.size() ? out[i + 1] : '\0';
            switch (state) {
                case State::Code:
                    if (c == '"') {
                        state = State::String;
                    } else if (c == '/' && next == '/') {
                        state = State::Line;
                        out[i] = ' ';
                    } else if (c == '/' && next == '*') {
                        state = State::Block;
                        out[i] = ' ';
                        out[++i] = ' ';
                    }
                    break;
                case State::String:
                    if (c == '"') {
                        state = State::Code;
                    } else if (c == '\n') {
                        state = State::Code; // unterminated: don't swallow the rest of the file
                    } else {
                        out[i] = ' ';
                    }
                    break;
                case State::Line:
                    if (c == '\n') {
                        state = State::Code;
                    } else {
                        out[i] = ' ';
                    }
                    break;
                case State::Block:
                    if (c == '*' && next == '/') {
                        out[i] = ' ';
                        out[++i] = ' ';
                        state = State::Code;
                    } else if (c != '\n') {
                        out[i] = ' ';
                    }
                    break;
            }
        }
        return out;
    }

    int lineAt(const std::string& text, std::size_t offset) {
        return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
    }

    // The procedure whose body contains `offset`: the last `procedure <name> begin` before it.
    // Forward declarations (`procedure name;`) have no `begin` and are skipped.
    std::string procedureAt(const std::string& text, std::size_t offset) {
        static const std::regex header(R"(\bprocedure\s+(\w+)\s+begin\b)", std::regex::icase);
        std::string name;
        const auto end = text.begin() + static_cast<std::ptrdiff_t>(offset);
        for (std::sregex_iterator it(text.begin(), end, header), last; it != last; ++it) {
            name = (*it)[1].str();
        }
        return name;
    }

} // namespace

bool ScriptSourceFacts::assigns(Override::Field field) const {
    return std::any_of(overrides.begin(), overrides.end(), [field](const Override& o) { return o.field == field; });
}

ScriptSourceFacts inspectScriptSource(std::string_view source) {
    const std::string code = withoutComments(source);
    ScriptSourceFacts facts;

    // Both the raw opcode and the command.h macros that wrap it, applied to the script's own object.
    struct Pattern {
        ScriptSourceFacts::Override::Field field;
        std::regex regex;
    };
    static const std::vector<Pattern> patterns = {
        { ScriptSourceFacts::Override::Field::AiPacket,
            std::regex(R"(\bcritter_add_trait\s*\(\s*self_obj\s*,\s*TRAIT_OBJECT\s*,\s*OBJECT_AI_PACKET\b)") },
        { ScriptSourceFacts::Override::Field::AiPacket, std::regex(R"(\bset_self_ai\s*\()") },
        { ScriptSourceFacts::Override::Field::AiPacket, std::regex(R"(\bset_ai\s*\(\s*self_obj\s*,)") },
        { ScriptSourceFacts::Override::Field::Team,
            std::regex(R"(\bcritter_add_trait\s*\(\s*self_obj\s*,\s*TRAIT_OBJECT\s*,\s*OBJECT_TEAM_NUM\b)") },
        { ScriptSourceFacts::Override::Field::Team, std::regex(R"(\bset_self_team\s*\()") },
        { ScriptSourceFacts::Override::Field::Team, std::regex(R"(\bset_team\s*\(\s*self_obj\s*,)") },
    };
    for (const Pattern& pattern : patterns) {
        for (std::sregex_iterator it(code.begin(), code.end(), pattern.regex), last; it != last; ++it) {
            const auto offset = static_cast<std::size_t>(it->position());
            const std::string procedure = procedureAt(code, offset);
            if (procedure.empty()) {
                continue; // a macro definition, not a call
            }
            facts.overrides.push_back({ pattern.field, procedure, lineAt(code, offset) });
        }
    }
    std::sort(facts.overrides.begin(), facts.overrides.end(),
        [](const auto& a, const auto& b) { return a.line < b.line; });

    static const std::regex lvar(R"(#define\s+(LVAR_\w+)\s+\(?\s*(\d+)\s*\)?)");
    for (std::sregex_iterator it(code.begin(), code.end(), lvar), last; it != last; ++it) {
        facts.localVarNames.emplace(std::stoi((*it)[2].str()), (*it)[1].str());
    }
    return facts;
}

} // namespace geck::resource
