#include "Web/WebAssets.h"

#include <WebServer.h>
#include <string.h>

#include "Web/WebAssets.generated.h"

bool WebAssets::serveAsset(WebServer &server, const char *path) {
    for (size_t i = 0; i < WebAssetData::COUNT; i++) {
        const WebAssetData::Asset &asset = WebAssetData::ASSETS[i];
        if (strcmp(asset.path, path) != 0) {
            continue;
        }

        // Unconditional: every browser that can run the page accepts gzip, and the
        // device never decompresses.
        server.sendHeader("Content-Encoding", "gzip");
        server.sendHeader("Cache-Control", "no-store");
        // Explicit length, written straight from flash - never through a String (V1 RAM).
        server.send_P(200, asset.contentType, reinterpret_cast<const char *>(asset.data), asset.length);
        return true;
    }

    return false;
}
