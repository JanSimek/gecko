#pragma once

#include <SFML/Graphics/Sprite.hpp>
#include <SFML/System/Vector2.hpp>
#include <memory>
#include <vector>

namespace geck {

class Object;
class EditorSession;

/**
 * @brief Pixel-perfect object picking under a world position.
 *
 * Extracted from EditorWidget so the hit-testing logic (which objects a click
 * lands on, accounting for visibility and per-pixel transparency) lives in one
 * Qt-free place. EditorWidget remains the SelectionDataProvider and delegates its
 * getObjectsAtPosition()/isObjectSelectable() overrides here.
 */
class ObjectPicker {
public:
    explicit ObjectPicker(EditorSession& session)
        : _session(session) {
    }

    /// Visible objects whose drawn pixels cover @p worldPos, topmost-drawn first
    /// (so repeated clicks cycle a stack from top to bottom).
    std::vector<std::shared_ptr<Object>> objectsAtPosition(sf::Vector2f worldPos) const;

    /// Whether an object can be picked/selected — i.e. it is currently visible.
    bool isSelectable(const std::shared_ptr<Object>& object) const;

private:
    static bool isPointInSpriteBounds(sf::Vector2f worldPos, const sf::Sprite& sprite);
    static bool isPointInSpritePixel(sf::Vector2f worldPos, const sf::Sprite& sprite);

    EditorSession& _session;
};

} // namespace geck
