// Golden-file comparison. Fixtures are read and output written in binary mode with
// LF line ends, so git's autocrlf can never make a byte-identical frame mismatch.
// Rebaselining is manual: copy the written output over the fixture on purpose.
#ifndef TEST_COMMON_GOLDEN_H
#define TEST_COMMON_GOLDEN_H

#include <cstdio>
#include <string>
#include <sys/stat.h>
#include <vector>

#include "check.h"

#define TEST_FIXTURE_DIR TEST_PROJECT_DIR "/test/fixtures/"
#define TEST_OUTPUT_DIR TEST_PROJECT_DIR "/.pio/test-out/" TEST_PIOENV "/"

inline bool readBinary(const std::string &path, std::string &out) {
    FILE *f = fopen(path.c_str(), "rb");
    if (!f) return false;
    char buffer[4096];
    size_t n;
    out.clear();
    while ((n = fread(buffer, 1, sizeof(buffer), f)) > 0) out.append(buffer, n);
    fclose(f);
    return true;
}

inline void makeDir(const std::string &path) {
#ifdef _WIN32
    mkdir(path.c_str());
#else
    mkdir(path.c_str(), 0755);
#endif
}

inline std::string writeActual(const char *name, const std::string &actual) {
    makeDir(TEST_PROJECT_DIR "/.pio/test-out");
    makeDir(TEST_OUTPUT_DIR);
    const std::string path = std::string(TEST_OUTPUT_DIR) + name;
    FILE *f = fopen(path.c_str(), "wb");
    if (f) {
        fwrite(actual.data(), 1, actual.size(), f);
        fclose(f);
    }
    return path;
}

inline std::vector<std::string> splitLines(const std::string &text) {
    std::vector<std::string> lines;
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) end = text.size();
        lines.push_back(text.substr(start, end - start));
        start = end + 1;
    }
    return lines;
}

// Frame lines run to ~800 characters; show the stretch around the first difference.
inline std::string excerpt(const std::string &line, const size_t column) {
    const size_t from = column > 30 ? column - 30 : 0;
    std::string out = line.substr(from, 80);
    if (from > 0) out = "..." + out;
    if (from + 80 < line.size()) out += "...";
    return "'" + out + "'";
}

inline void assertMatchesGolden(const char *fixture, const std::string &actual) {
    std::string expected;
    const std::string fixturePath = std::string(TEST_FIXTURE_DIR) + fixture;
    if (!readBinary(fixturePath, expected)) {
        TEST_FAIL_MESSAGE(("cannot read fixture " + fixturePath).c_str());
    }
    if (expected == actual) return;

    const std::vector<std::string> want = splitLines(expected);
    const std::vector<std::string> got = splitLines(actual);
    const std::string written = writeActual(fixture, actual);

    size_t line = 0;
    while (line < want.size() && line < got.size() && want[line] == got[line]) line++;

    std::string message;
    if (line < want.size() && line < got.size()) {
        size_t column = 0;
        while (column < want[line].size() && column < got[line].size() && want[line][column] == got[line][column]) {
            column++;
        }
        message = strf("%s line %u col %u: want ", fixture, (unsigned) line + 1, (unsigned) column + 1)
                  + excerpt(want[line], column) + " got " + excerpt(got[line], column);
    } else {
        message = strf("%s: %u lines expected, %u produced (first %u identical)", fixture,
                       (unsigned) want.size(), (unsigned) got.size(), (unsigned) line);
        if (want.size() == got.size()) message = strf("%s: differs only in line ends or trailing bytes", fixture);
    }
    message += "; actual written to " + written;
    TEST_FAIL_MESSAGE(message.c_str());
}

#endif
