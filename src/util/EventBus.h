#pragma once

#include <variant>
#include <functional>
#include <vector>
#include <unordered_map>
#include <typeindex>
#include <memory>
#include <mutex>
#include "Coordinates.h"

namespace geck {

// Forward declarations
class Object;
struct MapObject;

/**
 * @brief Event types for the unified event system
 */

struct TileSelectedEvent {
    TileIndex tileIndex;
    int elevation;
    bool isRoof;
};

struct ObjectSelectedEvent {
    std::shared_ptr<Object> object;
    HexPosition hexPosition;
    int elevation;
};

struct SelectionChangedEvent {
    enum class Type { Added, Removed, Cleared };
    Type type;
    int itemCount;
};

struct MapLoadedEvent {
    std::string mapPath;
    int elevationCount;
};

struct CoordinateClickedEvent {
    WorldCoords worldPos;
    ScreenCoords screenPos;
    int button; // Qt::MouseButton
};

struct ViewportChangedEvent {
    sf::FloatRect viewBounds;
    float zoomLevel;
};

struct PlacementModeChangedEvent {
    enum class Mode { None, Tile, Object };
    Mode mode;
    int selectedIndex;
};

struct DragDropEvent {
    enum class Type { Started, Moving, Completed, Cancelled };
    Type type;
    WorldCoords position;
    std::variant<TileIndex, int> data; // TileIndex or object index
};

/**
 * @brief All UI events as a variant
 */
using UIEvent = std::variant<
    TileSelectedEvent,
    ObjectSelectedEvent,
    SelectionChangedEvent,
    MapLoadedEvent,
    CoordinateClickedEvent,
    ViewportChangedEvent,
    PlacementModeChangedEvent,
    DragDropEvent
>;

/**
 * @brief Event bus for decoupled communication
 * 
 * Replaces mixed Qt signals/slots and custom callbacks with
 * a unified system following KISS principle.
 */
class EventBus {
public:
    using EventHandler = std::function<void(const UIEvent&)>;
    using SubscriptionId = size_t;

    EventBus() = default;
    ~EventBus() = default;

    // Singleton access
    static EventBus& getInstance() {
        static EventBus instance;
        return instance;
    }

    /**
     * @brief Subscribe to a specific event type
     * @tparam EventType The event type to subscribe to
     * @param handler Function to call when event is published
     * @return Subscription ID for later unsubscription
     */
    template<typename EventType>
    [[nodiscard]] SubscriptionId subscribe(std::function<void(const EventType&)> handler) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        auto id = _nextId++;
        auto wrapper = [handler](const UIEvent& event) {
            if (auto* specificEvent = std::get_if<EventType>(&event)) {
                handler(*specificEvent);
            }
        };
        
        _handlers[std::type_index(typeid(EventType))].emplace_back(id, wrapper);
        return id;
    }

    /**
     * @brief Subscribe to all events
     * @param handler Function to call for any event
     * @return Subscription ID for later unsubscription
     */
    [[nodiscard]] SubscriptionId subscribeAll(EventHandler handler) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        auto id = _nextId++;
        _universalHandlers.emplace_back(id, handler);
        return id;
    }

    /**
     * @brief Unsubscribe using subscription ID
     * @param id Subscription ID returned from subscribe
     */
    void unsubscribe(SubscriptionId id) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Remove from type-specific handlers
        for (auto& [type, handlers] : _handlers) {
            handlers.erase(
                std::remove_if(handlers.begin(), handlers.end(),
                    [id](const auto& pair) { return pair.first == id; }),
                handlers.end()
            );
        }
        
        // Remove from universal handlers
        _universalHandlers.erase(
            std::remove_if(_universalHandlers.begin(), _universalHandlers.end(),
                [id](const auto& pair) { return pair.first == id; }),
            _universalHandlers.end()
        );
    }

    /**
     * @brief Publish an event to all subscribers
     * @param event The event to publish
     */
    void publish(const UIEvent& event) {
        std::lock_guard<std::mutex> lock(_mutex);
        
        // Call type-specific handlers
        auto typeIndex = std::visit([](const auto& e) { 
            return std::type_index(typeid(e)); 
        }, event);
        
        if (auto it = _handlers.find(typeIndex); it != _handlers.end()) {
            for (const auto& [id, handler] : it->second) {
                handler(event);
            }
        }
        
        // Call universal handlers
        for (const auto& [id, handler] : _universalHandlers) {
            handler(event);
        }
    }

    /**
     * @brief Convenience method to publish specific event type
     * @tparam EventType The event type
     * @param event The event to publish
     */
    template<typename EventType>
    void publish(const EventType& event) {
        publish(UIEvent(event));
    }

    /**
     * @brief Clear all subscriptions
     */
    void clear() {
        std::lock_guard<std::mutex> lock(_mutex);
        _handlers.clear();
        _universalHandlers.clear();
    }

private:
    mutable std::mutex _mutex;
    SubscriptionId _nextId = 1;
    
    // Type-specific handlers
    std::unordered_map<std::type_index, std::vector<std::pair<SubscriptionId, EventHandler>>> _handlers;
    
    // Universal handlers that receive all events
    std::vector<std::pair<SubscriptionId, EventHandler>> _universalHandlers;

    // Prevent copying
    EventBus(const EventBus&) = delete;
    EventBus& operator=(const EventBus&) = delete;
};

/**
 * @brief RAII helper for automatic unsubscription
 */
class EventSubscription {
public:
    EventSubscription(EventBus::SubscriptionId id) : _id(id) {}
    
    ~EventSubscription() {
        if (_id != 0) {
            EventBus::getInstance().unsubscribe(_id);
        }
    }
    
    // Move only
    EventSubscription(EventSubscription&& other) noexcept : _id(other._id) {
        other._id = 0;
    }
    
    EventSubscription& operator=(EventSubscription&& other) noexcept {
        if (this != &other) {
            if (_id != 0) {
                EventBus::getInstance().unsubscribe(_id);
            }
            _id = other._id;
            other._id = 0;
        }
        return *this;
    }
    
    // No copy
    EventSubscription(const EventSubscription&) = delete;
    EventSubscription& operator=(const EventSubscription&) = delete;

private:
    EventBus::SubscriptionId _id;
};

} // namespace geck