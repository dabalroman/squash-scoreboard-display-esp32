#ifndef SQUASH_SCOREBOARD_DISPLAY_ESP32_UTILS_H
#define SQUASH_SCOREBOARD_DISPLAY_ESP32_UTILS_H

#if __cplusplus < 201402L
#include <memory>
#include <utility>

namespace std {
    template<typename T, typename... Args>
    std::unique_ptr<T> make_unique(Args&&... args) {
        return std::unique_ptr<T>(new T(std::forward<Args>(args)...));
    }
}
#endif

#endif //SQUASH_SCOREBOARD_DISPLAY_ESP32_UTILS_H