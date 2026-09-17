#ifndef LOGGER_HELPER
#define LOGGER_HELPER

#include "Board.h"
#include "RemoteDevelopmentService.h"

extern RemoteDevelopmentService *gRemoteDevelopmentService;

inline void printLn(const char *format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    // `Serial` is false while no USB host is attached, so this never stalls loop().
    if (Board::SERIAL_LOG && Serial) {
        Serial.println(buf);
    }

    if (!gRemoteDevelopmentService) return;

    gRemoteDevelopmentService->printLn(buf);
}

#endif //LOGGER_HELPER
