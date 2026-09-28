#ifndef WEB_ACCESS_GATE_H
#define WEB_ACCESS_GATE_H

/**
 * The one rule every gated route (PlayerSetupWebUi::gated()) applies: open while
 * PROFILE is on screen (the AP is up, a phone is meant to be there), or - so the
 * web UI is reachable without walking to the board and joining its setup AP -
 * while Dev Mode is on and the device already sits on the house network as STA.
 *
 * A free function, not a method, so the truth table is host-testable without
 * WebServer, PreferencesManager or RemoteDevelopmentService.
 */
namespace WebAccessGate {
    inline bool open(const bool profileActive, const bool devMode, const bool staConnected) {
        return profileActive || (devMode && staConnected);
    }
}

#endif //WEB_ACCESS_GATE_H
