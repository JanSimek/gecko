#include "TextureManager.h"

#include "ResourceRepository.h"

#include "../format/frm/Direction.h"
#include "../format/frm/Frame.h"
#include "../format/frm/Frm.h"
#include "../format/pal/Pal.h"
#include "../util/Exceptions.h"
#include "../util/ResourcePaths.h"

#include <algorithm>
#include <cctype>

namespace geck::resource {

TextureManager::TextureManager(ResourceRepository& repository)
    : _repository(repository) {
}

void TextureManager::clear() {
    _textures.clear();
}

bool TextureManager::has(std::string_view key) const {
    return _textures.contains(std::string(key));
}

void TextureManager::store(std::string_view key, std::unique_ptr<sf::Texture> texture) {
    _textures.try_emplace(std::string(key), std::move(texture));
}

void TextureManager::preload(const std::filesystem::path& path) {
    const std::string key = path.generic_string();
    if (_textures.contains(key)) {
        return;
    }

    std::string extension = path.extension().string();
    std::transform(extension.begin(), extension.end(), extension.begin(), [](unsigned char value) {
        return static_cast<char>(std::tolower(value));
    });

    if (extension.rfind(".frm", 0) == 0) {
        [[maybe_unused]] auto* frm = _repository.load<Frm>(path);
        return;
    }

    auto texture = std::make_unique<sf::Texture>();
    if (!texture->loadFromFile(key)) {
        throw ResourceException("Failed to load texture with extension: " + extension, key);
    }

    _textures.try_emplace(key, std::move(texture));
}

const sf::Texture& TextureManager::get(const std::filesystem::path& path) {
    const std::string key = path.generic_string();
    auto iter = _textures.find(key);
    if (iter != _textures.end()) {
        return *iter->second;
    }

    Frm* frm = _repository.find<Frm>(path);
    if (!frm) {
        preload(path);
        frm = _repository.find<Frm>(path);
        if (!frm) {
            auto textureIter = _textures.find(key);
            if (textureIter != _textures.end()) {
                return *textureIter->second;
            }
            throw ResourceException("Texture does not exist", key);
        }
    }

    Pal* palette = _repository.load<Pal>(ResourcePaths::Pal::COLOR);
    if (!palette) {
        throw ResourceException("Failed to load color palette for texture", key);
    }

    auto texture = std::make_unique<sf::Texture>();
    const sf::Image image = imageFromFrm(*frm, *palette);
    if (!texture->loadFromImage(image)) {
        throw SpriteException("Failed to load texture from FRM image", key);
    }

    const auto [storedIter, inserted] = _textures.try_emplace(key, std::move(texture));
    if (!inserted) {
        throw ResourceException("Failed to cache texture", key);
    }

    return *storedIter->second;
}

sf::Image TextureManager::imageFromFrm(const Frm& frm, Pal& pal) const {
    const auto colors = pal.palette();
    const unsigned maxWidth = frm.maxFrameWidth();
    const unsigned maxHeight = frm.maxFrameHeight();

    sf::Image image;
    image.resize({ frm.width(), frm.height() }, { 0, 0, 0, 0 });

    int yOffset = 0;
    for (const auto& direction : frm.directions()) {
        int xOffset = 0;

        for (const Frame& frame : direction.frames()) {
            for (int x = 0; x < frame.width(); ++x) {
                for (int y = 0; y < frame.height(); ++y) {
                    const uint8_t paletteIndex = frame.index(static_cast<uint16_t>(x), static_cast<uint16_t>(y));
                    const geck::Rgb color = colors[paletteIndex];

                    constexpr uint8_t WHITE = 255;
                    constexpr uint8_t OPAQUE_ALPHA = 255;
                    constexpr int BRIGHTNESS = 4;

                    uint8_t r = 0;
                    uint8_t g = 0;
                    uint8_t b = 0;
                    uint8_t a = 0;

                    if (!(color.r == WHITE && color.g == WHITE && color.b == WHITE)) {
                        r = static_cast<uint8_t>(color.r * BRIGHTNESS);
                        g = static_cast<uint8_t>(color.g * BRIGHTNESS);
                        b = static_cast<uint8_t>(color.b * BRIGHTNESS);
                        a = OPAQUE_ALPHA;
                    }

                    image.setPixel({ maxWidth * xOffset + x, maxHeight * yOffset + y }, { r, g, b, a });
                }
            }
            ++xOffset;
        }
        ++yOffset;
    }

    return image;
}

} // namespace geck::resource
