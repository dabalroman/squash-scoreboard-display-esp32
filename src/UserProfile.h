#ifndef USERPROFILE_H
#define USERPROFILE_H

#include <Arduino.h>
#include <Color.h>

class UserProfile {
    uint8_t id = 0;
    char name[16] = {};
    Color color = Colors::White;

public:
    UserProfile(const uint8_t id, const char *_name, const Color color) : id(id), color(color) {
        strncpy(name, _name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    uint8_t getId() const {
        return id;
    }

    const char *getName() const {
        return name;
    }

    Color getColor() const {
        return color;
    }
};


#endif //USERPROFILE_H
