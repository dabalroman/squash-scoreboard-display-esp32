---
paths:
  - "src/Display/*.h"
  - "src/Display/Images/**"
---

# Rear OLED (`BackDisplay`)

- `BackDisplay::drawBitmap()` (full-screen 128x64 art) uses the whole panel (user's call, 2026-09-28).
- **Menu rows** come from `ScrollableWidget::rowBaseline` (`TOP_OFFSET` 6 + `ROW_PITCH` 18 per row: baselines 24 / 42 / 60, rows and `>` marker alike, both boards). Row 0 clears the navbar strip. `VERTICAL_CURSOR_OFFSET_9pt7b` (20) stays the small-font line height for `setCursorToLine`.
- **OLED navbar** (`Display/OledNavbar.h`): the top strip (y 1..7, built-in 5x7 font), a value struct in the `EInkFooter` style that owns the strip's one gate, `!EInkDisplay::available()` - views never repeat it, never `BOARD_REV`. Main menu: battery icon + percent (when `batteryMonitor.available()`) and the watch badge; CONFIG: the IP (`preferencesManager.wifiIpAddress`, omitted while empty); GARMIN: the paired count. Other screens stay free of it (user's call, 2026-10-03). V2 carries the same facts in the e-paper footer. The strip refreshes only when the view re-renders (input, `restoreView`).
- `BackDisplay::drawBatteryPercent` draws a 13x7 icon (12x7 body, 1x3 nub, up to three 2x3 bars from `batteryGaugeBars()`; unlit bars are left out - an outlined 2 px bar reads as lit) 3 px left of the right-aligned percent. The widest "100%" puts the icon at x 87; `drawWatchBadge()` sits at x 74 so its blanked box ends at x 85.
