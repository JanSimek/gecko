#include "writer/maps/MapRegistryWriter.h"

#include "reader/TextParsing.h"

#include <cstddef>
#include <fstream>
#include <iterator>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace geck::writer {

namespace {

    using geck::text::toLower;
    using geck::text::trim;

    std::string readBytes(const std::filesystem::path& path) {
        std::ifstream in(path, std::ios::binary);
        if (!in) {
            throw std::runtime_error("MapRegistryWriter: cannot open " + path.string());
        }
        return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    }

    void writeBytes(const std::filesystem::path& path, const std::string& bytes) {
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (!out) {
            throw std::runtime_error("MapRegistryWriter: cannot write " + path.string());
        }
        out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    }

    // Split into lines, each segment KEEPING its trailing "\r\n"/"\n"; a final newline-less segment is
    // kept as-is. Rejoining the vector reproduces the input byte-for-byte.
    std::vector<std::string> splitKeepEol(const std::string& content) {
        std::vector<std::string> lines;
        std::size_t start = 0;
        while (start < content.size()) {
            const std::size_t nl = content.find('\n', start);
            if (nl == std::string::npos) {
                lines.push_back(content.substr(start));
                break;
            }
            lines.push_back(content.substr(start, nl - start + 1));
            start = nl + 1;
        }
        return lines;
    }

    std::string eolOf(const std::string& line) {
        if (line.size() >= 2 && line[line.size() - 2] == '\r' && line.back() == '\n') {
            return "\r\n";
        }
        if (!line.empty() && line.back() == '\n') {
            return "\n";
        }
        return "";
    }

    std::string bodyOf(const std::string& line) {
        return line.substr(0, line.size() - eolOf(line).size());
    }

    std::string detectEol(const std::vector<std::string>& lines) {
        for (const auto& line : lines) {
            const std::string eol = eolOf(line);
            if (!eol.empty()) {
                return eol;
            }
        }
        return "\n";
    }

    std::string join(const std::vector<std::string>& lines) {
        std::string out;
        for (const auto& line : lines) {
            out += line;
        }
        return out;
    }

    // "[Map NNN]" inner text -> NNN, or -1; mirrors MapsTxtReader::parseSectionIndex.
    int sectionIndex(const std::string& sectionInner) {
        if (toLower(sectionInner).rfind("map", 0) != 0) {
            return -1;
        }
        try {
            return std::stoi(sectionInner.substr(3));
        } catch (const std::exception&) {
            return -1;
        }
    }

    // If `line` is the `{targetId}{audio}{text}` message, return it rebuilt with `newText` (audio kept,
    // line ending preserved); otherwise nullopt. Brace-matched so a text containing '}{' round-trips.
    std::optional<std::string> rebuildMessageLine(const std::string& line, int targetId, const std::string& newText) {
        const std::string body = bodyOf(line);
        const std::size_t open0 = body.find('{');
        if (open0 == std::string::npos) {
            return std::nullopt; // comment / blank line
        }
        const std::size_t close0 = body.find('}', open0);
        if (close0 == std::string::npos) {
            return std::nullopt;
        }
        int lineId = -1;
        try {
            lineId = std::stoi(body.substr(open0 + 1, close0 - open0 - 1));
        } catch (const std::exception&) {
            return std::nullopt;
        }
        if (lineId != targetId) {
            return std::nullopt;
        }
        const std::size_t open1 = body.find('{', close0);
        const std::size_t close1 = open1 == std::string::npos ? std::string::npos : body.find('}', open1);
        if (close1 == std::string::npos) {
            return std::nullopt;
        }
        const std::string audio = body.substr(open1 + 1, close1 - open1 - 1);
        return body.substr(0, open0) + "{" + std::to_string(targetId) + "}{" + audio + "}{" + newText + "}" + eolOf(line);
    }

} // namespace

bool updateLookupName(const std::filesystem::path& mapsTxtPath, int mapIndex, const std::string& newName) {
    if (mapIndex < 0) {
        return false;
    }
    std::vector<std::string> lines = splitKeepEol(readBytes(mapsTxtPath));

    int section = -1;
    for (std::string& line : lines) {
        const std::string body = bodyOf(line);
        const std::string trimmed = trim(body);
        if (!trimmed.empty() && trimmed.front() == '[' && trimmed.back() == ']') {
            section = sectionIndex(trim(trimmed.substr(1, trimmed.size() - 2)));
            continue;
        }
        if (section != mapIndex) {
            continue;
        }
        const std::size_t eq = body.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        if (toLower(trim(body.substr(0, eq))) == "lookup_name") {
            line = body.substr(0, eq + 1) + newName + eolOf(line); // key + '=' kept, value replaced
            writeBytes(mapsTxtPath, join(lines));
            return true;
        }
    }
    return false; // no matching [Map mapIndex] section, or it has no lookup_name
}

bool updateDisplayName(const std::filesystem::path& mapMsgPath, int mapIndex, int elevation, const std::string& newText) {
    if (mapIndex < 0 || elevation < 0 || elevation > 2) {
        return false;
    }
    const int id = mapIndex * 3 + elevation + 200;
    std::vector<std::string> lines = splitKeepEol(readBytes(mapMsgPath));

    for (std::string& line : lines) {
        if (const auto rebuilt = rebuildMessageLine(line, id, newText)) {
            line = *rebuilt;
            writeBytes(mapMsgPath, join(lines));
            return true;
        }
    }

    // Id not present: append a fresh message, keeping the file's line-ending style.
    const std::string eol = detectEol(lines);
    std::string content = join(lines);
    if (!content.empty() && content.back() != '\n') {
        content += eol;
    }
    content += "{" + std::to_string(id) + "}{}{" + newText + "}" + eol;
    writeBytes(mapMsgPath, content);
    return true;
}

} // namespace geck::writer
