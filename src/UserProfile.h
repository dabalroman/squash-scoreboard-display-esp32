#ifndef USERPROFILE_H
#define USERPROFILE_H

#include <Arduino.h>
#include <Color.h>

/**
 * One player. Two identifiers, deliberately, because they answer different
 * questions:
 *
 *   id   the position in this boot's roster, 0..n-1. What the front LEDs show as
 *        "P  3", and what MatchOrderKeeper keys its map on. Assigned at load and
 *        valid only until the next reboot: reordering the roster renumbers it.
 *
 *   uid  the durable identity, generated once and stored in NVS beside the name.
 *        Survives renaming, recolouring and reordering, so two players who happen
 *        to share a name are still two people, and anything that later keeps
 *        per-player history can attribute it to the right one.
 *
 * Use id for anything that lives inside a single match, uid for anything that
 * outlives one.
 */
class UserProfile {
    uint8_t id = 0;
    uint32_t uid = 0;
    char name[10] = {};
    Color color = Colors::White;

public:
    UserProfile(const uint8_t id, const uint32_t uid, const char *_name, const Color color)
        : id(id), uid(uid), color(color) {
        strncpy(name, _name, sizeof(name) - 1);
        name[sizeof(name) - 1] = '\0';
    }

    uint8_t getId() const {
        return id;
    }

    uint32_t getUid() const {
        return uid;
    }

    const char *getName() const {
        return name;
    }

    Color getColor() const {
        return color;
    }
};


#endif //USERPROFILE_H
