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

## Brightness schedule

The display brightness follows the sun, using the GPS location cached in EEPROM:

- **Evening:** starting at sunset, brightness steps down one level at a time,
  reaching the minimum at **bedtime** (set from the `BED TIME` menu item in
  half-hour steps between 7:00 PM and 11:30 PM; default 10:00 PM).
- **Night:** minimum brightness.
- **Morning:** starting at astronomical dawn, brightness steps back up, reaching
  the full daytime level at sunrise.

Without a known location (before the first-ever GPS fix), fixed fallback times
are used: down from 9:00 to 10:00 PM, up from 6:30 to 8:00 AM.

**Setting the daytime brightness:** the clock keeps two brightness values — the
live display brightness, which the schedule drives, and a saved *daytime*
brightness, which is the level the morning ramp climbs to and the only one
stored in EEPROM. The **+** and **−** buttons set the saved daytime brightness
**only during the day phase** (between sunrise and sunset). Pressing them at
night or during a ramp still adjusts the display immediately, but the change
is temporary: it lasts until the next phase begins, and it does not alter the
daytime setting. To change how bright the clock is during the day, adjust it
during the day.

## RTC backup battery

Neither the DS3231 nor the DS1307 can report its backup-battery voltage. What the
DS3231 does provide is an **Oscillator Stop Flag**, which latches whenever the
chip has lost all power — i.e., the coin cell could not keep it running while
the clock was unplugged. The firmware reads the flag at startup; if it is set,
the display shows **RTC BATT DEAD** in place of the greeting and repeats the
warning every 10 minutes until you enter the configuration menu (hold **+** and
**−** for two seconds) — an ordinary snooze or brightness press won't dismiss it
unnoticed. The clock keeps blinking
(unset-time mode) until GPS provides a trustworthy time. The flag is cleared
whenever the RTC is written with a trusted time, so the warning will reappear
on the next power-up only if the battery is still unable to hold the clock.

Note that replacing the battery itself cuts the RTC's power, so the warning
will show once on the first power-up after a battery change — enter the
configuration menu to dismiss it.

The check assumes a DS3231. If the RTC is a DS1307 (where that register is just
battery-backed RAM), set `RTCIsDS3231` to 0 near the top of the sketch to
disable the check and avoid false warnings.

## EEPROM map

| Address | Contents |
|---|---|
| 0 | Daytime brightness (+100) |
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
