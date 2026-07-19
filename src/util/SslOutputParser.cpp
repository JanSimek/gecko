#include "SslOutputParser.h"

#include <algorithm>
#include <cctype>

namespace geck::ssl {

namespace {

    std::string_view trimmed(std::string_view text) {
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
            text.remove_prefix(1);
        }
        while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
            text.remove_suffix(1);
        }
        return text;
    }

    // Consume a run of digits from the front of `text` into `value`; false when there is none.
    bool consumeNumber(std::string_view& text, int& value) {
        std::size_t digits = 0;
        while (digits < text.size() && std::isdigit(static_cast<unsigned char>(text[digits])) != 0) {
            ++digits;
        }
        if (digits == 0) {
            return false;
        }
        value = 0;
        for (std::size_t i = 0; i < digits; ++i) {
            value = value * 10 + (text[i] - '0');
        }
        text.remove_prefix(digits);
        return true;
    }

    // Consume ":line[:col]:" from the front of `rest` into the diagnostic; false (leaving
    // `rest` untouched) when it doesn't start with a line number.
    bool consumeLocationNumbers(std::string_view& rest, Diagnostic& diagnostic) {
        if (rest.empty() || rest.front() != ':') {
            return false;
        }
        std::string_view afterColon = rest.substr(1);
        int line = 0;
        if (!consumeNumber(afterColon, line)) {
            return false;
        }
        diagnostic.line = line;
        if (!afterColon.empty() && afterColon.front() == ':') {
            std::string_view afterSecond = afterColon.substr(1);
            int column = 0;
            if (consumeNumber(afterSecond, column)) {
                diagnostic.column = column;
                afterColon = afterSecond;
            }
        }
        if (!afterColon.empty() && afterColon.front() == ':') {
            afterColon.remove_prefix(1);
        }
        rest = afterColon;
        return true;
    }

    // Parse the "<file>:line:col: message" tail after the severity tag. The angle brackets and
    // the ":col" part are treated as optional so minor upstream format drift stays parseable.
    void parseLocationAndMessage(std::string_view tail, Diagnostic& diagnostic) {
        std::string_view rest = tail;

        if (!rest.empty() && rest.front() == '<') {
            if (const std::size_t close = rest.find('>'); close != std::string_view::npos) {
                diagnostic.file = std::string(rest.substr(1, close - 1));
                rest.remove_prefix(close + 1);
            }
        }

        consumeLocationNumbers(rest, diagnostic);

        diagnostic.message = std::string(trimmed(rest));
        if (diagnostic.message.empty() && diagnostic.file.empty()) {
            // Nothing matched the expected shape: keep the whole tail so the error isn't lost.
            diagnostic.message = std::string(trimmed(tail));
        }
    }

} // namespace

std::vector<Diagnostic> parseSslcOutput(std::string_view output) {
    constexpr std::string_view ERROR_TAG = "[Error] ";
    constexpr std::string_view WARNING_TAG = "[Warning] ";

    std::vector<Diagnostic> diagnostics;
    while (!output.empty()) {
        std::size_t end = output.find('\n');
        std::string_view line = output.substr(0, end);
        output.remove_prefix(end == std::string_view::npos ? output.size() : end + 1);

        line = trimmed(line);

        Diagnostic diagnostic;
        if (line.starts_with(ERROR_TAG)) {
            diagnostic.severity = DiagnosticSeverity::Error;
            line.remove_prefix(ERROR_TAG.size());
        } else if (line.starts_with(WARNING_TAG)) {
            diagnostic.severity = DiagnosticSeverity::Warning;
            line.remove_prefix(WARNING_TAG.size());
        } else {
            continue;
        }

        parseLocationAndMessage(line, diagnostic);
        diagnostics.push_back(std::move(diagnostic));
    }
    return diagnostics;
}

std::size_t countDiagnostics(const std::vector<Diagnostic>& diagnostics, DiagnosticSeverity severity) {
    return static_cast<std::size_t>(std::ranges::count_if(diagnostics,
        [severity](const Diagnostic& diagnostic) { return diagnostic.severity == severity; }));
}

} // namespace geck::ssl
