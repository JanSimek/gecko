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

class TextureManager final {
public:
    explicit TextureManager(ResourceRepository& repository);

    void clear();
    [[nodiscard]] bool has(std::string_view key) const;
    void store(std::string_view key, std::unique_ptr<sf::Texture> texture);
    void preload(const std::filesystem::path& path);
    [[nodiscard]] const sf::Texture& get(const std::filesystem::path& path);

private:
    [[nodiscard]] sf::Image imageFromFrm(const Frm& frm, Pal& pal) const;

    ResourceRepository& _repository;
    std::unordered_map<std::string, std::unique_ptr<sf::Texture>> _textures;
};

} // namespace geck::resource
