// Host shim for the ESP32 Preferences (NVS) API: an in-memory store with namespaces.
// Models the device's quirk that a read-only begin() fails on a namespace that does
// not exist yet. Only what PlayerRoster uses.
#ifndef HOST_SHIM_PREFERENCES_H
#define HOST_SHIM_PREFERENCES_H

#include <cstdint>
#include <cstring>
#include <map>
#include <string>
#include <vector>

namespace HostNvs {
    typedef std::map<std::string, std::vector<uint8_t>> Namespace;

    inline std::map<std::string, Namespace> &store() {
        static std::map<std::string, Namespace> s;
        return s;
    }

    // When set, every begin() fails, as on a broken NVS partition.
    inline bool &failBegin() {
        static bool f = false;
        return f;
    }

    // Every putBytes() that reached the store, whatever the key.
    inline unsigned &writeCount() {
        static unsigned n = 0;
        return n;
    }

    inline void reset() {
        store().clear();
        failBegin() = false;
        writeCount() = 0;
    }
}

class Preferences {
    std::string ns;
    bool open = false;
    bool readOnly = true;

    const std::vector<uint8_t> *find(const char *key) const {
        if (!open) return nullptr;
        const auto &space = HostNvs::store()[ns];
        const auto it = space.find(key);
        return it == space.end() ? nullptr : &it->second;
    }

public:
    bool begin(const char *name, const bool ro = false) {
        open = false;
        if (HostNvs::failBegin()) return false;
        if (ro && HostNvs::store().count(name) == 0) return false;
        HostNvs::store()[name];
        ns = name;
        readOnly = ro;
        open = true;
        return true;
    }

    void end() { open = false; }

    bool isKey(const char *key) const { return find(key) != nullptr; }

    size_t getBytesLength(const char *key) const {
        const auto *v = find(key);
        return v ? v->size() : 0;
    }

    size_t getBytes(const char *key, void *buf, const size_t maxLen) const {
        const auto *v = find(key);
        if (!v || v->size() > maxLen) return 0;
        memcpy(buf, v->data(), v->size());
        return v->size();
    }

    size_t putBytes(const char *key, const void *value, const size_t len) {
        if (!open || readOnly) return 0;
        const auto *p = static_cast<const uint8_t *>(value);
        HostNvs::store()[ns][key] = std::vector<uint8_t>(p, p + len);
        HostNvs::writeCount()++;
        return len;
    }
};

#endif
