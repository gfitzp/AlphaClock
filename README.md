# AlphaClock

Software for Alpha Clock Five from Evil Mad Scientist Laboratories, modified to
set its time automatically from GPS, select its US time zone (with DST) from the
GPS location, and ramp the display brightness down at sunset and up at dawn.

Complete documentation for the original clock:
http://wiki.evilmadscientist.com/Alpha_Clock_Five

The firmware sketch is [`alphafive/examples/AlphaClock/AlphaClock.ino`](alphafive/examples/AlphaClock/AlphaClock.ino).

## Hardware

- Alpha Clock Five, with the MCU upgraded to an **ATmega1284** (16 MHz external clock)
- Adafruit GPS module connected to `Serial1` (9600 baud)
- Optional DS3231/ChronoDot RTC on I2C

## Build environment

- Arduino IDE with [MightyCore](https://github.com/MCUdude/MightyCore) (currently 3.1.0)
- Board settings: **ATmega1284**, Clock: **External 16 MHz**, Pinout: **Sanguino**, BOD: 2.7V
- The bootloader uploads at 57600 baud
- Serial monitor: **19200 baud** (set by `a5Init()` in the alphafive library). The clock
  prints its startup banner, GPS sync messages, and the daily sunrise/sunset times here.

Command-line compile (using the Arduino IDE's bundled CLI):

```
arduino-cli compile --fqbn "MightyCore:avr:1284:clock=16MHz_external,pinout=sanguino" alphafive/examples/AlphaClock
```

## Libraries

| Library | Version in use | Source | Purpose |
|---|---|---|---|
| alphafive | (bundled in this repo) | [`alphafive/`](alphafive/) | Alpha Clock Five display, buttons, sound, EEPROM |
| Time | 1.6.1 | https://github.com/PaulStoffregen/Time | System timekeeping (`now()`, `setTime()`, etc.) |
| DS1307RTC | 1.4.1 | https://github.com/PaulStoffregen/DS1307RTC | Optional RTC module support |
| Adafruit GPS Library | 1.7.5 | https://github.com/adafruit/Adafruit_GPS | NMEA parsing for time and location |
| Timezone | 1.3.1 | https://github.com/JChristensen/Timezone | UTC-to-local conversion with DST rules |

The sunrise/sunset calculation (USNO "Almanac for Computers" algorithm) is
embedded directly in the sketch — no additional library required.

Install the external libraries into your Arduino libraries folder
(`~/Documents/Arduino/libraries` on macOS). The `alphafive` library must also be
present there for the IDE to find it; keep that copy in sync with this repo.

## EEPROM map

| Address | Contents |
|---|---|
| 0 | Brightness (+100) |
| 1 | 12/24-hour mode |
| 2 | Alarm enabled |
| 3 | Alarm hour (+100) |
| 4 | Alarm minute (+100) |
| 5 | Alarm tone |
| 6 | Night light type |
| 7 | Number character set |
| 8 | Display mode |
| 9 | GPS mode on/off |
| 10 | Location-valid marker (0xA5) |
| 11–12 | Cached latitude (degrees × 100, int16 little-endian) |
| 13–14 | Cached longitude (degrees × 100, int16 little-endian) |
| 15 | Time zone index |
| 16 | Bedtime (half-hours past midnight) |
