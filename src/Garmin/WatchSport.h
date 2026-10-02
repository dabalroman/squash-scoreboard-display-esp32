#ifndef GARMIN_WATCH_SPORT_H
#define GARMIN_WATCH_SPORT_H

#include <stdint.h>

#include "DeviceMode/DeviceModeState.h"
#include "GarminProtocol.h"

/**
 * DeviceModeState <-> the wire's sport and screen (spec 11.2). One table, read both ways:
 * the STATE header's `sport`, and (for SELECT_SPORT) which mode a sport id opens.
 */
struct WatchModeInfo {
    DeviceModeState mode;
    Garmin::SportId sport;
    // False: the mode always shows `screen` (no screen data). True: the view describes it.
    bool viewDescribes;
    Garmin::ScreenId screen;
};

namespace WatchSport {
    inline const WatchModeInfo *table(uint8_t &count) {
        // A local, not a static constexpr member: C++11 has no inline variables.
        static const WatchModeInfo TABLE[] = {
            {DeviceModeState::Booting, Garmin::SportId::None, false, Garmin::ScreenId::Booting},
            {DeviceModeState::ModeSwitchingMode, Garmin::SportId::None, true, Garmin::ScreenId::Menu},
            {DeviceModeState::ConfigMode, Garmin::SportId::None, false, Garmin::ScreenId::Config},
            {DeviceModeState::PlayerSetupMode, Garmin::SportId::None, false, Garmin::ScreenId::Profile},
            {DeviceModeState::SquashMode, Garmin::SportId::Squash, true, Garmin::ScreenId::ChoosePlayers},
            {DeviceModeState::VolleyballMode, Garmin::SportId::Volleyball, true, Garmin::ScreenId::ChoosePlayers},
            {DeviceModeState::ShortVolleyballMode, Garmin::SportId::ShortVolleyball, true,
             Garmin::ScreenId::ChoosePlayers},
            {DeviceModeState::PadelMode, Garmin::SportId::Padel, true, Garmin::ScreenId::ChoosePlayers},
        };
        count = sizeof(TABLE) / sizeof(TABLE[0]);
        return TABLE;
    }

    // An unknown state is shown as BOOTING (board busy).
    inline WatchModeInfo fromModeState(const DeviceModeState mode) {
        uint8_t count;
        const WatchModeInfo *rows = table(count);
        for (uint8_t i = 0; i < count; i++) {
            if (rows[i].mode == mode) return rows[i];
        }
        return rows[0];
    }

    // False for None or an id no mode plays (-> INVALID on SELECT_SPORT).
    inline bool modeForSport(const uint8_t sport, DeviceModeState &out) {
        if (sport == static_cast<uint8_t>(Garmin::SportId::None)) return false;
        uint8_t count;
        const WatchModeInfo *rows = table(count);
        for (uint8_t i = 0; i < count; i++) {
            if (static_cast<uint8_t>(rows[i].sport) == sport) {
                out = rows[i].mode;
                return true;
            }
        }
        return false;
    }
}

#endif //GARMIN_WATCH_SPORT_H
