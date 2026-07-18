#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace geck::ssl {

enum class DiagnosticSeverity {
    Warning,
    Error,
};

/// One diagnostic line from the sslc compiler's output.
struct Diagnostic {
    DiagnosticSeverity severity = DiagnosticSeverity::Error;
    std::string file;    // source file as printed by the compiler ("" when the line carried none)
    int line = 0;        // 1-based source line, 0 when absent
    int column = 0;      // 1-based source column, 0 when absent
    std::string message; // the text after the location prefix
};

/// Parse sslc's captured output into structured diagnostics.
///
/// sslc (sfall edition) prints every diagnostic to stdout as
///   [Error] <file.ssl>:12:5: message
///   [Warning] <file.ssl>:12:5: message
/// (parseError/parseWarning in its parse.c — the file name sits in literal angle brackets).
/// Lines without the [Error]/[Warning] prefix (logo, "Compiling …", the
/// "*** THERE WERE ERRORS … ***" summary) are not diagnostics and are skipped. A prefixed
/// line whose location part doesn't match the expected shape is still kept, with the whole
/// remainder as the message, so a format drift upstream degrades to less detail rather than
/// a silently dropped error.
std::vector<Diagnostic> parseSslcOutput(std::string_view output);

/// Number of parsed diagnostics with the given severity.
std::size_t countDiagnostics(const std::vector<Diagnostic>& diagnostics, DiagnosticSeverity severity);

} // namespace geck::ssl
