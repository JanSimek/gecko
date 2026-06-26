#pragma once

#include "format/IFile.h"

#include <cstddef>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

/// @file
/// @brief The single, lossless, round-trippable model of a `.gam` file.
///
/// A `.gam` lists the engine's GAME_GLOBAL_VARS (the GVAR_* progression dictionary, from
/// data/vault13.gam) and a map's MAP_GLOBAL_VARS. @ref Gam is the one data class for both uses:
///
///  - **Lookup / analysis** (the CLI tools): name/value lists for gvars and mvars, by index or key — see
///    @ref gameGlobalVars / @ref gvarValue / @ref mapGlobalVars.
///  - **Lossless editing** (the Map Info panel): for a BASE map the engine re-reads the map's globals
///    from `MAP_GLOBAL_VARS:` (fallout2-ce map.cc), so the editor must edit a value here and write the
///    file back without disturbing anything else.
///
/// To stay lossless it keeps the *whole* file in line order (@ref lines), every line retaining its **raw
/// content** (sans EOL) as the canonical form, so an unchanged line serialises byte-for-byte. Editing a
/// `MAP_GLOBAL_VARS:` value rebuilds that one line from its parsed parts — `valuePrefix + value +
/// valueSuffix` — exactly as @ref geck::writer::setField rebuilds a maps.txt line from
/// `key + "=" + value + inlineComment`. This preserves the variable name, the `:=` spacing/alignment, the
/// closing `;`, and any trailing `// comment`, with no positional offset that could drift across repeated
/// edits.
///
/// Build it with @ref geck::GamReader::parse (or @ref geck::GamReader through the resource cache) and
/// write it with @ref geck::writer::serializeGam. Line endings are not tracked per line: the reader
/// strips a trailing `\r` and the serialiser writes `\n` (the engine writes LF), so a round trip is
/// data-identical and LF-normalised. `finalNewline` records whether the file ended with a newline.

namespace geck {

struct GamLine {
    enum class Kind {
        Blank,         ///< empty or whitespace-only line
        Comment,       ///< a `//`-prefixed line
        SectionHeader, ///< `GAME_GLOBAL_VARS:` or `MAP_GLOBAL_VARS:`
        Variable,      ///< `NAME := value;` (with an optional trailing `// comment`)
        Other,         ///< anything else — kept verbatim via `raw`
    };

    enum class Section {
        None, ///< before any section header
        GameGlobalVars,
        MapGlobalVars,
    };

    Kind kind = Kind::Blank;
    Section section = Section::None;
    std::string raw; ///< the line content without its EOL — canonical for serialisation

    // Variable lines only — the line split into structured parts so an edit rebuilds it (instead of a
    // fragile positional substring replace). `valuePrefix + std::to_string(value) + valueSuffix == raw`.
    std::string name;        ///< the variable name (text before `:=`)
    int value = 0;           ///< the parsed integer value
    std::string valuePrefix; ///< everything in `raw` up to the integer (leading ws + name + `:=` + ws)
    std::string valueSuffix; ///< everything from after the integer to EOL (`;`, trailing `// comment`, ws)
};

/// The whole `.gam`, in file order. Derives from @ref IFile so it can live in the resource cache.
class Gam : public IFile {
public:
    Gam()
        : IFile(std::filesystem::path{}) { }
    explicit Gam(const std::filesystem::path& path)
        : IFile(path) { }

    std::vector<GamLine> lines;
    bool finalNewline = true; ///< did the file end with a newline?

    /// The GAME_GLOBAL_VARS variables, in file order — the order gvar ordinals index by.
    std::vector<std::pair<std::string, int>> gameGlobalVars() const {
        return varsInSection(GamLine::Section::GameGlobalVars);
    }

    /// The MAP_GLOBAL_VARS variables, in file order — the order map_global_vars is indexed by.
    std::vector<std::pair<std::string, int>> mapGlobalVars() const {
        return varsInSection(GamLine::Section::MapGlobalVars);
    }

    // --- Lookup helpers (used by the CLI gvar dictionary / analysis tools). ---

    std::size_t gvarCount() const { return countInSection(GamLine::Section::GameGlobalVars); }
    std::size_t mvarCount() const { return countInSection(GamLine::Section::MapGlobalVars); }

    const std::string& gvarKey(std::size_t index) const {
        return keyAt(GamLine::Section::GameGlobalVars, index);
    }
    const std::string& mvarKey(std::size_t index) const {
        return keyAt(GamLine::Section::MapGlobalVars, index);
    }

    int gvarValue(std::size_t index) const { return valueAt(GamLine::Section::GameGlobalVars, index); }
    int mvarValue(std::size_t index) const { return valueAt(GamLine::Section::MapGlobalVars, index); }

    int gvarValue(const std::string& key) const { return valueOf(GamLine::Section::GameGlobalVars, key); }
    int mvarValue(const std::string& key) const { return valueOf(GamLine::Section::MapGlobalVars, key); }

    /// Rewrite the `index`-th MAP_GLOBAL_VARS variable's value by rebuilding its line from the parsed
    /// parts (`valuePrefix + value + valueSuffix`), so the name, `:=` spacing, the `;`, and any trailing
    /// comment are preserved. Only the edited line's `raw` is touched; every other line stays canonical.
    /// Returns false if `index` is out of range.
    bool setMapGlobalVar(std::size_t index, int value) {
        std::size_t seen = 0;
        for (GamLine& line : lines) {
            if (!isVar(line, GamLine::Section::MapGlobalVars)) {
                continue;
            }
            if (seen == index) {
                line.value = value;
                line.raw = line.valuePrefix + std::to_string(value) + line.valueSuffix;
                return true;
            }
            ++seen;
        }
        return false;
    }

    /// Append a new MAP_GLOBAL_VARS variable. Compiled scripts reference map globals by positional index
    /// (the MAP_GLOBAL_VARS order), so a new variable is always added as the positionally-last map global
    /// — inserted in @ref lines immediately after the last existing MapGlobalVars Variable line, or (when
    /// there are none yet) immediately after the `MAP_GLOBAL_VARS:` section header. This keeps every
    /// existing variable's index stable. The line is built canonically as `name := value;` so it matches
    /// the engine's variable shape and round-trips through the serialiser/reader. Returns false (adding
    /// nothing) if the document has no `MAP_GLOBAL_VARS:` section at all.
    bool addMapGlobalVar(const std::string& name, int value) {
        std::size_t insertAfter = 0;
        bool foundAnchor = false;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            const GamLine& line = lines[i];
            const bool isHeader = line.kind == GamLine::Kind::SectionHeader
                && line.section == GamLine::Section::MapGlobalVars;
            if (isHeader || isVar(line, GamLine::Section::MapGlobalVars)) {
                insertAfter = i;
                foundAnchor = true;
            }
        }
        if (!foundAnchor) {
            return false; // no MAP_GLOBAL_VARS section to append into
        }

        GamLine added;
        added.kind = GamLine::Kind::Variable;
        added.section = GamLine::Section::MapGlobalVars;
        added.name = name;
        added.value = value;
        added.valuePrefix = name + " := ";
        added.valueSuffix = ";";
        added.raw = added.valuePrefix + std::to_string(value) + added.valueSuffix;

        lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(insertAfter + 1), std::move(added));
        return true;
    }

    /// Erase the `index`-th MAP_GLOBAL_VARS variable line. Removing a variable shifts every later
    /// variable's positional index down by one, so a caller that exposes this must warn that compiled
    /// scripts referencing those variables can break. Returns false if `index` is out of range.
    bool removeMapGlobalVar(std::size_t index) {
        std::size_t seen = 0;
        for (std::size_t i = 0; i < lines.size(); ++i) {
            if (!isVar(lines[i], GamLine::Section::MapGlobalVars)) {
                continue;
            }
            if (seen == index) {
                lines.erase(lines.begin() + static_cast<std::ptrdiff_t>(i));
                return true;
            }
            ++seen;
        }
        return false;
    }

private:
    static bool isVar(const GamLine& line, GamLine::Section section) {
        return line.kind == GamLine::Kind::Variable && line.section == section;
    }

    std::vector<std::pair<std::string, int>> varsInSection(GamLine::Section section) const {
        std::vector<std::pair<std::string, int>> out;
        for (const GamLine& line : lines) {
            if (isVar(line, section)) {
                out.emplace_back(line.name, line.value);
            }
        }
        return out;
    }

    std::size_t countInSection(GamLine::Section section) const {
        std::size_t n = 0;
        for (const GamLine& line : lines) {
            if (isVar(line, section)) {
                ++n;
            }
        }
        return n;
    }

    const GamLine* lineAt(GamLine::Section section, std::size_t index) const {
        std::size_t seen = 0;
        for (const GamLine& line : lines) {
            if (isVar(line, section)) {
                if (seen == index) {
                    return &line;
                }
                ++seen;
            }
        }
        return nullptr;
    }

    const std::string& keyAt(GamLine::Section section, std::size_t index) const {
        const GamLine* line = lineAt(section, index);
        if (line == nullptr) {
            throw std::out_of_range("gam variable index out of range");
        }
        return line->name;
    }

    int valueAt(GamLine::Section section, std::size_t index) const {
        const GamLine* line = lineAt(section, index);
        if (line == nullptr) {
            throw std::out_of_range("gam variable index out of range");
        }
        return line->value;
    }

    int valueOf(GamLine::Section section, const std::string& key) const {
        for (const GamLine& line : lines) {
            if (isVar(line, section) && line.name == key) {
                return line.value;
            }
        }
        throw std::runtime_error("gam variable '" + key + "' not found");
    }
};

} // namespace geck
