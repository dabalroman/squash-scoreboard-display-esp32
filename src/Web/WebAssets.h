#ifndef WEB_ASSETS_H
#define WEB_ASSETS_H

class WebServer;

/**
 * The web/ files, gzipped into the app image by helpers/web_assets.py (pre-build)
 * as src/Web/WebAssets.generated.h - gitignored, included only by WebAssets.cpp.
 * In the app image rather than a filesystem partition: OTA writes only the app,
 * so page and firmware can never drift apart.
 */
namespace WebAssets {
    // 200 with the gzipped body straight from flash; false (nothing sent) for an unknown path.
    bool serveAsset(WebServer &server, const char *path);
}

#endif //WEB_ASSETS_H
