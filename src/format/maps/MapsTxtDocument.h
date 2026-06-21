#pragma once

#include <string>
#include <vector>

/// @file
/// @brief Lossless, round-trippable document model for Fallout 2's `data/maps.txt`.
///
/// Unlike @ref MapsTxt (a lossy, query-oriented view that keeps only the modelled fields), this model
/// preserves the *whole* file so it can be edited and written back without losing anything: the
/// preamble, every `[Map NNN]` section in order, and within each section every line — blank lines,
/// `;` comments, commented-out keys, repeated keys (`random_start_point_N`), multi-value keys
/// (`ambient_sfx`, `can_rest_here`), and keys the typed reader never modelled.
///
/// Each line keeps its **raw content** (sans line ending) as the canonical form, so an unchanged line
/// serialises byte-for-byte; editing a field rebuilds only that one line. Line endings are *not*
/// tracked per line: the reader strips a trailing `\r` and the serialiser writes `\n` (the engine
/// writes LF itself and tolerates both — see fallout2-ce config.cc), so a round trip is data-identical
/// and LF-normalised. `finalNewline` records whether the file ended with a newline.

namespace geck {

struct MapsTxtLine {
    enum class Kind {
        Blank,    ///< empty or whitespace-only line
        Comment,  ///< a `;`-prefixed line (or a commented-out `;key=value`)
        KeyValue, ///< `key=value` with an optional trailing `; ...` inline comment
    };

    Kind kind = Kind::Blank;
    std::string raw;           ///< the line content without its EOL — canonical for serialisation
    std::string key;           ///< KeyValue: the text before `=` (original case/spacing)
    std::string value;         ///< KeyValue: the value, trimmed, without the inline comment
    std::string inlineComment; ///< KeyValue: the `  ; ...` suffix incl. its leading whitespace, or ""
};

struct MapsTxtSection {
    int index = -1;        ///< the NNN of `[Map NNN]`; -1 for a non-Map section
    std::string headerRaw; ///< the `[Map NNN]` line exactly as read (preserves zero-padding/spacing)
    std::vector<MapsTxtLine> lines;
};

/// The whole `data/maps.txt`, in file order.
struct MapsTxtDocument {
    std::vector<MapsTxtLine> preamble; ///< lines before the first `[...]` section
    std::vector<MapsTxtSection> sections;
    bool finalNewline = true; ///< did the file end with a newline?

    const MapsTxtSection* section(int index) const {
        for (const auto& section : sections) {
            if (section.index == index) {
                return &section;
            }
        }
        return nullptr;
    }

    MapsTxtSection* section(int index) {
        for (auto& section : sections) {
            if (section.index == index) {
                return &section;
            }
        }
        return nullptr;
    }
};

} // namespace geck
