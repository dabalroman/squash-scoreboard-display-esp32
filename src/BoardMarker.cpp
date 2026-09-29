#include "BoardMarker.h"

// `used` stops the compiler dropping it; --gc-sections still would unless something
// references it, which RemoteDevelopmentService's OTA log line does.
extern "C" __attribute__((section(".rodata_custom_desc"), used))
const BoardMarker::Marker board_marker = {
    BoardMarker::MAGIC,
    BoardMarker::RUNNING_REV,
    {0, 0, 0},
};
