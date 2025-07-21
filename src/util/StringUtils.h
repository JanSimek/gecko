#pragma once

#include <string>
#include <string_view>
#include <vector>
#include <algorithm>
#include <sstream>
#include <array>
#include <unordered_set>

namespace geck {

/**
 * @brief Optimized string utilities for performance-critical operations
 *
 * This class provides fast string operations optimized for common use cases
 * in the map editor, particularly path construction and formatting.
 */
class StringUtils {
public:
    // Fast integer to string conversion using stack-allocated buffer
    template <typename T>
    static std::string fastToString(T value) {
        static constexpr size_t BUFFER_SIZE = 32;
        char buffer[BUFFER_SIZE];
        char* end = buffer + BUFFER_SIZE;
        char* start = end;

        bool negative = false;
        if constexpr (std::is_signed_v<T>) {
            if (value < 0) {
                negative = true;
                value = -value;
            }
        }

        // Convert digits
        do {
            *--start = '0' + (value % 10);
            value /= 10;
        } while (value > 0);

        if (negative) {
            *--start = '-';
        }

        return std::string(start, end - start);
    }

    // Pre-built path cache for common patterns
    static std::string getTilePath(int tileId) {
        static thread_local std::string pathBuffer;
        pathBuffer.clear();
        pathBuffer.reserve(32); // "art/tiles/12345.frm" fits comfortably

        pathBuffer.append("art/tiles/");
        pathBuffer.append(fastToString(tileId));
        pathBuffer.append(".frm");

        return pathBuffer;
    }

    static std::string getObjectPath(int objectId) {
        static thread_local std::string pathBuffer;
        pathBuffer.clear();
        pathBuffer.reserve(32);

        pathBuffer.append("art/critters/");
        pathBuffer.append(fastToString(objectId));
        pathBuffer.append(".frm");

        return pathBuffer;
    }

    // Fast string concatenation with minimal allocations
    template <typename... Args>
    static std::string concat(Args&&... args) {
        size_t totalLength = 0;
        auto addLength = [&totalLength](const auto& arg) {
            if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, std::string>) {
                totalLength += arg.length();
            } else if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, const char*>) {
                totalLength += std::strlen(arg);
            } else if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, std::string_view>) {
                totalLength += arg.length();
            } else {
                totalLength += 20; // Estimate for numeric types
            }
        };

        (addLength(args), ...);

        std::string result;
        result.reserve(totalLength);

        auto appendArg = [&result](const auto& arg) {
            if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, std::string>) {
                result.append(arg);
            } else if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, const char*>) {
                result.append(arg);
            } else if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, std::string_view>) {
                result.append(arg);
            } else {
                result.append(fastToString(arg));
            }
        };

        (appendArg(args), ...);
        return result;
    }

    // Fast string formatting with format string
    template <typename... Args>
    static std::string format(const std::string& format, Args&&... args) {
        std::string result;
        result.reserve(format.length() + 50); // Reserve extra space for arguments

        size_t pos = 0;
        size_t argIndex = 0;

        auto formatArg = [&result](const auto& arg) {
            if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, std::string>) {
                result.append(arg);
            } else if constexpr (std::is_same_v<std::decay_t<decltype(arg)>, const char*>) {
                result.append(arg);
            } else {
                result.append(fastToString(arg));
            }
        };

        std::array<std::function<void()>, sizeof...(args)> formatters = {
            [&]() { formatArg(args); }...
        };

        while (pos < format.length()) {
            size_t nextPos = format.find("{}", pos);
            if (nextPos == std::string::npos) {
                result.append(format.substr(pos));
                break;
            }

            result.append(format.substr(pos, nextPos - pos));

            if (argIndex < formatters.size()) {
                formatters[argIndex]();
                argIndex++;
            }

            pos = nextPos + 2;
        }

        return result;
    }

    // In-place string operations to avoid allocations
    static void toLowerInPlace(std::string& str) {
        std::transform(str.begin(), str.end(), str.begin(),
            [](unsigned char c) { return std::tolower(c); });
    }

    static void toUpperInPlace(std::string& str) {
        std::transform(str.begin(), str.end(), str.begin(),
            [](unsigned char c) { return std::toupper(c); });
    }

    // Fast string trimming
    static std::string_view trim(std::string_view str) {
        auto start = str.find_first_not_of(" \t\n\r");
        if (start == std::string_view::npos) {
            return "";
        }

        auto end = str.find_last_not_of(" \t\n\r");
        return str.substr(start, end - start + 1);
    }

    // Fast string splitting using string_view to avoid copies
    static std::vector<std::string_view> split(std::string_view str, char delimiter) {
        std::vector<std::string_view> result;
        result.reserve(8); // Reserve space for common case

        size_t start = 0;
        size_t end = str.find(delimiter);

        while (end != std::string_view::npos) {
            result.push_back(str.substr(start, end - start));
            start = end + 1;
            end = str.find(delimiter, start);
        }

        result.push_back(str.substr(start));
        return result;
    }

    // Check if string ends with suffix (faster than string::ends_with for short suffixes)
    static bool endsWith(std::string_view str, std::string_view suffix) {
        if (suffix.length() > str.length()) {
            return false;
        }

        return str.substr(str.length() - suffix.length()) == suffix;
    }

    // Check if string starts with prefix
    static bool startsWith(std::string_view str, std::string_view prefix) {
        if (prefix.length() > str.length()) {
            return false;
        }

        return str.substr(0, prefix.length()) == prefix;
    }

    // Fast case-insensitive comparison
    static bool equalsIgnoreCase(std::string_view a, std::string_view b) {
        if (a.length() != b.length()) {
            return false;
        }

        return std::equal(a.begin(), a.end(), b.begin(),
            [](unsigned char a, unsigned char b) {
                return std::tolower(a) == std::tolower(b);
            });
    }

    // String pool for frequently used strings to reduce allocations
    class StringPool {
    public:
        static StringPool& getInstance() {
            static StringPool instance;
            return instance;
        }

        const std::string& intern(const std::string& str) {
            auto it = _pool.find(str);
            if (it != _pool.end()) {
                return *it;
            }

            auto result = _pool.insert(str);
            return *result.first;
        }

        void clear() {
            _pool.clear();
        }

        size_t size() const {
            return _pool.size();
        }

    private:
        std::unordered_set<std::string> _pool;
    };

    // Get interned string to reduce duplicate allocations
    static const std::string& intern(const std::string& str) {
        return StringPool::getInstance().intern(str);
    }
};

} // namespace geck