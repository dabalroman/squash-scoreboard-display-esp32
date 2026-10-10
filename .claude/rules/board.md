---
paths:
  - "src/Board.h"
---

# Board and pinout

All pins live in `src/Board.h`, in **one shared block** (both boards are the same module on the same wiring), checked by `pinIsSafe()` static_asserts.

| Function | GPIO (both boards) |
|---|---|
| Remote A / B / C / D (prev / next / undo / enter) | 8 / 10 / 13 / 14 (`INPUT_PULLDOWN`: unwired pins float and fire phantom presses; the receiver drives its output, so it wins) |
| WS2812B data | 18 |
| Buzzer (active high) | 3 (V2 via MOSFET, V1 direct) |
| OLED I2C SDA / SCL | 4 / 5 (26-32 are SPI flash, 33-37 octal PSRAM on N16R8) |
| E-paper SCK / MOSI / CS / DC / RST / BUSY | 12 / 11 / 9 / 15 / 16 / 17 (unconnected on V1) |
| Battery ADC (ADC1, 10k/10k divider) | 6 |

- On the bench, LEDs and buzzer need **battery power**; USB alone does not feed the 5 V rail.
