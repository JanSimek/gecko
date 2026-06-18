#pragma once

#include <SFML/Graphics.hpp>

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

namespace geck {
class Frm;
class Pal;
}

namespace geck::resource {

class ResourceRepository;
class DataFileSystem;

/**
 * @brief Owns the sf::Texture cache and turns parsed resources into textures.
 *
 * Threading contract:
 *  - preload() is parse-only and safe to call from background loaders: it just
 *    warms the ResourceRepository's parsed-resource cache and never touches the
 *    texture cache or the GL context.
 *  - get()/has()/store()/clear() touch the sf::Texture cache and create GPU
 *    textures, so they must run on the main (rendering) thread that owns the
 *    GL context. The texture cache is therefore not separately synchronized.
 */
class TextureManager final {
public:
    TextureManager(ResourceRepository& repository, DataFileSystem& files);

    void clear();
    [[nodiscard]] bool has(std::string_view key) const;
    void store(std::string_view key, std::unique_ptr<sf::Texture> texture);

    // Parse-only; background-thread safe. Never creates a texture.
    void preload(const std::filesystem::path& path);

    // Main-thread only: creates GPU textures on demand.
    [[nodiscard]] const sf::Texture& get(const std::filesystem::path& path);

private:
    [[nodiscard]] static bool isFrmPath(const std::filesystem::path& path);
    [[nodiscard]] const sf::Texture& createFrmTexture(const std::filesystem::path& path, const std::string& key);
    [[nodiscard]] const sf::Texture& createImageTexture(const std::filesystem::path& path, const std::string& key);
    [[nodiscard]] static sf::Image imageFromFrm(const Frm& frm, Pal& pal);

    ResourceRepository& _repository;
    DataFileSystem& _files;
    std::unordered_map<std::string, std::unique_ptr<sf::Texture>> _textures;
};

} // namespace geck::resource
