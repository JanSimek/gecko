#include "resource/ScriptSourceFacts.h"

#include <algorithm>
#include <regex>

namespace geck::resource {

namespace {

    enum class LexState { Code,
        Line,
        Block,
        String };

    // Advances the scan by one character (two for a comment delimiter), blanking it when it belongs to
    // a comment or a string's text, and returns the state after it.
    LexState blankStep(std::string& out, std::size_t& i, LexState state) {
        const char c = out[i];
        const char next = i + 1 < out.size() ? out[i + 1] : '\0';
        switch (state) {
            case LexState::Code:
                if (c == '"') {
                    return LexState::String;
                }
                if (c == '/' && next == '/') {
                    out[i] = ' ';
                    return LexState::Line;
                }
                if (c == '/' && next == '*') {
                    out[i] = ' ';
                    out[++i] = ' ';
                    return LexState::Block;
                }
                return LexState::Code;
            case LexState::String:
                if (c == '"' || c == '\n') {
                    return LexState::Code; // an unterminated string doesn't swallow the rest of the file
                }
                out[i] = ' ';
                return LexState::String;
            case LexState::Line:
                if (c == '\n') {
                    return LexState::Code;
                }
                out[i] = ' ';
                return LexState::Line;
            case LexState::Block:
                if (c == '*' && next == '/') {
                    out[i] = ' ';
                    out[++i] = ' ';
                    return LexState::Code;
                }
                if (c != '\n') {
                    out[i] = ' ';
                }
                return LexState::Block;
        }
        return state;
    }

    // Blanks out line and block comments and the text of string literals while keeping every newline
    // and offset, so a match position still maps to its line. Neither can contain a call, and a
    // comment opener inside a string is not a comment.
    std::string withoutComments(std::string_view source) {
        std::string out(source);
        LexState state = LexState::Code;
        std::size_t i = 0;
        while (i < out.size()) {
            state = blankStep(out, i, state);
            ++i;
        }
        return out;
    }

    int lineAt(std::string_view text, std::size_t offset) {
        return 1 + static_cast<int>(std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(offset), '\n'));
    }

    // The procedure whose body contains `offset`: the last `procedure <name> begin` before it.
    // Forward declarations (`procedure name;`) have no `begin` and are skipped.
    std::string procedureAt(std::string_view text, std::size_t offset) {
        using Iterator = std::regex_iterator<std::string_view::const_iterator>;
        static const std::regex header(R"(\bprocedure\s+(\w+)\s+begin\b)", std::regex::icase);
        std::string name;
        const auto end = text.begin() + static_cast<std::ptrdiff_t>(offset);
        for (Iterator it(text.begin(), end, header), last; it != last; ++it) {
            name = (*it)[1].str();
        }
        return name;
    }

} // namespace

bool ScriptSourceFacts::assigns(Override::Field field) const {
    return std::ranges::any_of(overrides, [field](const Override& o) { return o.field == field; });
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
            facts.overrides.emplace_back(pattern.field, procedure, lineAt(code, offset));
        }
    }
    std::ranges::sort(facts.overrides, { }, &ScriptSourceFacts::Override::line);

    static const std::regex lvar(R"(#define\s+(LVAR_\w+)\s+\(?\s*(\d+)\s*\)?)");
    for (std::sregex_iterator it(code.begin(), code.end(), lvar), last; it != last; ++it) {
        facts.localVarNames.try_emplace(std::stoi((*it)[2].str()), (*it)[1].str());
    }
    return facts;
}

} // namespace geck::resource
