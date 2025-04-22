#ifndef USERPROFILE_H
#define USERPROFILE_H

#include <Arduino.h>
#include <Color.h>

class UserProfile {
    uint8_t id = 0;
    Color color = {0, 0, 0};

public:
    UserProfile(const uint8_t id, const Color color) : id(id), color(color) {
    }

    uint8_t getId() const {
        return id;
    }

    Color getColor() const {
        return color;
    }
};


#endif //USERPROFILE_H
