#ifndef ROSTER_SAVE_VALIDATOR_H
#define ROSTER_SAVE_VALIDATOR_H

#include <stdint.h>
#include <string>

#include "PlayerRosterData.h"

/**
 * POST /save's validation, kept free of WebServer and Arduino String so the host
 * tests (test/test_web_roster) run it as is.
 */

// Key -> value view of an urlencoded body. The firmware backs it with WebServer::arg().
class FormLookup {
public:
    virtual ~FormLookup() {
    }

    virtual bool has(const char *key) const = 0;
    virtual std::string get(const char *key) const = 0;
};

namespace RosterSaveValidator {
    // Mints a uid for a new row: never 0, and not taken by entries below `filled`.
    typedef uint32_t (*UidGenerator)(const PlayersData &staged, uint8_t filled);

    /**
     * Every field is checked on its own - never trusted because the posted `count`
     * said so. Returns nullptr with `out` fully staged, or the Polish reason for a
     * 400 (`out` is then partial and must not be saved). The reset=1 path is the
     * caller's.
     */
    const char *validate(const FormLookup &form, UidGenerator generateUid, PlayersData &out);
}

#endif //ROSTER_SAVE_VALIDATOR_H
