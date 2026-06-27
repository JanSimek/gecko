#include "cli/FrmInspect.h"

#include "format/frm/Direction.h"
#include "format/frm/Frame.h"
#include "format/frm/Frm.h"
#include "format/lst/Lst.h"
#include "format/pal/Pal.h"
#include "resource/FrmResolver.h"
#include "resource/GameResources.h"
#include "resource/ResourcePaths.h"
#include "util/Constants.h"

#include <SFML/Graphics/Image.hpp>
#include <SFML/Graphics/RenderTarget.hpp>
#include <SFML/Graphics/RenderTexture.hpp>
#include <SFML/Graphics/Sprite.hpp>
#include <SFML/Graphics/Texture.hpp>
#include <SFML/Graphics/VertexArray.hpp>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <exception>
#include <memory>
#include <ostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace geck::cli {

using nlohmann::json;

namespace {

    // The art LSTs, indexed in FRM_TYPE enum order (so the index is the FRM type ordinal).
    struct ArtList {
        std::string_view dir;
        std::string_view lst;
    };
    constexpr std::array<ArtList, 8> kArtLists = { {
        { ResourcePaths::Directories::ITEMS, ResourcePaths::Lst::ITEMS },
        { ResourcePaths::Directories::CRITTERS, ResourcePaths::Lst::CRITTERS },
        { ResourcePaths::Directories::SCENERY, ResourcePaths::Lst::SCENERY },
        { ResourcePaths::Directories::WALLS, ResourcePaths::Lst::WALLS },
        { ResourcePaths::Directories::TILES, ResourcePaths::Lst::TILES },
        { ResourcePaths::Directories::MISC, ResourcePaths::Lst::MISC },
        { ResourcePaths::Directories::INTERFACE, ResourcePaths::Lst::INTERFACE },
        { ResourcePaths::Directories::INVENTORY, ResourcePaths::Lst::INVENTORY },
    } };

    std::string toLower(std::string s) {
        std::transform(s.begin(), s.end(), s.begin(),
            [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        return s;
    }

    const char* frmTypeName(Frm::FRM_TYPE type) {
        switch (type) {
            case Frm::FRM_TYPE::ITEM:
                return "item";
            case Frm::FRM_TYPE::CRITTER:
                return "critter";
            case Frm::FRM_TYPE::SCENERY:
                return "scenery";
            case Frm::FRM_TYPE::WALL:
                return "wall";
            case Frm::FRM_TYPE::TILE:
                return "tile";
            case Frm::FRM_TYPE::MISC:
                return "misc";
            case Frm::FRM_TYPE::INTERFACE:
                return "interface";
            case Frm::FRM_TYPE::INVENTORY:
                return "inventory";
            default:
                return "unknown";
        }
    }

    // The bare entry name an LST line carries, without the FRM extension or any trailing ",meta"
    // (scenery/critter lines append fields after a comma). Lowercased, like the LST itself.
    std::string lstEntryStem(const std::string& entry) {
        std::string name = entry;
        if (const auto comma = name.find(','); comma != std::string::npos) {
            name.erase(comma);
        }
        if (const auto dot = name.find_last_of('.'); dot != std::string::npos) {
            name.erase(dot);
        }
        // trim surrounding whitespace the reader may have left around a comma-split field
        const auto notSpace = [](unsigned char c) { return std::isspace(c) == 0; };
        name.erase(name.begin(), std::find_if(name.begin(), name.end(), notSpace));
        name.erase(std::find_if(name.rbegin(), name.rend(), notSpace).base(), name.end());
        return toLower(name);
    }

    // A minimal shell-style glob: '*' matches any run (including empty), '?' one character. Both
    // pattern and text are compared as-is (callers lowercase first). Iterative with backtracking so
    // it stays O(n*m) worst case and keeps the nesting shallow.
    bool globMatch(std::string_view pattern, std::string_view text) {
        std::size_t p = 0;
        std::size_t t = 0;
        std::size_t star = std::string_view::npos;
        std::size_t mark = 0;
        while (t < text.size()) {
            if (p < pattern.size() && (pattern[p] == '?' || pattern[p] == text[t])) {
                ++p;
                ++t;
            } else if (p < pattern.size() && pattern[p] == '*') {
                star = p++;
                mark = t;
            } else if (star != std::string_view::npos) {
                p = star + 1;
                t = ++mark;
            } else {
                return false;
            }
        }
        while (p < pattern.size() && pattern[p] == '*') {
            ++p;
        }
        return p == pattern.size();
    }

    // --- FRM stitched-texture geometry ------------------------------------------------------------
    // imageFromFrm lays frames out in a grid: row = direction (in FRM order), column = frame index,
    // each cell sized to the FRM's max frame box. These mirror that layout so we can slice one frame.
    struct CellRect {
        int x = 0;
        int y = 0;
        int w = 0;
        int h = 0;
    };
    CellRect frameCell(const Frm& frm, std::size_t dir, std::size_t frameIdx, const Frame& frame) {
        const int maxW = frm.maxFrameWidth();
        const int maxH = frm.maxFrameHeight();
        return { static_cast<int>(frameIdx) * maxW, static_cast<int>(dir) * maxH, frame.width(), frame.height() };
    }

    // Load the parsed FRM + the shared colour palette, or throw with a clear message.
    struct LoadedFrm {
        Frm* frm = nullptr;
        Pal* pal = nullptr;
    };
    LoadedFrm loadFrm(resource::GameResources& resources, const std::string& artPath) {
        LoadedFrm loaded;
        loaded.frm = resources.repository().load<Frm>(artPath);
        if (loaded.frm == nullptr) {
            throw std::runtime_error("could not read or parse FRM: " + artPath);
        }
        loaded.pal = resources.repository().load<Pal>(std::string(ResourcePaths::Pal::COLOR));
        if (loaded.pal == nullptr) {
            throw std::runtime_error("could not load the colour palette (color.pal)");
        }
        return loaded;
    }

    // --- frm info ---------------------------------------------------------------------------------
    json frameInfoArray(const Frm& frm) {
        auto frames = json::array();
        const auto& directions = frm.directions();
        for (std::size_t d = 0; d < directions.size(); ++d) {
            const auto& dirFrames = directions[d].frames();
            for (std::size_t f = 0; f < dirFrames.size(); ++f) {
                const Frame& frame = dirFrames[f];
                frames.push_back({ { "direction", d }, { "frame", f }, { "width", frame.width() },
                    { "height", frame.height() }, { "offsetX", frame.offsetX() }, { "offsetY", frame.offsetY() } });
            }
        }
        return frames;
    }

    // --- frm render -------------------------------------------------------------------------------
    // Background checkerboard so transparent/edge pixels of the sprite are visible against it.
    void appendCheckerboard(sf::VertexArray& quads, float x, float y, float w, float h) {
        constexpr float tile = 8.0f;
        const sf::Color light(110, 110, 110);
        const sf::Color dark(80, 80, 80);
        for (float cy = 0; cy < h; cy += tile) {
            for (float cx = 0; cx < w; cx += tile) {
                const sf::Color c = (static_cast<int>(cx / tile) + static_cast<int>(cy / tile)) % 2 == 0 ? light : dark;
                const float x0 = x + cx;
                const float y0 = y + cy;
                const float x1 = std::min(x + cx + tile, x + w);
                const float y1 = std::min(y + cy + tile, y + h);
                for (const sf::Vector2f& pt : { sf::Vector2f{ x0, y0 }, sf::Vector2f{ x1, y0 }, sf::Vector2f{ x1, y1 } }) {
                    quads.append(sf::Vertex{ pt, c });
                }
                for (const sf::Vector2f& pt : { sf::Vector2f{ x0, y0 }, sf::Vector2f{ x1, y1 }, sf::Vector2f{ x0, y1 } }) {
                    quads.append(sf::Vertex{ pt, c });
                }
            }
        }
    }

    // Which directions/frames to draw, honouring the optional single-dir / single-frame filters.
    struct GridPlan {
        std::vector<std::size_t> dirs;   // direction indices, in row order
        std::vector<std::size_t> frames; // frame indices, in column order
    };
    GridPlan planGrid(const Frm& frm, int dirFilter, int frameFilter) {
        GridPlan plan;
        const std::size_t dirCount = frm.directions().size();
        for (std::size_t d = 0; d < dirCount; ++d) {
            if (dirFilter < 0 || static_cast<std::size_t>(dirFilter) == d) {
                plan.dirs.push_back(d);
            }
        }
        const std::size_t fpd = frm.framesPerDirection();
        for (std::size_t f = 0; f < fpd; ++f) {
            if (frameFilter < 0 || static_cast<std::size_t>(frameFilter) == f) {
                plan.frames.push_back(f);
            }
        }
        return plan;
    }

    // Draw one frame's sprite (a sub-rect of the stitched sheet) into its cell, on a checkerboard.
    void drawFrameCell(sf::RenderTarget& target, const sf::Texture& sheet, const Frm& frm,
        std::size_t dir, std::size_t frameIdx, float cellX, float cellY) {
        const int maxW = frm.maxFrameWidth();
        const int maxH = frm.maxFrameHeight();
        sf::VertexArray bg(sf::PrimitiveType::Triangles);
        appendCheckerboard(bg, cellX, cellY, static_cast<float>(maxW), static_cast<float>(maxH));
        target.draw(bg);

        const auto& dirFrames = frm.directions()[dir].frames();
        if (frameIdx >= dirFrames.size()) {
            return; // a direction may have fewer frames than framesPerDirection
        }
        const CellRect rect = frameCell(frm, dir, frameIdx, dirFrames[frameIdx]);
        sf::Sprite sprite(sheet);
        sprite.setTextureRect(sf::IntRect({ rect.x, rect.y }, { rect.w, rect.h }));
        sprite.setPosition({ cellX, cellY });
        target.draw(sprite);
    }

    struct GridImage {
        sf::Image image;
        std::size_t rows = 0;
        std::size_t cols = 0;
    };

    GridImage renderGrid(resource::GameResources& resources, const std::string& artPath,
        const Frm& frm, const GridPlan& plan) {
        const int maxW = frm.maxFrameWidth();
        const int maxH = frm.maxFrameHeight();
        constexpr int pad = 4;
        const unsigned int width = static_cast<unsigned int>((maxW + pad) * static_cast<int>(plan.frames.size()) + pad);
        const unsigned int height = static_cast<unsigned int>((maxH + pad) * static_cast<int>(plan.dirs.size()) + pad);

        auto target = std::make_unique<sf::RenderTexture>();
        if (!target->resize({ width, height })) {
            throw std::runtime_error("could not create a " + std::to_string(width) + "x" + std::to_string(height)
                + " off-screen render target — no GL context (headless without a display?)");
        }
        target->clear(sf::Color(40, 40, 40));

        // The stitched sheet (every direction x frame) — slice per-frame sub-rects out of it. Keyed by
        // the full art path so the TextureManager finds the same parsed FRM and palette the editor uses.
        const sf::Texture& sheet = resources.textures().get(artPath);

        for (std::size_t row = 0; row < plan.dirs.size(); ++row) {
            for (std::size_t col = 0; col < plan.frames.size(); ++col) {
                const float cellX = static_cast<float>(pad + static_cast<int>(col) * (maxW + pad));
                const float cellY = static_cast<float>(pad + static_cast<int>(row) * (maxH + pad));
                drawFrameCell(*target, sheet, frm, plan.dirs[row], plan.frames[col], cellX, cellY);
            }
        }
        target->display();
        return { target->getTexture().copyToImage(), plan.dirs.size(), plan.frames.size() };
    }

} // namespace

std::optional<uint32_t> parseFid(const std::string& token) {
    // A FID is unsigned. Reject a leading sign up front: std::stoul would otherwise silently wrap a
    // negative into a huge value instead of failing, turning "-1" into a bogus FID.
    if (token.empty() || token.front() == '-' || token.front() == '+') {
        return std::nullopt;
    }
    try {
        std::size_t consumed = 0;
        const unsigned long value = std::stoul(token, &consumed, 0); // base 0: 0x.. hex, else decimal
        if (consumed != token.size()) {
            return std::nullopt; // trailing non-numeric chars -> not a pure FID
        }
        return static_cast<uint32_t>(value);
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

std::string normalizeArtToken(const std::string& token) {
    std::string normalized = token;
    if (!normalized.empty() && normalized.front() == '/') {
        normalized.erase(0, 1);
    }
    std::replace(normalized.begin(), normalized.end(), '\\', '/');
    normalized = toLower(normalized);
    if (!resource::hasFrmExtension(normalized)) {
        normalized += ".frm";
    }
    return normalized;
}

std::optional<std::string> resolveFrmTarget(
    resource::GameResources& resources, const std::string& target, std::string& error) {
    // A pure number is a FID; resolve it via the engine LST lookup.
    if (const auto fid = parseFid(target); fid.has_value()) {
        try {
            return resources.frmResolver().resolve(*fid);
        } catch (const std::exception& e) {
            error = std::string("could not resolve FID ") + target + ": " + e.what();
            return std::nullopt;
        }
    }

    const std::string normalized = normalizeArtToken(target);
    // A path already under an art directory is used directly.
    if (normalized.find('/') != std::string::npos) {
        return normalized;
    }

    // A bare name: search every art LST for a matching entry and rebuild its full art path.
    const std::string stem = lstEntryStem(normalized);
    for (const auto& art : kArtLists) {
        auto* lst = resources.repository().find<Lst>(std::string(art.lst));
        if (lst == nullptr) {
            try {
                lst = resources.repository().load<Lst>(std::string(art.lst));
            } catch (const std::exception&) {
                continue; // this art type's LST is absent in the mounted data
            }
        }
        if (lst == nullptr) {
            continue;
        }
        for (const auto& entry : lst->list()) {
            if (lstEntryStem(entry) == stem) {
                return std::string(art.dir) + stem + ".frm";
            }
        }
    }
    error = "could not resolve art name '" + target + "' in any art .lst (try art/<dir>/<name>.frm or a FID)";
    return std::nullopt;
}

int frmInfo(resource::GameResources& resources, const std::string& target, std::ostream& out) {
    std::string error;
    const auto artPath = resolveFrmTarget(resources, target, error);
    if (!artPath.has_value()) {
        out << "frm info: " << error << "\n";
        return 1;
    }

    Frm* frm = nullptr;
    try {
        frm = resources.repository().load<Frm>(*artPath);
    } catch (const std::exception& e) {
        out << "frm info: could not read FRM " << *artPath << ": " << e.what() << "\n";
        return 1;
    }
    if (frm == nullptr) {
        out << "frm info: could not read FRM " << *artPath << "\n";
        return 1;
    }

    json info;
    info["resolvedArtPath"] = *artPath;
    if (const auto fid = resources.frmResolver().resolveFid(*artPath); fid.has_value()) {
        info["fid"] = *fid;
    } else {
        info["fid"] = nullptr; // critters are animation-encoded, so the inverse isn't always available
    }
    info["directionCount"] = frm->directions().size();
    info["framesPerDirection"] = frm->framesPerDirection();
    info["frames"] = frameInfoArray(*frm);
    out << info.dump() << "\n";
    return 0;
}

int frmRender(resource::GameResources& resources, const FrmRenderOptions& options, std::ostream& out) {
    std::string error;
    const auto artPath = resolveFrmTarget(resources, options.target, error);
    if (!artPath.has_value()) {
        out << "frm render: " << error << "\n";
        return 1;
    }

    try {
        LoadedFrm loaded = loadFrm(resources, *artPath);
        const GridPlan plan = planGrid(*loaded.frm, options.direction, options.frame);
        if (plan.dirs.empty() || plan.frames.empty()) {
            out << "frm render: nothing to draw (direction/frame filter out of range)\n";
            return 1;
        }
        const GridImage grid = renderGrid(resources, *artPath, *loaded.frm, plan);
        if (!grid.image.saveToFile(options.outPath)) {
            out << "frm render: failed to write image: " << options.outPath << "\n";
            return 1;
        }
        const sf::Vector2u size = grid.image.getSize();
        out << "wrote " << options.outPath << " (" << size.x << "x" << size.y << "), grid "
            << grid.rows << "x" << grid.cols << " (directions x frames), from " << *artPath << "\n";
    } catch (const std::exception& e) {
        out << "frm render: " << e.what() << "\n";
        return 1;
    }
    return 0;
}

int resolveFidCommand(resource::GameResources& resources, const std::string& fidToken, std::ostream& out) {
    const auto fid = parseFid(fidToken);
    if (!fid.has_value()) {
        out << "frm resolve: '" << fidToken << "' is not a valid FID (use 0x.. hex or decimal)\n";
        return 1;
    }

    // Decode the type byte (engine FID_TYPE = (fid & 0x0F000000) >> 24) and the base index.
    const auto type = static_cast<Frm::FRM_TYPE>((*fid & FileFormat::TYPE_MASK) >> FileFormat::TYPE_MASK_SHIFT);
    const uint32_t index = (type == Frm::FRM_TYPE::CRITTER)
        ? (*fid & FileFormat::CRITTER_ID_MASK)
        : (*fid & FileFormat::BASE_ID_MASK);

    json info;
    info["fid"] = *fid;
    info["type"] = frmTypeName(type);
    info["index"] = index;
    try {
        info["artPath"] = resources.frmResolver().resolve(*fid);
    } catch (const std::exception& e) {
        info["artPath"] = nullptr;
        info["error"] = e.what();
    }
    out << info.dump() << "\n";
    return 0;
}

int listFrms(resource::GameResources& resources, const std::string& glob, std::ostream& out) {
    const std::string pattern = toLower(glob);
    auto entries = json::array();

    for (const auto& art : kArtLists) {
        Lst* lst = nullptr;
        try {
            lst = resources.repository().load<Lst>(std::string(art.lst));
        } catch (const std::exception&) {
            continue; // this art type's LST is absent in the mounted data
        }
        if (lst == nullptr) {
            continue;
        }
        for (const auto& entry : lst->list()) {
            const std::string stem = lstEntryStem(entry);
            if (stem.empty() || !globMatch(pattern, stem)) {
                continue;
            }
            const std::string artPath = std::string(art.dir) + stem + ".frm";
            json item{ { "name", stem }, { "artPath", artPath } };
            if (const auto fid = resources.frmResolver().resolveFid(artPath); fid.has_value()) {
                item["fid"] = *fid;
            } else {
                item["fid"] = nullptr;
            }
            entries.push_back(std::move(item));
        }
    }

    out << entries.dump() << "\n";
    return 0;
}

} // namespace geck::cli
