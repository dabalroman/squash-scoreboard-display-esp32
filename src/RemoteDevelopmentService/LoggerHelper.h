#ifndef LOGGER_HELPER
#define LOGGER_HELPER

#include "RemoteDevelopmentService.h"

extern RemoteDevelopmentService *gRemoteDevelopmentService;

inline void printLn(const char *format, ...) {
    if (!gRemoteDevelopmentService) return;

    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), format, args);
    va_end(args);

    gRemoteDevelopmentService->printLn("%s", buf);
}

#endif //LOGGER_HELPER
