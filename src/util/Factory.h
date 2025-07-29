#pragma once

#include <memory>
#include <functional>
#include <unordered_map>
#include <string>
#include <string_view>
#include <stdexcept>
#include "Exceptions.h"

namespace geck {

/**
 * @brief Generic factory template for creating objects
 * 
 * Consolidates multiple factory patterns (SpriteFactory, ReaderFactory, WriterFactory)
 * into a single, reusable template following DRY principle.
 * 
 * @tparam BaseType The base class type for created objects
 */
template<typename BaseType>
class Factory {
public:
    using CreatorFunc = std::function<std::unique_ptr<BaseType>()>;
    using CreatorFuncWithArgs = std::function<std::unique_ptr<BaseType>(const std::any&)>;

    Factory() = default;
    virtual ~Factory() = default;

    // Prevent copying but allow moving
    Factory(const Factory&) = delete;
    Factory& operator=(const Factory&) = delete;
    Factory(Factory&&) = default;
    Factory& operator=(Factory&&) = default;

    /**
     * @brief Register a type with a creation function
     * @param type Type identifier
     * @param creator Function that creates instances
     */
    void registerType(std::string_view type, CreatorFunc creator) {
        _creators[std::string(type)] = std::move(creator);
    }

    /**
     * @brief Register a type with a creation function that takes arguments
     * @param type Type identifier
     * @param creator Function that creates instances with arguments
     */
    void registerTypeWithArgs(std::string_view type, CreatorFuncWithArgs creator) {
        _creatorsWithArgs[std::string(type)] = std::move(creator);
    }

    /**
     * @brief Create an instance of the specified type
     * @param type Type identifier
     * @return Unique pointer to created instance
     * @throws GeckoException if type is not registered
     */
    [[nodiscard]] std::unique_ptr<BaseType> create(std::string_view type) const {
        auto it = _creators.find(std::string(type));
        if (it == _creators.end()) {
            throw GeckoException("Factory: Unknown type '" + std::string(type) + "'");
        }
        return it->second();
    }

    /**
     * @brief Create an instance of the specified type with arguments
     * @param type Type identifier
     * @param args Arguments to pass to creator
     * @return Unique pointer to created instance
     * @throws GeckoException if type is not registered
     */
    [[nodiscard]] std::unique_ptr<BaseType> createWithArgs(std::string_view type, const std::any& args) const {
        auto it = _creatorsWithArgs.find(std::string(type));
        if (it == _creatorsWithArgs.end()) {
            throw GeckoException("Factory: Unknown type with args '" + std::string(type) + "'");
        }
        return it->second(args);
    }

    /**
     * @brief Check if a type is registered
     * @param type Type identifier
     * @return True if type is registered
     */
    [[nodiscard]] bool hasType(std::string_view type) const noexcept {
        return _creators.contains(std::string(type)) || 
               _creatorsWithArgs.contains(std::string(type));
    }

    /**
     * @brief Get all registered type names
     * @return Vector of registered type names
     */
    [[nodiscard]] std::vector<std::string> getRegisteredTypes() const {
        std::vector<std::string> types;
        types.reserve(_creators.size() + _creatorsWithArgs.size());
        
        for (const auto& [type, _] : _creators) {
            types.push_back(type);
        }
        for (const auto& [type, _] : _creatorsWithArgs) {
            if (std::ranges::find(types, type) == types.end()) {
                types.push_back(type);
            }
        }
        
        return types;
    }

    /**
     * @brief Clear all registered types
     */
    void clear() noexcept {
        _creators.clear();
        _creatorsWithArgs.clear();
    }

protected:
    std::unordered_map<std::string, CreatorFunc> _creators;
    std::unordered_map<std::string, CreatorFuncWithArgs> _creatorsWithArgs;
};

/**
 * @brief Singleton factory template
 * 
 * For factories that should have only one instance globally.
 */
template<typename BaseType>
class SingletonFactory : public Factory<BaseType> {
public:
    static SingletonFactory& getInstance() {
        static SingletonFactory instance;
        return instance;
    }

private:
    SingletonFactory() = default;
};

} // namespace geck