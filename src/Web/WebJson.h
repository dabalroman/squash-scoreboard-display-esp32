#ifndef WEB_JSON_H
#define WEB_JSON_H

#include <cstdio>
#include <string>

namespace WebJson {
    /**
     * Appends `text` as a quoted JSON string. `<` is escaped too, so a value can
     * never close a script element; bytes >= 0x80 pass through (the UTF-8 palette names).
     */
    inline void appendString(std::string &out, const char *text) {
        out += '"';
        for (const char *c = text; *c != '\0'; c++) {
            const unsigned char ch = static_cast<unsigned char>(*c);
            if (ch == '\\' || ch == '"') {
                out += '\\';
                out += *c;
            } else if (ch < 0x20 || ch == '<') {
                char escaped[7];
                snprintf(escaped, sizeof(escaped), "\\u%04X", ch);
                out += escaped;
            } else {
                out += *c;
            }
        }
        out += '"';
    }
}

#endif //WEB_JSON_H
