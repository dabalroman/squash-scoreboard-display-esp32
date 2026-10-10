---
paths:
  - "src/Strings.h"
  - "src/Display/LedDisplay/LedText.h"
---

# Strings (UI language)

Every user-visible string is a `constexpr const char* const` in `src/Strings.h`, chosen by `#if LANG_PL` / `#else`. **One language per build**: `-DLANG_PL` is in `build_flags` for both boards (in `[s3]`; every firmware env inherits it via `extends`), so the unselected branch never reaches the preprocessor. Never index a two-row table at runtime - that ships both languages.

- The device is Polish. The English table stays as the unbuilt `#else` branch for reference; **edit both sides** or the other language silently rots.
- **No diacritics anywhere.** Every GFX font declares range `0x20-0x7E` and `GlyphMasks.h` has no accented glyphs, so words that would need one were *replaced*, not stripped: `SIATKA` (not siatkowka), `NISKA` (not slaba).
- LED words are constrained further, to the 46-glyph table - **no W, K, M or V**. Return is `COFNIJ`, not `WSTECZ`, because the LEDs, OLED and e-paper are readable at once and must agree.
- Polish numerals decline (1 gracz / 2-4 gracze / 5+ graczy), so a `"%u <noun>"` format string is wrong for some counts. Use a label-colon form (`"W GRZE: %u"`).
- The `_OLED` variants exist because the OLED list is space-padded for centring at a fixed x (10 chars max, `FreeMono9pt7b`), while the e-paper rows are not.
- Out of scope: `printLn`/telnet/serial logs, the WiFi config web page, player names, the AP SSID/password, and the `BAT` / `FW` abbreviations.
- Verify a language change with `grep -ac "<english word>" .pio/build/v2/firmware.bin` - it must return 0.
