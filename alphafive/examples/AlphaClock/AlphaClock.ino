/*

    AlphaClock.ino

    -- Alpha Clock Five Firmware, version 2.2 --

    Version 2.2.0 - November 15, 2019
    Copyright (c) 2019 Windell H. Oskay.  All right reserved.
    http://www.evilmadscientist.com/

    ------------------------------------------------------------

    Designed for Alpha Clock Five, a five letter word clock designed by
    Evil Mad Scientist Laboratories http://www.evilmadscientist.com

    Thanks to Trammell Hudson for inspiration and helpful discussion.
    https://bitbucket.org/hudson/alphaclock

    Thanks to William Phelps - wm (at) usa.net, for several important
    bug fixes.    https://github.com/wbphelps/AlphaClock

    ------------------------------------------------------------

    Target: ATmega1284 (upgraded from the original ATmega644), clock at 16 MHz.

    Environment

    Install the MightyCore additions for Arduino:
    https://github.com/MCUdude/MightyCore#how-to-install

    Tools > Board> MightyCore: ATmega1284
    Clock: External 16 MHz
    BOD: 2.7V
    Variant: 1284 (select the actual chip on your board)
    Pinout: Sanguino pinout

    Required libraries (see README.md for the versions in use):

    Time library:         https://github.com/PaulStoffregen/Time
    DS1307RTC library:    https://github.com/PaulStoffregen/DS1307RTC
    Adafruit GPS library: https://github.com/adafruit/Adafruit_GPS
    Timezone library:     https://github.com/JChristensen/Timezone

    (The above can be added to your regular Arduino libraries folder.)

    For additional requirements, please see:
    http://wiki.evilmadscience.com/Alpha_Clock_Firmware_v2

    Note in particular that the Alpha Clock Five bootloader uses
    a typical upload speed of 57200 baud.

    ------------------------------------------------------------

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this library.  If not, see <http://www.gnu.org/licenses/>.

    Note that the two word lists included with this distribution are NOT licensed under the GPL.
    - The list in fiveletterwords.h is derived from SCOWL, http://wordlist.sourceforge.net
    Please see README-SCOWL.txt for copyright restrictions on the use and redistribution of this word list.
    - The alternative list in fiveletterwordspd.h is in the PUBLIC DOMAIN,
    and cannot be restricted by the GPL or other copyright licenses.

*/

#include "alphafive.h"      // Alpha Clock Five library

// Comment out exactly one of the following two lines

#include "fiveletterwords.h"        // Standard word list --
//#include "fiveletterwordspd.h"    // Public domain alternative

#include <Time.h>           // The Arduino Time library, https://github.com/PaulStoffregen/Time
#include <Wire.h>           // For optional RTC module
#include <DS1307RTC.h>      // For optional RTC module. https://github.com/PaulStoffregen/DS1307RTC
#include <EEPROM.h>         // For saving settings
#include <Adafruit_GPS.h>
#include <Timezone.h>
#include <avr/wdt.h>    // Watchdog timer: auto-recover from firmware hangs

// "Factory" default configuration can be configured here:

#define a5brightLevelDefault 9
#define a5HourMode24Default 0
#define a5AlarmEnabledDefault 0
#define a5AlarmHrDefault 7
#define a5AlarmMinDefault 30
#define a5NightLightTypeDefault 0
#define a5AlarmToneDefault 2
#define a5NumberCharSetDefault 2
#define a5DisplayModeDefault 0
#define a5GPSModeDefault 0          // GPS off
#define a5BedtimeDefault (22 * 60)  // Fully dimmed by 10:00 PM

#define USERNAME "GLENN"    // Greeting name used at startup
#define BIRTHDAY_MONTH 8    // What month and day to wish you a happy birthday
#define BIRTHDAY_DAY 3

// GPS and timezone settings for daylight saving time

byte GPSMode;
#define GPSSerial Serial1
#define GPSECHO false
#define GPSDEBUG false      // Set true for verbose GPS logging over serial.
                            // NOTE: the debug dumps block the main loop long enough
                            // to overflow the GPS receive buffer; leave false normally.
uint32_t timer = millis();
uint32_t last_gps_update = 60000;   // Start with at least 60 secs since last GPS update
uint32_t last_rtc_update = 0;
byte rtcSyncedFromGPS = 0;          // Set once the RTC has been written from GPS time
time_t utc_time, local_time;
Adafruit_GPS GPS(&GPSSerial);

// Note: enabling the GPS feature also enables auto-DST changes.
// The local time zone is selected automatically from the GPS location
// (see selectTimezoneIndex), using these TimeChangeRule values:

TimeChangeRule usEDT = {"EDT", Second, Sun, Mar, 2, -240};    // UTC - 4 hours
TimeChangeRule usEST = {"EST", First, Sun, Nov, 2, -300};     // UTC - 5 hours
TimeChangeRule usCDT = {"CDT", Second, Sun, Mar, 2, -300};    // UTC - 5 hours
TimeChangeRule usCST = {"CST", First, Sun, Nov, 2, -360};     // UTC - 6 hours
TimeChangeRule usMDT = {"MDT", Second, Sun, Mar, 2, -360};    // UTC - 6 hours
TimeChangeRule usMST = {"MST", First, Sun, Nov, 2, -420};     // UTC - 7 hours
TimeChangeRule usPDT = {"PDT", Second, Sun, Mar, 2, -420};    // UTC - 7 hours
TimeChangeRule usPST = {"PST", First, Sun, Nov, 2, -480};     // UTC - 8 hours
TimeChangeRule usAKDT = {"AKDT", Second, Sun, Mar, 2, -480};  // UTC - 8 hours
TimeChangeRule usAKST = {"AKST", First, Sun, Nov, 2, -540};   // UTC - 9 hours
TimeChangeRule usHST = {"HST", First, Sun, Nov, 2, -600};     // UTC - 10 hours

Timezone usEastern(usEDT, usEST);
Timezone usCentral(usCDT, usCST);
Timezone usMountain(usMDT, usMST);
Timezone usArizona(usMST);          // Arizona: Mountain Standard Time year-round
Timezone usPacific(usPDT, usPST);
Timezone usAlaska(usAKDT, usAKST);
Timezone usHawaii(usHST);           // Hawaii: no DST

#define TZEastern 0
#define TZCentral 1
#define TZMountain 2
#define TZArizona 3
#define TZPacific 4
#define TZAlaska 5
#define TZHawaii 6
#define TZCount 7

Timezone* const timezones[] =
{
  &usEastern, &usCentral, &usMountain, &usArizona, &usPacific, &usAlaska, &usHawaii
};

int8_t tzIndex = TZEastern;

// GPS location cache, for time zone selection and the sunrise/sunset calculation.
// Cached in EEPROM (addresses 10-15) so both work at power-up, before the first GPS fix.

#define EELocMagicAddr 10
#define EELocMagic 0xA5
#define EELatAddr 11
#define EELonAddr 13
#define EETzAddr 15

byte locationValid = 0;
int16_t storedLat100, storedLon100;   // Latitude and longitude, in degrees * 100

// Sunrise/sunset schedule.  Times are minutes past local midnight; -1 = unknown.

int sunriseMinutes = -1;
int sunsetMinutes = -1;
int astroDawnMinutes = -1;      // Astronomical dawn: sun 18 degrees below horizon
unsigned long lastSunCalcDayNumber = 0;   // elapsedDays() of the last recompute
byte lastSunCalcDST = 0;                  // DST in effect at the last recompute

// Brightness-ramp state:
byte lastScheduleMinute = 61;   // Evaluate the schedule only when the minute changes
int8_t lastScheduleTarget = -1; // Last brightness the schedule applied; -1 = not yet run
byte schedulePhaseLast = 0;     // 0 = day, 1 = evening ramp, 2 = night, 3 = morning ramp
byte scheduleOverride = 0;      // Set when brightness was changed manually; cleared at next phase

// Clock mode variables

byte HourMode24;
byte AlarmEnabled;          // If the "ALARM" function is currently turned on or off.
byte AlarmTimeHr;
byte AlarmTimeMin;
int8_t AlarmTone;

int8_t NightLightType;
byte NightLightSign;
unsigned int NightLightStep;

// Configuration menu:
byte menuItem;              // Current position within options menu
int8_t optionValue;
#define MenuItemsMax 12

#define AMPM24HRMenuItem 0
#define NightLightMenuItem 1
#define AlarmToneMenuItem 2
#define SoundTestMenuItem 3
#define numberCharSetMenuItem 4
#define DisplayStyleMenuItem 5
#define SetYearMenuItem 6
#define SetMonthMenuItem 7
#define SetDayMenuItem 8
#define SetSecondsMenuItem 9
#define AltModeMenuItem 10
#define GPSModeMenuItem 11
#define BedtimeMenuItem 12

// Clock display mode:
int8_t DisplayMode;
int8_t DisplayModeLocalLast;
byte DisplayModePhase;
byte DisplayModePhaseCount;
byte VCRmode;
byte modeShowMenu;
byte modeShowDateViaButtons;
byte modeLEDTest;
byte UpdateEE;
int8_t numberCharSet;

// Other global variables:
byte UseRTC;
unsigned long NextClockUpdate, NextAlarmCheck;
unsigned long milliTemp;
unsigned int FLWoffset; // Counter variable for FLW (Five Letter Word) display mode

// Text Display Variables:
unsigned long DisplayWordEndTime;
char wordCache[5];
char dpCache[5];

byte wordSequence;
byte wordSequenceStep;
byte modeShowText;

byte RedrawNow, RedrawNow_NoFade;

// Button Management:
#define ButtonCheckInterval 20    // Time delay between responding to button state, ms
#define HoldDownTime 2000         // How long to hold buttons to access menus requiring holding two buttons
byte buttonStateLast;
byte buttonMonitor;
unsigned long Btn1_AlrmSet_StartTime, Btn2_TimeSet_StartTime, Btn3_Plus_StartTime, Btn4_Minus_StartTime;
unsigned long NextButtonCheck, LastButtonPress;

byte UpdateAlarmState, UpdateBrightness;
byte AlarmTimeChanged, TimeChanged;
byte holdDebounce;

// Brightness steps for manual brightness adjustment
byte Brightness;      // Live display brightness (driven by the sunrise/sunset schedule)
byte DayBrightness;   // The user's daytime brightness setting; the only one saved to EEPROM
#define BrightnessMax 11
byte MBlevel[] =
{
  0, 1, 5, 10, 15, 19, 15, 19, 5, 10, 15, 19
};
byte MBmode[]  =
{
  0, 0, 0, 0, 0, 0, 1, 1, 2, 2, 2, 2
};
// Brightness schedule (all values in minutes past local midnight):
// In the evening, brightness ramps down step by step, starting at sunset and
// reaching minimum brightness at bedtime.  In the morning it ramps back up,
// starting at astronomical dawn and reaching full brightness at sunrise.
// Bedtime is set from the configuration menu ("BED TIME"), in half-hour
// steps between 7:00 PM and 11:30 PM, and is stored in EEPROM.
unsigned int BedtimeMinutes = a5BedtimeDefault;
#define BedtimeEarliestMinutes (19 * 60)      // 7:00 PM
#define BedtimeLatestMinutes (23 * 60 + 30)   // 11:30 PM
#define MinEveningRampMinutes 30        // Shortest evening ramp (if sunset is at/after bedtime)
#define DefaultMorningRampMinutes 90    // Morning ramp length if astronomical dawn is unavailable
#define FallbackDawnMinutes (8 * 60)    // Full brightness by 8:00 AM if sunrise is unknown

// For fade and update management:
byte SecLast;
byte MinNowOnesLast;
byte MinAlarmOnesLast;

// Alarm variables
byte AlarmTimeSnoozeMin;
byte AlarmTimeSnoozeHr;
byte snoozed;
byte alarmPrimed;
byte alarmNow;

byte modeShowAlarmTime;
byte SoundSequence;

// Alarm tone preview (played when selecting a tone in the config menu):
byte previewActive = 0;
byte previewStep;

void incrementAlarm(void)
{
  // Advance alarm time by one minute
  AlarmTimeMin += 1;

  if (AlarmTimeMin > 59)
  {
    AlarmTimeMin = 0;
    AlarmTimeHr += 1;

    if (AlarmTimeHr > 23)
    {
      AlarmTimeHr = 0;
    }
  }

  UpdateEE = 1;
}

void decrementAlarm(void)
{
  // Retard alarm time by one minute
  if (AlarmTimeMin > 0)
  {
    AlarmTimeMin--;
  }
  else
  {
    AlarmTimeMin = 59;

    if (AlarmTimeHr > 0)
    {
      AlarmTimeHr--;
    }
    else
    {
      AlarmTimeHr = 23;
    }
  }

  UpdateEE = 1;
}

void TurnOffAlarm(void)
{
  // This cancels the alarm when it is going off (or snoozed).
  // It does leave the alarm enabled for next time, however.
  if (alarmNow || snoozed)
  {
    snoozed = 0;
    alarmNow = 0;
    a5noTone();

    if (modeShowMenu == 0)
    {
      DisplayWordSequence(2);    // Display: "ALARM OFF", EXCEPT if we are in the menus.
    }
  }
}

void checkButtons(void)
{
  buttonMonitor |= a5GetButtons();

  if (milliTemp  >=  NextButtonCheck)  // Typically, go through this every 20 ms.
  {
    NextButtonCheck = milliTemp + ButtonCheckInterval;
    /*
        #define a5alarmSetBtn  1               // Snooze/Set alarm button
        #define a5timeSetBtn   2               // Set time button
        #define a5plusBtn      4               // + button
        #define a5minusBtn     8               // - button
    */

    if (buttonMonitor)   // If any buttons have been down in last ButtonCheckInterval
    {
      if (VCRmode)
      {
        EndVCRmode();    // Turn off VCR-blink mode, if it was still on.
      }

      // Check to see if any of the buttons has JUST been depressed:

      if ((buttonMonitor & a5_alarmSetBtn) && ((buttonStateLast & a5_alarmSetBtn) == 0))
      {
        // If Alarm Set button was just depressed
        Btn1_AlrmSet_StartTime = milliTemp;

        if (alarmNow)   // If alarm is going off, this button is the SNOOZE button.
        {
          if (modeShowMenu)
          {
            TurnOffAlarm();
          }
          else
          {
            alarmNow = 0;
            a5noTone();
            snoozed = 1;
            a5editFontChar('a', 54, 1, 37);     // Define special character
            DisplayWord("SNaZE", 1500);
            AlarmTimeSnoozeMin = minute() + 9;
            AlarmTimeSnoozeHr = hour();

            if (AlarmTimeSnoozeMin > 59)
            {
              AlarmTimeSnoozeMin -= 60;
              AlarmTimeSnoozeHr += 1;
            }

            if (AlarmTimeSnoozeHr > 23)
            {
              AlarmTimeSnoozeHr -= 24;
            }
          }
        }
      }

      if ((buttonMonitor & a5_timeSetBtn) && ((buttonStateLast & a5_timeSetBtn) == 0))
      {
        // Button S2 just depressed.
        Btn2_TimeSet_StartTime = milliTemp;
        TimeChanged = 0;
      }

      if ((buttonMonitor & a5_plusBtn) && ((buttonStateLast & a5_plusBtn) == 0))
      {
        Btn3_Plus_StartTime = milliTemp;
      }

      if ((buttonMonitor & a5_minusBtn) && ((buttonStateLast & a5_minusBtn) == 0))
      {
        Btn4_Minus_StartTime = milliTemp;
      }
    }
    else if ((buttonStateLast == 0) && (buttonMonitor == 0))
    {
      // Reset some variables if all buttons are up, and have not just been released:
      TimeChanged = 0;
      AlarmTimeChanged = 0;
      holdDebounce = 1;
    }

    if (modeShowMenu || buttonMonitor)
    {
      LastButtonPress = milliTemp;    // Reset EEPROM Save Timer if menu shown, or if a button pressed.
    }

    if (modeShowMenu && holdDebounce)     // Button behavior, when in Config menu mode:
    {
      // Check to see if AlarmSet button was JUST released::
      if (((buttonMonitor & a5_alarmSetBtn) == 0) && (buttonStateLast & a5_alarmSetBtn))
      {
        if (menuItem > 0)    // Decrement current position within options menu
        {
          menuItem--;
        }
        else
        {
          menuItem = MenuItemsMax;    // Wrap-around at low-end of menu
        }

        optionValue = 0;
        TurnOffAlarm();
        DisplayMenuOptionName();
      }

      // If TimeSet button was just released::
      if (((buttonMonitor & a5_timeSetBtn) == 0) && (buttonStateLast & a5_timeSetBtn))
      {
        if ((alarmNow) || (snoozed))   // Just Turn Off Alarm
        {
          TurnOffAlarm();
        }
        else    // If we are in a configuration menu
        {
          menuItem++;

          if (menuItem > MenuItemsMax)    // Decrement current position within options menu
          {
            menuItem = 0;    // Wrap-around at high-end of menu
          }

          optionValue = 0;
          TurnOffAlarm();
          DisplayMenuOptionName();
        }
      }

      if (((buttonMonitor & a5_plusBtn) == 0) && (buttonStateLast & a5_plusBtn))
      {
        // The "+" button has just been released.
        optionValue = 1;
        UpdateEE = 1;
        RedrawNow = 1;
      }

      if (((buttonMonitor & a5_minusBtn) == 0) && (buttonStateLast & a5_minusBtn))
      {
        // The "-" Button has just been released.
        optionValue = -1;
        UpdateEE = 1;
        RedrawNow = 1;
      }
    }
    else
    {
      // Button behavior, when NOT in Config menu mode:

      /////////////////////////////  Time-Of-Day Adjustments  /////////////////////////////

      // Check to see if both time-set button and plus button are both currently depressed:
      if ((buttonMonitor & a5_TimeSetPlusBtns) == a5_TimeSetPlusBtns)
        if (TimeChanged < 2)
        {
          adjustTime(60); // Add one minute
          RedrawNow = 1;
          TimeChanged = 2;  // One-time press: detected

          if (UseRTC)
          {
            RTC.set(now());
          }
        }
        else if (milliTemp >= (Btn3_Plus_StartTime + 400))
        {
          adjustTime(60); // Add one minute
          RedrawNow_NoFade = 1;

          if (UseRTC)
          {
            RTC.set(now());
          }
        }

      // Check to see if both time-set button and minus button are both currently depressed:
      if ((buttonMonitor & a5_TimeSetMinusBtns) == a5_TimeSetMinusBtns)
        if (TimeChanged < 2)
        {
          adjustTime(-60); // Subtract one minute
          RedrawNow = 1;
          TimeChanged = 2;

          if (UseRTC)
          {
            RTC.set(now());
          }
        }
        else if (milliTemp > (Btn4_Minus_StartTime + 400))
        {
          adjustTime(-60); // Subtract one minute
          RedrawNow_NoFade = 1;

          //          TimeChanged = 1;
          if (UseRTC)
          {
            RTC.set(now());
          }
        }

      /////////////////////////////  Time-Of-Alarm Adjustments  /////////////////////////////

      // Entering alarm mode:
      // If Alarm button has been down 40 ms,
      //    (to avoid displaying alarm if Alarm+Time buttons are pressed at the same time)
      //    the Set Time button is not down,
      //    and no other high-priority modes are enabled...

      if ((buttonMonitor & a5_alarmSetBtn) && (modeShowAlarmTime == 0))
        if (((buttonMonitor & a5_timeSetBtn) == 0) && (modeShowText == 0))
          if (milliTemp >= (Btn1_AlrmSet_StartTime + 40))    // of those "ifs," Check hold-time LAST.
          {
            modeShowAlarmTime = 1;
            RedrawNow = 1;
            AlarmTimeChanged = 0;
          }

      // Check to see if both alarm-set button and plus button are both currently depressed:
      if ((buttonMonitor & a5_AlarmSetPlusBtns) == a5_AlarmSetPlusBtns)
        if (TimeChanged < 2)
        {
          incrementAlarm(); // Add one minute
          RedrawNow = 1;
          TimeChanged = 2;  // One-time press: detected
          snoozed = 0;  //  Recalculating alarm time *turns snooze off.*
        }
        else if (milliTemp >= (Btn3_Plus_StartTime + 400))
        {
          incrementAlarm(); // Add one minute
          RedrawNow_NoFade = 1;
          //          TimeChanged = 1;
        }

      // Check to see if both alarm-set button and minus button are both currently depressed:
      if ((buttonMonitor & a5_AlarmSetMinusBtns) == a5_AlarmSetMinusBtns)
        if (TimeChanged < 2)
        {
          decrementAlarm(); // Subtract one minute
          RedrawNow = 1;
          TimeChanged = 2; // One-time press: detected
          snoozed = 0;  //  Recalculating alarm time *turns snooze off.*
        }
        else if (milliTemp > (Btn4_Minus_StartTime + 400))
        {
          decrementAlarm(); // Subtract one minute
          RedrawNow_NoFade = 1;
          //          TimeChanged = 1;
        }

      // Check to see if both S1 and S2 are both currently depressed:
      if ((buttonMonitor & a5_alarmSetBtn) && (buttonMonitor & a5_timeSetBtn))
      {
        if (modeShowDateViaButtons == 0)
        {
          // Display date
          modeShowDateViaButtons = 1;
          TimeChanged  = 1; // This overrides the usual alarm on/off function of the time set button.
          RedrawNow = 1;
        }
      }

      /////////////////////////////  ENTERING & LEAVING LED TEST MODE  /////////////////////////////
      // Check to see if both S1 and S2 are both currently held down:
      if ((buttonMonitor & a5_alarmSetBtn) && (buttonMonitor & a5_timeSetBtn))
      {
        if ((milliTemp >= (Btn1_AlrmSet_StartTime + 2 * HoldDownTime)) && (milliTemp >= (Btn2_TimeSet_StartTime + 2 * HoldDownTime)))
        {
          Btn1_AlrmSet_StartTime = milliTemp;  // Reset hold-down timer
          Btn2_TimeSet_StartTime = milliTemp;   // Reset hold-down timer
          holdDebounce = 0;

          if (modeLEDTest) // If we are currently in the LED Test mode,
          {
            modeLEDTest = 0;  //  Exit LED Test Mode
            RedrawNow = 1;
            DisplayWord("-END-", 1500);
          }
          else
          {
            // Display version and enter LED Test Mode
            modeLEDTest = 1;
            DisplayWordSequence(5);
            SoundSequence = 0;
          }
        }
      }

      // Check to see if AlarmSet button was JUST released::
      if (((buttonMonitor & a5_alarmSetBtn) == 0) && (buttonStateLast & a5_alarmSetBtn))
      {
        if (modeShowAlarmTime && holdDebounce)
        {
          modeShowAlarmTime = 0;
          RedrawNow = 1;
        }

        if (modeShowDateViaButtons == 1)
        {
          modeShowDateViaButtons = 0;
          RedrawNow = 1;
        }
      }

      // If TimeSet button was just released::
      if (((buttonMonitor & a5_timeSetBtn) == 0) && (buttonStateLast & a5_timeSetBtn))
      {
        if (holdDebounce)
        {
          if ((alarmNow) || (snoozed))   // Just Turn Off Alarm
          {
            TurnOffAlarm();
          }
          else if (TimeChanged == 0)   // If the time has just been adjusted, DO NOT change alarm status.
          {
            RedrawNow = 1;
            UpdateEE = 1;

            if (AlarmEnabled)
            {
              AlarmEnabled = 0;
            }
            else
            {
              AlarmEnabled = 1;
            }
          }
          else
          {
            if (UseRTC)
            {
              RTC.set(now());
            }
          }
        }
      }

      if (((buttonMonitor & a5_plusBtn) == 0) && (buttonStateLast & a5_plusBtn))
      {
        // The "+" button has just been released.
        if (holdDebounce)
        {
          if (TimeChanged > 0)
          {
            TimeChanged = 1;    // Acknowledge that the button has been released, for purposes of time editing.
          }

          if (AlarmTimeChanged > 0)
          {
            AlarmTimeChanged = 1;    // Acknowledge that the button has been released, for purposes of time editing.
          }

          // IF no other buttons are down, increase brightness:
          if (((buttonMonitor & a5_allButtonsButPlus) == 0) && (AlarmTimeChanged + TimeChanged == 0))
            if (Brightness < BrightnessMax)
            {
              Brightness++;
              UpdateBrightness = 1;
              UpdateEE = 1;

              if (schedulePhaseLast == 0)   // During the day, this sets the saved daytime brightness;
              {                             // at night or during a ramp it only adjusts the live display.
                DayBrightness = Brightness;
              }
            }
        }
      }

      if (((buttonMonitor & a5_minusBtn) == 0) && (buttonStateLast & a5_minusBtn))
      {
        // The "-" Button has just been released.
        if (holdDebounce)
        {
          if (TimeChanged > 0)
          {
            TimeChanged = 1;    // Acknowledge that the button has been released, for purposes of time editing.
          }

          if (AlarmTimeChanged > 0)
          {
            AlarmTimeChanged = 1;    // Acknowledge that the button has been released, for purposes of time editing.
          }

          // IF no other buttons are down, and times have not been adjusted, decrease brightness:
          if (((buttonMonitor & a5_allButtonsButMinus) == 0) && (AlarmTimeChanged + TimeChanged == 0))
            if (Brightness > 0)
            {
              Brightness--;
              UpdateBrightness = 1;
              UpdateEE = 1;

              if (schedulePhaseLast == 0)
              {
                DayBrightness = Brightness;
              }
            }
        }
      }
    } // End not-in-config-menu statements

    /////////////////////////////  ENTERING & LEAVING CONFIG MENU  /////////////////////////////

    // Check to see if both S3 and S4 are both currently held down:
    if ((buttonMonitor & a5_plusBtn) && (buttonMonitor & a5_minusBtn))
    {
      if ((milliTemp >= (Btn3_Plus_StartTime + HoldDownTime)) && (milliTemp >= (Btn4_Minus_StartTime + HoldDownTime)))
      {
        Btn3_Plus_StartTime = milliTemp;     // Reset hold-down timer
        Btn4_Minus_StartTime = milliTemp;    // Reset hold-down timer
        holdDebounce = 0;
        TurnOffAlarm();

        if (modeShowMenu) // If we are currently in the configuration menu,
        {
          modeShowMenu = 0;  //  Exit configuration menu
          DisplayWord("     ", 500);
        }
        else
        {
          modeShowMenu = 1;  // Enter configuration menu
          menuItem = 0;
          DisplayWord("     ", 500);
        }
      }
    }

    buttonStateLast = buttonMonitor;
    buttonMonitor = 0;
  }
}

void DisplayMenuOptionName(void)
{
  // Display title of menu name after switching to new menu utem.
  switch (menuItem)
  {
    case NightLightMenuItem:
      DisplayWordSequence(4);  // Night Light
      break;

    case AlarmToneMenuItem:
      DisplayWordSequence(6);  // Alarm Tone
      break;

    case SoundTestMenuItem:
      DisplayWordSequence(3); // Sound-test menu item, 3.  Display "TEST" "SOUND" "USE+-"
      break;

    case numberCharSetMenuItem:
      DisplayWordSequence(7); // Font Style
      break;

    case DisplayStyleMenuItem:
      DisplayWordSequence(8); // Clock Style
      break;

    case SetYearMenuItem:
      DisplayWord("YEAR ", 800);
      DisplayWordDP("___12");
      break;

    case SetMonthMenuItem:
      DisplayWord("MONTH", 800);
      break;

    case SetDayMenuItem:
      DisplayWord("DAY  ", 800);
      DisplayWordDP("__12_");
      break;

    case SetSecondsMenuItem:
      DisplayWord("SECS ", 800);
      DisplayWordDP("___12");
      break;

    case AltModeMenuItem:
      DisplayWordSequence(9); // "TIME AND..."
      //    DisplayWord ("ALTW/", 2000);
      //   DisplayWordDP("__11_");
      break;

    case GPSModeMenuItem:
      DisplayWord(" GPS ", 800);
      break;

    case BedtimeMenuItem:
      DisplayWordSequence(17);  // Bed Time
      break;

    default:  // do nothing!
      break;
  }
}

void ManageAlarm(void)
{
  if ((SoundSequence == 0) && (modeShowMenu == 0))
  {
    DisplayWord("ALARM", 400);    // Synchronize with sounds!
  }

  // RedrawNow_NoFade = 1;

  if ((TIMSK1 & _BV(OCIE1A)) == 0)     // If last tone has finished
  {
    if (AlarmTone == 0)   // X-Low Tone
    {
      if (SoundSequence < 8)
      {
        if (SoundSequence & 1)
        {
          a5tone(50, 300);
        }
        else
        {
          a5tone(0, 300);
        }

        SoundSequence++;
      }
      else
      {
        a5tone(0, 1200);
        SoundSequence = 0;
      }
    }
    else if (AlarmTone == 1)   // Low Tone
    {
      if (SoundSequence < 8)
      {
        if (SoundSequence & 1)
        {
          a5tone(100, 200);
        }
        else
        {
          a5tone(0, 200);
        }

        SoundSequence++;
      }
      else
      {
        a5tone(0, 1200);
        SoundSequence = 0;
      }
    }
    else  if (AlarmTone == 2) // Med Tone
    {
      if (SoundSequence < 6)
      {
        if (SoundSequence & 1)
        {
          a5tone(1000, 200);
        }
        else
        {
          a5tone(0, 200);
        }

        SoundSequence++;
      }
      else
      {
        a5tone(0, 1400);
        SoundSequence = 0;
      }
    }
    else  if (AlarmTone == 3) // High Tone
    {
      if (SoundSequence < 6)
      {
        if (SoundSequence & 1)
        {
          a5tone(2050, 300);
        }
        else
        {
          a5tone(0, 200);
        }

        SoundSequence++;
      }
      else
      {
        a5tone(0, 1000);
        SoundSequence = 0;
      }
    }
    else if (AlarmTone == 4)   // Siren Tone
    {
      if (SoundSequence < 254)
      {
        a5tone(20 + 4 * SoundSequence, 2);
        SoundSequence++;
      }
      else if (SoundSequence == 254)
      {
        a5tone(20 + 4 * SoundSequence, 1500);
        SoundSequence++;
      }
      else
      {
        a5tone(0, 1000);
        SoundSequence = 0;
      }
    }
    else if (AlarmTone == 5)   // "Tink" Tone
    {
      if (SoundSequence == 0)
      {
        a5tone(1000, 50);   // was 50
        SoundSequence++;
      }
      else  if (SoundSequence == 1)
      {
        a5tone(0, 1900);
        SoundSequence++;
      }
      else
      {
        a5tone(0, 50);
        SoundSequence = 0;
      }
    }
  }
}

void ManageTonePreview(void)
{
  // Play a short, one-cycle preview of the currently selected alarm tone:
  // the same patterns as ManageAlarm(), starting with a beep (no leading
  // silence) and ending without the long trailing pause.

  if ((TIMSK1 & _BV(OCIE1A)) == 0)     // If last tone has finished
  {
    if (AlarmTone == 0)   // X-Low Tone
    {
      if (previewStep < 7)
      {
        if (previewStep & 1)
        {
          a5tone(0, 300);
        }
        else
        {
          a5tone(50, 300);
        }

        previewStep++;
      }
      else
      {
        previewActive = 0;
      }
    }
    else if (AlarmTone == 1)   // Low Tone
    {
      if (previewStep < 7)
      {
        if (previewStep & 1)
        {
          a5tone(0, 200);
        }
        else
        {
          a5tone(100, 200);
        }

        previewStep++;
      }
      else
      {
        previewActive = 0;
      }
    }
    else if (AlarmTone == 2)   // Med Tone
    {
      if (previewStep < 5)
      {
        if (previewStep & 1)
        {
          a5tone(0, 200);
        }
        else
        {
          a5tone(1000, 200);
        }

        previewStep++;
      }
      else
      {
        previewActive = 0;
      }
    }
    else if (AlarmTone == 3)   // High Tone
    {
      if (previewStep < 5)
      {
        if (previewStep & 1)
        {
          a5tone(0, 200);
        }
        else
        {
          a5tone(2050, 300);
        }

        previewStep++;
      }
      else
      {
        previewActive = 0;
      }
    }
    else if (AlarmTone == 4)   // Siren Tone: the rising sweep, without the long hold
    {
      if (previewStep < 254)
      {
        a5tone(20 + 4 * previewStep, 2);
        previewStep++;
      }
      else
      {
        previewActive = 0;
      }
    }
    else   // "Tink" Tone: a single tick
    {
      if (previewStep == 0)
      {
        a5tone(1000, 50);
        previewStep++;
      }
      else
      {
        previewActive = 0;
      }
    }
  }
}

void DisplayWordSequence(byte sequence)
{
  // Usage:  // DisplayWordSequence(1); // displays "HELLO" "WORLD"
  if (sequence != wordSequence)
  {
    wordSequence = sequence;
    wordSequenceStep = 0;
  }

  DisplayWordDP("_____"); // Blank DPs unless stated otherwise.
  wordSequenceStep++;

  switch (sequence)
  {
    case 1:     // Display "HELLO" "WORLD"
      if (wordSequenceStep == 1)
      {
        DisplayWord("HELLO", 800);
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord("WORLD", 800);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("     ", 300);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 2:  // Display "ALARM" " OFF "
      if (wordSequenceStep == 1)
      {
        DisplayWord("ALARM", 800);
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord(" OFF ", 800);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("     ", 100);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 3:  // Display "TEST" "SOUND" "USE+-"
      if (wordSequenceStep == 1)
      {
        DisplayWord("TEST ", 600);
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord("SOUND", 600);
      }
      else if (wordSequenceStep == 5)
      {
        DisplayWord("USE+-", 600);
      }
      else if (wordSequenceStep < 7)
      {
        DisplayWord("     ", 200);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 4: // Display "NIGHT" "LIGHT"
      if (wordSequenceStep == 1)
      {
        DisplayWord("NIGHT", 600);
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord("LIGHT", 600);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("     ", 100);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 5: // Display "VER22" " LED " "TEST "  // Display software version number, 2.2
      if (wordSequenceStep == 1)
      {
        DisplayWord("VER22", 2000);
        DisplayWordDP("___1_");
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord(" LED ", 1000);
      }
      else if (wordSequenceStep == 5)
      {
        DisplayWord("TEST ", 1000);
      }
      else if (wordSequenceStep < 7)
      {
        DisplayWord("     ", 200);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 6:     // Display "ALARM" "TONE"
      if (wordSequenceStep == 1)
      {
        DisplayWord("ALARM", 700);
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord(" TONE", 700);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("     ", 100);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 7:     // Display "FONT " "STYLE"
      if (wordSequenceStep == 1)
      {
        DisplayWord("FONT ", 700);
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord("STYLE", 700);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("     ", 100);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 8:     // Display "CLOCK" "STYLE"
      if (wordSequenceStep == 1)
      {
        DisplayWord("CLOCK", 700);
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord("STYLE", 700);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("     ", 100);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 9:     // Display "TIME" "AND..."
      if (wordSequenceStep == 1)
      {
        DisplayWord("TIME ", 900);
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord("AND  ", 900);
        DisplayWordDP("__111");
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("     ", 100);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 10:    // Say "HELLO" "GLENN"
      if (wordSequenceStep < 3)
      {
        DisplayWord("HELLO", 800);    // not sure why I need to say hello twice for it to show up once, but whatever gets the job done...
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("HELLO", 800);
      }
      else if (wordSequenceStep < 7)
      {
        DisplayWord(USERNAME, 800);
      }
      else if (wordSequenceStep < 9)
      {
        DisplayWord("     ", 300);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 11:    // Say "HAPPY" "ANNI-" "VRSRY"
      if (wordSequenceStep < 3)
      {
        DisplayWord("HAPPY", 800);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("ANNI-", 800);
      }
      else if (wordSequenceStep < 7)
      {
        DisplayWord("VRSRY", 800);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 12:    // Say "HAPPY" "B-DAY" "GLENN"
      if (wordSequenceStep < 3)
      {
        DisplayWord("HAPPY", 800);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("B-DAY", 800);
      }
      else if (wordSequenceStep < 7)
      {
        DisplayWord(USERNAME, 800);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 13:    // Say "HAPPY" "NEW" "YEAR"
      if (wordSequenceStep < 3)
      {
        DisplayWord("HAPPY", 800);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord(" NEW ", 800);
      }
      else if (wordSequenceStep < 7)
      {
        DisplayWord("YEAR ", 800);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 14:    // Say "HAPPY" "4TH"
      if (wordSequenceStep < 3)
      {
        DisplayWord("HAPPY", 800);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord(" 4TH ", 800);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 15:    // Say "HAPPY" "THNKS" "GVING"
      if (wordSequenceStep < 3)
      {
        DisplayWord("HAPPY", 800);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("THNKS", 800);
      }
      else if (wordSequenceStep < 7)
      {
        DisplayWord("GVING", 800);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 16:    // Say "MERRY" "X-MAS"
      if (wordSequenceStep < 3)
      {
        DisplayWord("MERRY", 800);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("X-MAS", 800);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    case 17:    // Display " BED " "TIME "
      if (wordSequenceStep == 1)
      {
        DisplayWord(" BED ", 600);
      }
      else if (wordSequenceStep == 3)
      {
        DisplayWord("TIME ", 600);
      }
      else if (wordSequenceStep < 5)
      {
        DisplayWord("     ", 100);
      }
      else
      {
        wordSequence = 0;
      }

      break;

    default:
      // Turn off word sequences. (Catches case 0.)
      wordSequence = 0;
      wordSequenceStep = 0;
  }
}

void DisplayWord(char WordIn[], unsigned int duration)
{
  // Usage: DisplayWord ("ALARM", 500);
  modeShowText = 1;
  wordCache[0] = WordIn[0];
  wordCache[1] = WordIn[1];
  wordCache[2] = WordIn[2];
  wordCache[3] = WordIn[3];
  wordCache[4] = WordIn[4];
  DisplayWordEndTime = milliTemp + duration;
  RedrawNow = 1;
}

void DisplayWordDP(char WordIn[])
{
  // Usage: DisplayWord ("_123_");
  // Add or edit decimals for text displayed via DisplayWord().
  // Call in conjuction with DisplayWord, just before or after.
  dpCache[0] = WordIn[0];
  dpCache[1] = WordIn[1];
  dpCache[2] = WordIn[2];
  dpCache[3] = WordIn[3];
  dpCache[4] = WordIn[4];
}

void SpecialOccasionMessage()
{
  // Only show the message a few times per hour, and only in the morning
  if (second() % 20 == 0 && hour() < 12)
  {
    // personal messages
    if (month() == BIRTHDAY_MONTH && day() == BIRTHDAY_DAY)
    {
      DisplayWordSequence(12);    // Happy birthday!
    }
    // generic holidays go after the personal ones
    else if (month() == 1 && day() == 1)
    {
      DisplayWordSequence(13);    // Happy new year!
    }

    // else if (month() == 7 && day() == 4)
    //   DisplayWordSequence(14);  // Happy 4th of July!
    // else if (month() == 11 && weekday() == 5 && day() >= 22 && day() <= 28)
    //   DisplayWordSequence(15);  // Happy Thanksgiving!
    // else if (month() == 12 && day() == 25)
    //   DisplayWordSequence(16);  // Merry Christmas!
  }
}

void  EndVCRmode()
{
  if (VCRmode)
  {
    a5_brightLevel = MBlevel[Brightness];
    RedrawNow_NoFade = 1;
    VCRmode = 0;
    randomSeed(now());  // Either a button press or RTC time
  }
}

int8_t selectTimezoneIndex(float lat, float lon)
{
  // Approximate US time zone selection from GPS coordinates.
  // Real zone boundaries follow state lines rather than meridians, so
  // locations near a boundary (western Texas, Indiana, etc.) may be
  // classified wrong.  Note: the Navajo Nation observes DST but falls
  // inside the Arizona box here.

  if ((lat < 25.0) && (lon < -140.0))
  {
    return TZHawaii;
  }

  if ((lat > 50.0) && (lon < -125.0))
  {
    return TZAlaska;
  }

  if ((lat > 31.3) && (lat < 37.0) && (lon > -114.9) && (lon < -109.0))
  {
    return TZArizona;   // No DST
  }

  if (lon >= -85.0)
  {
    return TZEastern;
  }

  if (lon >= -102.0)
  {
    return TZCentral;
  }

  if (lon >= -115.0)
  {
    return TZMountain;
  }

  return TZPacific;
}

int sunEventMinutes(byte rise, int yr, byte mo, byte dy, float lat, float lon, int utcOffsetMin, float zenith)
{
  // Sunrise/sunset calculation, from the "Almanac for Computers" algorithm
  // (US Naval Observatory, 1990; as described by Ed Williams).
  // Returns the event time in minutes past local midnight, or -1 if the sun
  // does not reach the given zenith angle on that date at that location.
  // Zenith angles: 90.833 = official sunrise/sunset (upper limb + refraction),
  // 96 = civil twilight, 102 = nautical twilight, 108 = astronomical twilight.
  // Accuracy is within a couple of minutes, which is plenty for dimming a clock.
  // Uses floating point, so call it once per day -- not in a tight loop.

  int dayOfYear = (275 * mo / 9) - (((mo + 9) / 12) * (1 + ((yr - 4 * (yr / 4) + 2) / 3))) + dy - 30;

  float lngHour = lon / 15.0;
  float t;

  if (rise)
  {
    t = dayOfYear + ((6.0 - lngHour) / 24.0);
  }
  else
  {
    t = dayOfYear + ((18.0 - lngHour) / 24.0);
  }

  float M = (0.9856 * t) - 3.289;                       // Sun's mean anomaly
  float L = M + (1.916 * sin(M * DEG_TO_RAD)) + (0.020 * sin(2 * M * DEG_TO_RAD)) + 282.634;
  L = fmod(L, 360.0);                                   // Sun's true longitude

  if (L < 0)
  {
    L += 360.0;
  }

  float RA = atan(0.91764 * tan(L * DEG_TO_RAD)) * RAD_TO_DEG;  // Right ascension
  RA = fmod(RA, 360.0);

  if (RA < 0)
  {
    RA += 360.0;
  }

  // Put right ascension into the same quadrant as L, then convert to hours
  RA = (RA + (floor(L / 90.0) * 90.0) - (floor(RA / 90.0) * 90.0)) / 15.0;

  float sinDec = 0.39782 * sin(L * DEG_TO_RAD);         // Sun's declination
  float cosDec = cos(asin(sinDec));

  float cosH = (cos(zenith * DEG_TO_RAD) - (sinDec * sin(lat * DEG_TO_RAD)))
               / (cosDec * cos(lat * DEG_TO_RAD));

  if ((cosH > 1.0) || (cosH < -1.0))
  {
    return -1;    // Sun never reaches this zenith angle on this date at this location
  }

  float H;    // Sun's local hour angle, converted to hours

  if (rise)
  {
    H = (360.0 - (acos(cosH) * RAD_TO_DEG)) / 15.0;
  }
  else
  {
    H = (acos(cosH) * RAD_TO_DEG) / 15.0;
  }

  float UT = fmod(H + RA - (0.06571 * t) - 6.622 - lngHour, 24.0);   // Event time, UTC hours

  if (UT < 0)
  {
    UT += 24.0;
  }

  int localMin = (int)(UT * 60.0 + 0.5) + utcOffsetMin;

  while (localMin < 0)
  {
    localMin += 1440;
  }

  while (localMin >= 1440)
  {
    localMin -= 1440;
  }

  return localMin;
}

void recomputeSunTimes(void)
{
  time_t tLocal = now();
  lastSunCalcDayNumber = elapsedDays(tLocal);
  lastSunCalcDST = timezones[tzIndex]->locIsDST(tLocal);

  if (locationValid == 0)
  {
    sunriseMinutes = -1;
    sunsetMinutes = -1;
    astroDawnMinutes = -1;
    return;
  }

  float lat = storedLat100 / 100.0;
  float lon = storedLon100 / 100.0;

  // Current UTC offset (including DST), in minutes, from the active time zone.
  // time_t is unsigned, so the difference must be cast to a signed type before
  // dividing: for zones west of Greenwich it is negative.
  int utcOffsetMin = (int)(((int32_t)(tLocal - timezones[tzIndex]->toUTC(tLocal))) / 60);

  sunriseMinutes = sunEventMinutes(1, year(tLocal), month(tLocal), day(tLocal), lat, lon, utcOffsetMin, 90.833);
  sunsetMinutes = sunEventMinutes(0, year(tLocal), month(tLocal), day(tLocal), lat, lon, utcOffsetMin, 90.833);
  astroDawnMinutes = sunEventMinutes(1, year(tLocal), month(tLocal), day(tLocal), lat, lon, utcOffsetMin, 108.0);

  Serial.print("Sun times recomputed. Astronomical dawn: ");

  if (astroDawnMinutes >= 0)
  {
    Serial.print(astroDawnMinutes / 60);
    printDigits(astroDawnMinutes % 60);
  }
  else
  {
    Serial.print("none");
  }

  Serial.print(", sunrise: ");

  if (sunriseMinutes >= 0)
  {
    Serial.print(sunriseMinutes / 60);
    printDigits(sunriseMinutes % 60);
  }
  else
  {
    Serial.print("none");
  }

  Serial.print(", sunset: ");

  if (sunsetMinutes >= 0)
  {
    Serial.print(sunsetMinutes / 60);
    printDigits(sunsetMinutes % 60);
  }
  else
  {
    Serial.print("none");
  }

  Serial.println();
}

void EEReadLocation(void)
{
  if (EEPROM.read(EELocMagicAddr) == EELocMagic)
  {
    storedLat100 = (int16_t)(EEPROM.read(EELatAddr) | ((uint16_t)EEPROM.read(EELatAddr + 1) << 8));
    storedLon100 = (int16_t)(EEPROM.read(EELonAddr) | ((uint16_t)EEPROM.read(EELonAddr + 1) << 8));
    locationValid = 1;
    tzIndex = EEPROM.read(EETzAddr);

    if ((tzIndex < 0) || (tzIndex >= TZCount))
    {
      tzIndex = TZEastern;
    }
  }
}

void EESaveLocation(void)
{
  a5writeEEPROM(EELocMagicAddr, EELocMagic);
  a5writeEEPROM(EELatAddr, storedLat100 & 0xFF);
  a5writeEEPROM(EELatAddr + 1, (storedLat100 >> 8) & 0xFF);
  a5writeEEPROM(EELonAddr, storedLon100 & 0xFF);
  a5writeEEPROM(EELonAddr + 1, (storedLon100 >> 8) & 0xFF);
  a5writeEEPROM(EETzAddr, tzIndex);
}

void updateLocationFromGPS(void)
{
  // Cache the GPS location and select the local time zone from it.
  // EEPROM is only rewritten when the clock has moved more than ~0.1 degrees
  // (~7 miles), so this is safe to call on every GPS time update.
  float lat = GPS.latitudeDegrees;
  float lon = GPS.longitudeDegrees;

  if ((lat == 0.0) && (lon == 0.0))
  {
    return;   // No valid location data yet
  }

  int16_t lat100 = (int16_t)(lat * 100.0);
  int16_t lon100 = (int16_t)(lon * 100.0);

  if (locationValid && (abs(lat100 - storedLat100) <= 10) && (abs(lon100 - storedLon100) <= 10))
  {
    return;   // Location unchanged
  }

  storedLat100 = lat100;
  storedLon100 = lon100;
  locationValid = 1;
  tzIndex = selectTimezoneIndex(lat, lon);
  EESaveLocation();
  recomputeSunTimes();

  const char* const tzNames[] =
  {
    "Eastern", "Central", "Mountain", "Arizona", "Pacific", "Alaska", "Hawaii"
  };
  Serial.print("GPS location cached: ");
  Serial.print(lat, 2);
  Serial.print(", ");
  Serial.print(lon, 2);
  Serial.print("  Time zone: ");
  Serial.println(tzNames[tzIndex]);
}

void applySunSchedule(void)
{
  // Brightness ramp scheduler.  Four phases per day:
  //   Day (full daytime brightness)
  //   Evening ramp: step down from daytime brightness, sunset -> bedtime, reaching minimum at bedtime
  //   Night (minimum brightness)
  //   Morning ramp: step up from minimum, astronomical dawn -> sunrise, reaching daytime brightness at sunrise
  // Each step uses the display's normal fade, so the ramps feel continuous.
  // A manual brightness change suspends the schedule until the next phase begins.
  // Without a known GPS location, falls back to fixed times (9-10 PM down, 6:30-8 AM up).

  if (minute() == lastScheduleMinute)
  {
    return;   // Targets have minute resolution; evaluate once per minute.
  }

  lastScheduleMinute = minute();

  // Recompute the sun times once per day, whenever the date jumps (e.g., GPS
  // first setting the clock), and when DST switches during the day.
  time_t tNow = now();

  if ((elapsedDays(tNow) != lastSunCalcDayNumber) || (timezones[tzIndex]->locIsDST(tNow) != lastSunCalcDST))
  {
    recomputeSunTimes();
  }

  // Evening ramp window: sunset to bedtime
  int eveEnd = BedtimeMinutes;
  int eveStart = (sunsetMinutes >= 0) ? sunsetMinutes : (BedtimeMinutes - 60);

  if (eveStart > eveEnd - MinEveningRampMinutes)
  {
    eveStart = eveEnd - MinEveningRampMinutes;    // Keep a minimum ramp length (high-latitude summers)
  }

  // Morning ramp window: astronomical dawn to sunrise
  int mornEnd = (sunriseMinutes >= 0) ? sunriseMinutes : FallbackDawnMinutes;
  int mornStart;

  if ((astroDawnMinutes >= 0) && (astroDawnMinutes < mornEnd))
  {
    mornStart = astroDawnMinutes;
  }
  else
  {
    // No astronomical twilight (bright high-latitude nights), or it wrapped
    // past midnight: use a fixed-length pre-sunrise ramp instead.
    mornStart = mornEnd - DefaultMorningRampMinutes;

    if (mornStart < 0)
    {
      mornStart = 0;
    }
  }

  int nowMin = hour() * 60 + minute();
  int8_t dayBright = DayBrightness;

  if (dayBright < 1)
  {
    dayBright = 1;    // Never ramp toward a fully dark display
  }
  int8_t target;
  byte phase;

  if ((nowMin >= eveStart) && (nowMin < eveEnd))
  {
    phase = 1;    // Evening ramp: interpolate dayBright down toward 1, hitting it at bedtime
    target = dayBright - (int8_t)(((int)(dayBright - 1) * (nowMin - eveStart) + (eveEnd - eveStart) / 2)
                                  / (eveEnd - eveStart));
  }
  else if ((nowMin >= mornStart) && (nowMin < mornEnd))
  {
    phase = 3;    // Morning ramp: interpolate 1 up toward dayBright, hitting it at sunrise
    target = 1 + (int8_t)(((int)(dayBright - 1) * (nowMin - mornStart) + (mornEnd - mornStart) / 2)
                          / (mornEnd - mornStart));
  }
  else if ((nowMin >= mornEnd) && (nowMin < eveStart))
  {
    phase = 0;    // Day
    target = dayBright;
  }
  else
  {
    phase = 2;    // Night
    target = 1;
  }

  if (lastScheduleTarget < 0)
  {
    // First evaluation after power-up: apply the schedule directly.
    schedulePhaseLast = phase;
    scheduleOverride = 0;
  }
  else if (phase != schedulePhaseLast)
  {
    schedulePhaseLast = phase;
    scheduleOverride = 0;   // New phase: the schedule takes control again
  }
  else if (Brightness != lastScheduleTarget)
  {
    scheduleOverride = 1;   // Brightness was changed by hand: leave it alone until the next phase
  }

  if ((scheduleOverride == 0) && (Brightness != target))
  {
    Brightness = target;
    UpdateBrightness = 1;
  }

  lastScheduleTarget = target;
}

void setup()
{
  MCUSR = 0;        // Clear the reset-cause flags so a watchdog reset
  wdt_disable();    // can't leave the watchdog running into setup()

  a5Init();  // Required hardware init for Alpha Clock Five library functions
  VCRmode = 1;
  Serial.println("\nHello, World.");
  Serial.println("Alpha Clock Five here, reporting for duty!");
  EEReadSettings(); // Read settings stored in EEPROM
  EEReadLocation(); // Read cached GPS location and time zone from EEPROM

  /*
      Figured out a lot of the GPS code from https://github.com/adafruit/Adafruit_GPS/
      examples.

      Software License Agreement (BSD License)

      Copyright (c) 2012, Adafruit Industries
      All rights reserved.

      Redistribution and use in source and binary forms, with or without
      modification, are permitted provided that the following conditions are met:
      1. Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
      2. Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
      3. Neither the name of the copyright holders nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

      THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ''AS IS'' AND ANY
      EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
      WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
      DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE FOR ANY
      DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
      (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
      LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
      ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
      (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
      SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
  */

  if (GPSMode)
  {
    GPS.begin(9600);
    GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA); // needs to be PMTK_SET_NMEA_OUTPUT_RMCGGA otherwise we don't get the number of satellites we can currently see
    GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);    // 1, 5, 10 second GPS updates: PMTK_SET_NMEA_UPDATE_1HZ, PMTK_SET_NMEA_UPDATE_200_MILLIHERTZ, PMTK_SET_NMEA_UPDATE_100_MILLIHERTZ
    GPS.sendCommand(PGCMD_ANTENNA);
    delay(1000);
    GPSSerial.println(PMTK_Q_RELEASE);
  }

  if (Brightness == 0)
  {
    Brightness = 1;    // If display is fully dark at reset, turn it up to minimum brightness.
  }

  // Bound every I2C transaction: a bus glitch becomes a 25 ms error instead
  // of a hang (and the watchdog reset that would follow).
  Wire.setWireTimeout(25000, true);
  UseRTC = a5CheckForRTC();

  if (UseRTC)
  {
    setSyncProvider(RTC.get);   // Function to get the time from the RTC (e.g., Chronodot)

    if (timeStatus() != timeSet)
    {
      Serial.println("RTC detected, *but* I can't seem to sync to it. ;(");
      UseRTC = 0;
    }
    else
    {
      Serial.println("System time: Set by RTC.  Rock on!");
      EndVCRmode();
    }
  }
  else
  {
    Serial.println("RTC not detected. I don't know what time it is.  :(");
  }

  if (UseRTC == 0)
  {
    Serial.println("Setting the date to 2013. I didn't exist in 1970.");
    setTime(0, 0, 0, 1, 1, 2013);
  }

  SerialPrintTime();
  Serial.println();
  NextClockUpdate = millis() + 1;
  buttonMonitor = 0;
  holdDebounce = 0;
  modeShowAlarmTime = 0;
  modeShowDateViaButtons = 0; // for button-press date display
  modeShowMenu = 0;
  modeShowText = 0;
  modeLEDTest = 0;
  // Alarm Setup:
  snoozed = 0;
  alarmPrimed = 0;
  alarmNow = 0;
  SoundSequence = 0;
  NextButtonCheck = NextClockUpdate;
  NextAlarmCheck =  NextClockUpdate;
  UpdateEE = 0;
  LastButtonPress = NextClockUpdate;
  wordSequence = 0;
  wordSequenceStep = 0;
  RedrawNow = 1;
  RedrawNow_NoFade = 0;
  UpdateBrightness = 0;
  DisplayWordSequence(10);  // Say hello!
  buttonMonitor = a5GetButtons();

  if ((buttonMonitor & a5_alarmSetBtn) && (buttonMonitor & a5_timeSetBtn))
  {
    // If Alarm button and Time button (LED Test buttons) held down at turn on, reset to defaults.
    Brightness = a5brightLevelDefault;
    DayBrightness = a5brightLevelDefault;
    HourMode24 = a5HourMode24Default;
    AlarmEnabled = a5AlarmEnabledDefault;
    AlarmTimeHr = a5AlarmHrDefault;
    AlarmTimeMin = a5AlarmMinDefault;
    AlarmTone = a5AlarmToneDefault;
    NightLightType = a5NightLightTypeDefault;
    numberCharSet = a5NumberCharSetDefault;
    DisplayMode = a5DisplayModeDefault;
    GPSMode = a5GPSModeDefault;
    BedtimeMinutes = a5BedtimeDefault;
    wordSequenceStep = 0;
    DisplayWord("*****", 1000);
  }

  a5_brightLevel = MBlevel[Brightness];
  a5_brightMode = MBmode[Brightness];
  a5loadAltNumbers(numberCharSet);
  FLWoffset = 0;
  NightLightSign = 1;
  NightLightStep = 0;
  updateNightLight();
  DisplayModePhase = 0;
  DisplayModePhaseCount = 0;

  wdt_enable(WDTO_8S);  // Watchdog: reset the clock if the main loop ever
                        // hangs for more than 8 seconds (e.g., an I2C lockup)
}

void loop()
{
  wdt_reset();    // Feed the watchdog: we made it around the loop
  milliTemp = millis();
  checkButtons();

  // More code building on the https://github.com/adafruit/Adafruit_GPS examples...

  if (GPSMode)
  {
    // read data from the GPS in the 'main loop'
    char c = GPS.read();

    // if you want to debug, this is a good time to do it!
    if (GPSECHO)
      if (c)
      {
        Serial.print(c);
      }

    // if a sentence is received, we can check the checksum, parse it...
    if (GPS.newNMEAreceived())
    {
      // a tricky thing here is if we print the NMEA sentence, or data
      // we end up not listening and catching other sentences!
      // so be very wary if using OUTPUT_ALLDATA and trying to print out data
      if (GPSDEBUG)
      {
        Serial.print(GPS.lastNMEA()); // this also sets the newNMEAreceived() flag to false
      }

      // Parse the sentence (this also clears the newNMEAreceived() flag). A sentence that
      // fails to parse is simply skipped; the rest of the loop still runs.
      // Only update the time if we have a fix and we're getting RMC sentences, since those have both time and date values.
      // (strstr rather than String: no heap allocation, and matches both $GPRMC and the $GNRMC sent by multi-constellation modules.)
      if (GPS.parse(GPS.lastNMEA()) && GPS.fix && (strstr(GPS.lastNMEA(), "RMC") != NULL))
      {
        // Convert the GPS time into Unix epoch time
        utc_time = makeTime({GPS.seconds, GPS.minute, GPS.hour, 0, GPS.day, GPS.month, CalendarYrToTm(2000 + GPS.year)}); // '0' because makeTime() needs a weekday
        // Convert the Unix epoch time to the local time, using the time zone chosen from the GPS location
        local_time = timezones[tzIndex]->toLocal(utc_time);
        // Serial.print("Local time: "); Serial.println(local_time);

        // Update the time once a minute
        if (millis() - last_gps_update >= 60000)
        {
          last_gps_update = millis(); // Reset the timer
          updateLocationFromGPS();    // Refresh cached location / time zone, if the clock has moved
          local_time = timezones[tzIndex]->toLocal(utc_time);   // Recompute, in case the time zone just changed
          Serial.println();
          Serial.print("UTC time from GPS: ");
          Serial.print(2000 + GPS.year);
          Serial.print("-");

          if (GPS.month < 10)
          {
            Serial.print('0');
          }

          Serial.print(GPS.month);
          Serial.print("-");

          if (GPS.day < 10)
          {
            Serial.print('0');
          }

          Serial.print(GPS.day);
          Serial.print(" ");

          if (GPS.hour < 10)
          {
            Serial.print('0');
          }

          Serial.print(GPS.hour);
          Serial.print(":");

          if (GPS.minute < 10)
          {
            Serial.print('0');
          }

          Serial.print(GPS.minute);
          Serial.print(":");

          if (GPS.seconds < 10)
          {
            Serial.print('0');
          }

          Serial.println(GPS.seconds);
          Serial.print("       Local time: ");
          Serial.print(year(local_time));
          Serial.print("-");

          if (month(local_time) < 10)
          {
            Serial.print('0');
          }

          Serial.print(month(local_time));
          Serial.print("-");

          if (day(local_time) < 10)
          {
            Serial.print('0');
          }

          Serial.print(day(local_time));
          Serial.print(" ");

          if (hour(local_time) < 10)
          {
            Serial.print('0');
          }

          Serial.print(hour(local_time));
          Serial.print(":");

          if (minute(local_time) < 10)
          {
            Serial.print('0');
          }

          Serial.print(minute(local_time));
          Serial.print(":");

          if (second(local_time) < 10)
          {
            Serial.print('0');
          }

          Serial.println(second(local_time));
          // Set the internal clock
          setTime(local_time);
          EndVCRmode();     // GPS time counts as a valid sync: stop the "unset clock" blinking
          Serial.print("Set the time");

          // Also set the real-time clock if one is present.  Write it on the
          // first GPS sync and then only hourly: every write is a blocking I2C
          // transaction, and the fewer of those, the fewer chances for a bus
          // glitch to hang the main loop.
          if (UseRTC && ((rtcSyncedFromGPS == 0) || (millis() - last_rtc_update >= 3600000UL)))
          {
            last_rtc_update = millis();
            rtcSyncedFromGPS = 1;
            RTC.set(now());
            Serial.print(" and the real-time clock");
          }

          Serial.println(" using the GPS");
          Serial.println();
        }
      }
    }

    // approximately every 2 seconds or so, print out the current stats.
    // Debug only: this dump blocks the loop long enough for the 64-byte GPS
    // receive buffer to overflow, corrupting sentences.
    if (GPSDEBUG && (millis() - timer > 2000))
    {
      timer = millis(); // reset the timer
      Serial.print("\nTime: ");

      if (GPS.hour < 10)
      {
        Serial.print('0');
      }

      Serial.print(GPS.hour, DEC);
      Serial.print(':');

      if (GPS.minute < 10)
      {
        Serial.print('0');
      }

      Serial.print(GPS.minute, DEC);
      Serial.print(':');

      if (GPS.seconds < 10)
      {
        Serial.print('0');
      }

      Serial.print(GPS.seconds, DEC);
      Serial.print('.');

      if (GPS.milliseconds < 10)
      {
        Serial.print("00");
      }
      else if (GPS.milliseconds > 9 && GPS.milliseconds < 100)
      {
        Serial.print("0");
      }

      Serial.println(GPS.milliseconds);
      Serial.print("Date: ");
      Serial.print("20");
      Serial.print(GPS.year, DEC);
      Serial.print("-");

      if (GPS.month < 10)
      {
        Serial.print("0");
      }

      Serial.print(GPS.month, DEC);
      Serial.print("-");

      if (GPS.day < 10)
      {
        Serial.print("0");
      }

      Serial.println(GPS.day, DEC);
      Serial.print("Epoch time: ");
      Serial.println(utc_time);
      Serial.print("Fix: ");
      Serial.print((int)GPS.fix);
      Serial.print(" quality: ");
      Serial.println((int)GPS.fixquality);

      if (GPS.fix)
      {
        Serial.print("Location: ");
        Serial.print(GPS.latitude, 4);
        Serial.print(GPS.lat);
        Serial.print(", ");
        Serial.print(GPS.longitude, 4);
        Serial.println(GPS.lon);
        Serial.print("Speed (knots): ");
        Serial.println(GPS.speed);
        Serial.print("Angle: ");
        Serial.println(GPS.angle);
        Serial.print("Altitude: ");
        Serial.println(GPS.altitude);
        Serial.print("Satellites: ");
        Serial.println((int)GPS.satellites);
      }

      Serial.println();
    }
  }

  // Brightness ramps: down from sunset to bedtime, up from astronomical
  // dawn to sunrise (fixed fallback times when the GPS location is unknown).
  applySunSchedule();

  if (UpdateBrightness)
  {
    UpdateBrightness = 0;  // Reset the flag that triggered this clause.

    if (a5_brightMode == MBmode[Brightness])
    {
      a5_brightLevel = MBlevel[Brightness];
      UpdateDisplay(1);  // Force update of display data, with new brightness level
    }
    else
    {
      a5_brightLevel = 0;
      UpdateDisplay(1);  // Force update of display data, with temporary brightness level
      a5loadVidBuf_fromOSB();
      a5_brightLevel = MBlevel[Brightness];
      UpdateDisplay(1);  // Force update of display data, with new brightness level
      a5_brightMode = MBmode[Brightness];
    }
  }

  if (VCRmode)
  {
    if (modeShowText == 0)
    {
      byte temp = second() & 1;

      if ((temp) && (VCRmode == 1))
      {
        a5_brightLevel = 0;
        RedrawNow_NoFade = 1;
        VCRmode = 2;
      }

      if ((temp == 0) && (VCRmode == 2))
      {
        a5_brightLevel = MBlevel[Brightness];
        RedrawNow_NoFade = 1;
        VCRmode = 1;
      }
    }
  }

  if (RedrawNow || RedrawNow_NoFade)
  {
    NextClockUpdate = milliTemp + 10; // Reset auto-redraw timer.
    UpdateDisplay(1);    // Force redraw

    if (RedrawNow_NoFade)   // Explicitly do not fade.  Takes priority over redraw with fade.
    {
      a5_FadeStage = -1;
    }

    a5LoadNextFadeStage();
    a5loadVidBuf_fromOSB();
    RedrawNow = 0;
    RedrawNow_NoFade = 0;
  }
  else if (milliTemp >= NextClockUpdate)  // Update at most 100 times per second
  {
    NextClockUpdate = milliTemp + 10; // Reset auto-redraw timer.
    UpdateDisplay(0);  // Argument 0: Only update if display data has changed.
    a5LoadNextFadeStage();
    a5loadVidBuf_fromOSB();

    if (NightLightType >= 4)  // Only in pulse mode do we need to regularly update
    {
      updateNightLight();
    }

    if (UpdateEE)   // Don't need to check this more than 100 times/second.
    {
      EESaveSettings();
    }
  }

  // Check for alarm:
  if (milliTemp >= NextAlarmCheck)
  {
    NextAlarmCheck = milliTemp +  500;  // Check again in 1/2 second.

    if (AlarmEnabled)
    {
      byte hourTemp = hour();
      byte minTemp = minute();

      if ((AlarmTimeHr == hourTemp) && (AlarmTimeMin == minTemp))
      {
        if (alarmPrimed)
        {
          alarmPrimed = 0;
          alarmNow = 1;
          snoozed = 0;
          SoundSequence = 0;
        }
      }
      else
      {
        alarmPrimed = 1;
        // Prevent alarm from going off twice in the same minute, after being turned off and back on.
      }

      if (snoozed)
        if ((AlarmTimeSnoozeHr == hourTemp) && (AlarmTimeSnoozeMin == minTemp))
        {
          alarmNow = 1;
          snoozed = 0;
          SoundSequence = 0;
        }
    }
  }

  if (alarmNow)
  {
    previewActive = 0;   // The real alarm (or sound test) takes priority over a tone preview
    ManageAlarm();
  }
  else if (previewActive)
  {
    ManageTonePreview();
  }

  if (Serial.available())
  {
    processSerialMessage();
  }
}

#define a5_COMM_MSG_LEN  13   // time sync to PC is HEADER followed by unix time_t as ten ascii digits  (Was 11)
#define a5_COMM_HEADER  255   // Header tag for serial sync messages

void SerialSendDataDaisyChain(char DataIn[])
{
  char outputBuffer[13];
  char *toPtr = &outputBuffer[0];
  char *fromPtr = &DataIn[0];
  *toPtr++ = 255;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr++ = *fromPtr++;
  *toPtr = *fromPtr;
  // Explicit length: the buffer is binary data, not a NUL-terminated string.
  // Note that Serial1 is shared with the GPS module in this build.
  Serial1.write((uint8_t*)outputBuffer, 13);
}

void processSerialMessage()
{
  char c, c2;
  byte i, temp, temp2;
  int8_t paramNo, valueNo;
  char OutputCache[13];

  // if time sync available from serial port, update time and return true
  while (Serial.available() >=  a5_COMM_MSG_LEN)   // time message consists of a header and ten ascii digits
  {
    if (Serial.read() == a5_COMM_HEADER)
    {
      c = Serial.read() ;
      c2 = Serial.read();

      if (c == 'S')
      {
        if (c2 == 'T')
        {
          // COMMAND: ST, SET TIME
          time_t pctime = 0;

          for (i = 0; i < 10; i++)
          {
            c = Serial.read();

            if (c >= '0' && c <= '9')
            {
              pctime = (10 * pctime) + (c - '0') ; // convert digits to a number
            }
          }

          setTime(pctime);   // Sync Arduino clock to the time received on the serial port
          DisplayWord("SYNCD", 900);
          DisplayWordDP("____2");
          Serial.println("PC Time Sync Signal Received.");
          SerialPrintTime();

          if (UseRTC)
          {
            RTC.set(now());
          }

          EndVCRmode();
        }
      }
      else if (c == 'B')
      {
        if ((c2 == '0') || (c2 == 0)) // B0, with either ASCII or Binary 0.
        {
          // COMMAND: B0, Set Parameters
          c = Serial.read();   // B0 command: Which setting to adjust
          c2 = Serial.read();  // Read first char of additional data

          if (c == '2')
          {
            // edit font character
            // c2 : Idicates which ASCII character location to edit
            // Read in 8 more ASCII chars:
            // [___][_][___] <- "A", "B", "C" values, ASCII text
            i = 100 * (Serial.read() - '0');
            i += 10 * (Serial.read() - '0');
            i += (Serial.read() - '0');
            temp = (Serial.read() - '0');
            temp2 = 100 * (Serial.read() - '0');
            temp2 += 10 * (Serial.read() - '0');
            temp2 += (Serial.read() - '0');
            a5editFontChar(c2, i, temp, temp2);
            Serial.read();  // Empty input buffer, char 10 of 10
          }
          else
          {
            if (c == '0')
            {
              // Set brightness
              c = Serial.read();  // Read input buffer, char 3 of 10
              Brightness = (10 * (c2 - '0') + (c - '0'));
              UpdateBrightness = 1;

              if (schedulePhaseLast == 0)
              {
                DayBrightness = Brightness;
              }
            }

            if (c == '1')
            {
              // Load altnernate number set
              a5loadAltNumbers(c2 - '0');
              Serial.read();  // Empty input buffer, char 3 of 10
            }

            for (i = 3; i < 10; i++)
            {
              Serial.read();  // Empty input buffer
            }
          }

          RedrawNow = 1;
          EndVCRmode();
        }
        else   // Daisy chaining: With Bx, where x is less than 48 or x is less than 10:
        {
          if (c2 <= '9')
          {
            // if we're here, c2 is <= '9', c2 != 0, and c2 != '0'.
            OutputCache[0] = 'B';
            OutputCache[1] = c2 - 1;

            for (i = 2; i < 12; i++)
            {
              OutputCache[i] = Serial.read();
            }

            SerialSendDataDaisyChain(OutputCache);
          }
        }
      }
      else if (c == 'A')
      {
        if ((c2 == '0') || (c2 == 0)) // A0, with either ASCII or Binary 0.
        {
          // COMMAND: A0, DISPLAY ASCII DATA
          // ASCII display mode, first 5 chars will be displayed, second 5: decimals
          for (i = 0; i < 10; i++)
          {
            c = Serial.read();

            if (i < 5)
            {
              wordCache[i] = c;
            }
            else
            {
              dpCache[i - 5] = c;
            }
          }

          modeShowText = 3;
          RedrawNow = 1;
          EndVCRmode();
        }
        else   // Daisy chaining: With Ax, where x is less than 48 or x is less than 10:
        {
          if (c2 <= '9')
          {
            // if we're here, c2 is <= '9', c2 != 0, and c2 != '0'.
            OutputCache[0] = 'A';
            OutputCache[1] = c2 - 1;

            for (i = 2; i < 12; i++)
            {
              OutputCache[i] = Serial.read();
            }

            SerialSendDataDaisyChain(OutputCache);
          }
        }
      }
      else if (c == 'M')   // Mode setting commands
      {
        // Eventually, it would be nice to have all settings and functions
        // accessible through the remote interface.
        if (c2 == 'T')     // Command: 'MT' : Mode: Time
        {
          modeShowAlarmTime = 0;
          modeShowMenu = 0;
          modeShowText = 0;
          modeLEDTest = 0;
          EndVCRmode();
        }
      }
    }
  }
}

void updateNightLight(void)
{
  if (NightLightType == 4)
  {
    // "Sleep" mode
    unsigned int temp = 0;
    NightLightStep++;

    if (NightLightStep <= 255)
    {
      if (NightLightSign)
      {
        temp = NightLightStep;
      }
      else
      {
        temp = 255 - NightLightStep;
      }
    }
    else
    {
      if (NightLightSign)
      {
        temp = 255;
      }
      else
      {
        temp = 0;
      }

      if (NightLightStep > 280)
      {
        NightLightStep = 0;

        if (NightLightSign)
        {
          NightLightSign = 0;
        }
        else
        {
          NightLightSign = 1;
        }
      }
    }

    temp = (temp * temp) >> 8;

    if (temp > 252)
    {
      temp = 252;
    }

    temp += 3;
    a5nightLight(temp);
  }
  else if (NightLightType == 0)
  {
    a5nightLight(0);    // OFF
  }
  else if (NightLightType == 1)
  {
    a5nightLight(5);    // LOW
  }
  else if (NightLightType == 2)
  {
    a5nightLight(50);    // MED
  }
  else if (NightLightType == 3)
  {
    a5nightLight(255);    // HIGH
  }
}

void UpdateDisplay(byte forceUpdate)
{
  byte temp, remainder;

  if (modeShowText)  // Text Display
  {
    if ((milliTemp >= DisplayWordEndTime) && (modeShowText == 1))
    {
      modeShowText = 0;

      if (wordSequence)
      {
        DisplayWordSequence(wordSequence);
      }

      // If the word sequence is finished, return to clock display:
      if (wordSequence == 0)
      {
        RedrawNow = 1;
      }
    }
    else
    {
      if (forceUpdate)
      {
        a5clearOSB();
        a5loadOSB_Ascii(wordCache, a5_brightLevel);
        a5loadOSB_DP(dpCache, a5_brightLevel);
        a5BeginFadeToOSB();
      }
    }
  }
  else  if (modeLEDTest)  // LED Test Mode
  {
    if (milliTemp > DisplayWordEndTime)
    {
      forceUpdate = 1;
      SoundSequence++;
      DisplayWordEndTime = milliTemp + 350;  // Advance every 350 ms.
    }

    if (forceUpdate)
    {
      // Borrow SoundSequence as a dummy variable:
      if (SoundSequence > 91)
      {
        SoundSequence = 1;
      }

      remainder = SoundSequence - 1;
      temp = 4;

      while (remainder >= 18)
      {
        remainder -= 18;   // remainder
        temp -= 1;   // (4 - modulo)
      }

      byte map[] =
      {
        7, 0, 1, 10, 11, 3, 2, 12, 13, 14, 15, 16, 5, 17, 8, 9, 4, 6
      };
      a5clearOSB();
      a5_OSB[18 * temp + map[remainder]] = a5_brightLevel;
      a5BeginFadeToOSB();
      RedrawNow = 1;
    }
  }
  else if (modeShowMenu)
  {
    DisplayWordDP("_____");
    byte ExtendTextDisplay = 0;

    if (menuItem == AMPM24HRMenuItem)  // Hour mode: 12Hr / 24 Hr
    {
      if (optionValue != 0)
      {
        if (HourMode24)
        {
          HourMode24 = 0;
        }
        else
        {
          HourMode24 = 1;
        }

        optionValue = 0;
      }

      if (HourMode24)
      {
        DisplayWord("24 HR", 500);
      }
      else
      {
        DisplayWord("AM/PM", 500);
      }

      ExtendTextDisplay = 1;
    }
    else if (menuItem == NightLightMenuItem)  // Night Light
    {
      NightLightType += optionValue;

      if (NightLightType < 0)
      {
        NightLightType = 4;
      }

      if (NightLightType > 4)
      {
        NightLightType = 0;
      }

      if (optionValue != 0)
      {
        if (NightLightType == 4)
        {
          NightLightStep = 0;
          NightLightSign = 1;
        }

        updateNightLight();
      }

      optionValue = 0;

      if (NightLightType == 0)
      {
        DisplayWord(" NONE", 500);
      }
      else if (NightLightType == 1)
      {
        DisplayWord(" LOW ", 500);
      }
      else if (NightLightType == 2)
      {
        DisplayWord(" MED ", 500);
      }
      else if (NightLightType == 3)
      {
        DisplayWord(" HIGH", 500);
      }
      else  // (NightLightType == 4)
      {
        DisplayWord("SLEEP", 500);
      }

      ExtendTextDisplay = 1;
    }
    else if (menuItem == AlarmToneMenuItem)  // Alarm Tone: 2
    {
      if (optionValue != 0)
      {
        AlarmTone += optionValue;
        optionValue = 0;

        if (AlarmTone < 0)
        {
          AlarmTone = 5;
        }

        if (AlarmTone > 5)
        {
          AlarmTone = 0;
        }

        // Play a short preview of the newly selected tone
        a5noTone();
        previewStep = 0;
        previewActive = 1;
      }

      if (AlarmTone == 0)
      {
        DisplayWord("X LOW", 500);
      }
      else if (AlarmTone == 1)
      {
        DisplayWord(" LOW ", 500);
      }
      else if (AlarmTone == 2)
      {
        DisplayWord(" MED ", 500);
      }
      else if (AlarmTone == 3)
      {
        DisplayWord(" HIGH", 500);
      }
      else if (AlarmTone == 4)
      {
        DisplayWord("SIREN", 500);
      }
      else
      {
        DisplayWord(" TINK", 500);
      }

      ExtendTextDisplay = 1;
    }
    else if (menuItem == SoundTestMenuItem)  // Alarm Test: 3
    {
      DisplayWord(" +/- ", 500);

      if (optionValue != 0)
      {
        if (alarmNow == 0)
        {
          alarmNow = 1;
        }
        else
        {
          TurnOffAlarm();
        }

        optionValue = 0;
      }

      ExtendTextDisplay = 1;
    }
    else if (menuItem == numberCharSetMenuItem)
    {
      numberCharSet += optionValue;

      if (optionValue != 0)
      {
        optionValue = 0;

        if (numberCharSet < 0)
        {
          numberCharSet = 9;
        }

        if (numberCharSet > 9)
        {
          numberCharSet = 0;
        }

        a5loadAltNumbers(numberCharSet);
      }

      DisplayWord("01237", 500);  // Sample font display
      ExtendTextDisplay = 1;
    }
    else if (menuItem == DisplayStyleMenuItem)
    {
      temp = (DisplayMode & 3U);

      if (optionValue != 0)
      {
        if (optionValue == 1)
        {
          temp = (temp + 1) & 3U;
        }
        else if (temp == 0)
        {
          temp = 3;
        }
        else
        {
          temp--;
        }

        DisplayMode = (DisplayMode & 12U) | (temp);
        optionValue = 0;
        forceUpdate = 1;
      }

      TimeDisplay(DisplayMode & 3, forceUpdate); // Show clock time, in appropriate style
    }
    else if (menuItem == AltModeMenuItem)  // Alternate with seconds or date:
    {
      // if (TimeDisplay & 4): Alternate date with time
      // if (TimeDisplay & 8): Alternate date with seconds
      // if (TimeDisplay & 16): Alternate date with words
      if (optionValue != 0)
      {
        temp = 1;

        if (DisplayMode & 4)
        {
          temp = 2;
        }

        if (DisplayMode & 8)
        {
          temp = 3;
        }

        if (DisplayMode & 16)
        {
          temp = 4;
        }

        temp += optionValue;

        if (temp == 0)
        {
          temp = 4;    // Wrap around (low side)
        }
        else if (temp == 5)
        {
          temp = 0;    // wrap around (high side)
        }

        DisplayMode &= 3U;

        if (temp > 1)
        {
          DisplayMode |= (1 << temp);
        }

        // if temp is 0 or 1, display time only.
        DisplayModePhaseCount = 0;
        optionValue = 0;
      }

      if (DisplayMode & 4U)
      {
        DisplayWord("DATE ", 500);
      }
      else if (DisplayMode & 8U)
      {
        DisplayWord("SECS ", 500);
        DisplayWordDP("___1_");
      }
      else if (DisplayMode & 16U)
      {
        DisplayWord("WORDS", 500);
      }
      else
      {
        DisplayWord(" NONE", 500);
      }

      ExtendTextDisplay = 1;
    }
    else if (menuItem == GPSModeMenuItem)
    {
      if (optionValue != 0)
      {
        if (GPSMode)
        {
          GPSMode = 0;
        }
        else
        {
          GPSMode = 1;

          GPS.begin(9600);
          GPS.sendCommand(PMTK_SET_NMEA_OUTPUT_RMCGGA); // needs to be PMTK_SET_NMEA_OUTPUT_RMCGGA otherwise we don't get the number of satellites we can currently see
          GPS.sendCommand(PMTK_SET_NMEA_UPDATE_1HZ);    // 1, 5, 10 second GPS updates: PMTK_SET_NMEA_UPDATE_1HZ, PMTK_SET_NMEA_UPDATE_200_MILLIHERTZ, PMTK_SET_NMEA_UPDATE_100_MILLIHERTZ
          GPS.sendCommand(PGCMD_ANTENNA);
          // delay(1000);
          GPSSerial.println(PMTK_Q_RELEASE);
        }

        optionValue = 0;
      }

      if (GPSMode)
      {
        DisplayWord(" ON  ", 500);
      }
      else
      {
        DisplayWord(" OFF ", 500);
      }

      ExtendTextDisplay = 1;
    }
    else if (menuItem == BedtimeMenuItem)
    {
      if (optionValue != 0)
      {
        // Adjust bedtime in half-hour steps, wrapping between the limits
        if ((optionValue < 0) && (BedtimeMinutes <= BedtimeEarliestMinutes))
        {
          BedtimeMinutes = BedtimeLatestMinutes;
        }
        else if ((optionValue > 0) && (BedtimeMinutes >= BedtimeLatestMinutes))
        {
          BedtimeMinutes = BedtimeEarliestMinutes;
        }
        else
        {
          BedtimeMinutes += 30 * optionValue;
        }

        optionValue = 0;
        forceUpdate = 1;
      }

      TimeDisplay(22, forceUpdate); // Show bedtime, in clock-time style
    }
    else if (menuItem == SetYearMenuItem)
    {
      if (optionValue != 0)
      {
        AdjDayMonthYear(0, 0, optionValue); // Day, Month, Year
        optionValue = 0;
        forceUpdate = 1;
      }

      TimeDisplay(35, forceUpdate); // Show clock time, in appropriate style
    }
    else if (menuItem == SetMonthMenuItem)
    {
      if (optionValue != 0)
      {
        AdjDayMonthYear(0, optionValue, 0); // Day, Month, Year
        optionValue = 0;
        forceUpdate = 1;
      }

      TimeDisplay(33, forceUpdate); // Show clock time, in appropriate style
    }
    else if (menuItem == SetDayMenuItem)
    {
      if (optionValue != 0)
      {
        AdjDayMonthYear(optionValue, 0, 0); // Day, Month, Year
        optionValue = 0;
        forceUpdate = 1;
      }

      TimeDisplay(33, forceUpdate); // Show clock time, in appropriate style
    }
    else if (menuItem == SetSecondsMenuItem)
    {
      if (optionValue != 0)
      {
        adjustTime(optionValue); // Adjust by +/- 1 second

        if (UseRTC)
        {
          RTC.set(now());
        }

        optionValue = 0;
        forceUpdate = 1;
      }

      TimeDisplay(32, forceUpdate); // Show clock time, seconds
    }

    if (forceUpdate && ExtendTextDisplay)
    {
      if (menuItem != DisplayStyleMenuItem)
      {
        a5clearOSB();
        a5loadOSB_Ascii(wordCache, a5_brightLevel);
        a5loadOSB_DP(dpCache, a5_brightLevel);
        a5BeginFadeToOSB();
      }
    }
  }
  else if (modeShowDateViaButtons)
  {
    TimeDisplay(33, forceUpdate); // Show date
  }
  else if (modeShowAlarmTime)
  {
    TimeDisplay(20, forceUpdate); // Show alarm time
  }
  else
  {
    // Time Display Mode!  Possibly with aux. display.
    if ((DisplayMode > 3) && (DisplayMode < 32))
    {
      if (buttonMonitor)
      {
        // Do not use alternate display modes when buttons are pressed.
        DisplayModePhase = 0;
        DisplayModePhaseCount = 0;
      }
      else if (DisplayModePhaseCount >= 7) // Alternate display every 7 seconds
      {
        DisplayModePhaseCount = 0;
        DisplayModePhase++;

        if (DisplayModePhase > 1)
        {
          DisplayModePhase = 0;
        }

        forceUpdate = 1;
        DisplayWord("     ", 400);   // Blank out between display phases

        if (AlarmEnabled)
        {
          DisplayWordDP("2____");
        }
        else
        {
          DisplayWordDP("_____");
        }
      }

      if (DisplayModePhase == 0)
      {
        TimeDisplay(DisplayMode & 3, forceUpdate);  // "Normal" time display
      }
      else
      {
        // Alternate display modes: "Time and ... "
        if (DisplayMode & 4)
        {
          TimeDisplay(33, forceUpdate);    // if Bit 4 is set: Show date
        }
        else if (DisplayMode & 8)
        {
          TimeDisplay(32, forceUpdate);    // if Bit 8 is set: Show seconds
        }
        else if (DisplayMode & 16)
        {
          TimeDisplay(36, forceUpdate);    // if Bit 16 is set:  Show five letter words
        }
      }
    }
    else
    {
      TimeDisplay(DisplayMode, forceUpdate);
    }

    SpecialOccasionMessage();
  }
}

void AdjDayMonthYear(int8_t AdjDay, int8_t AdjMonth, int8_t AdjYear)
{
  // From Time library: API starts months from 1, this array starts from 0
  const uint8_t monthDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  time_t timeTemp = now();
  int yrTemp = year(timeTemp) + (int) AdjYear;
  int moTemp = month(timeTemp) + AdjMonth;  // Avoid changing year, unless requested

  if (moTemp < 1)
  {
    moTemp = 12;
  }

  if (moTemp > 12)
  {
    moTemp = 1;
  }

  byte daysInMonth = monthDays[moTemp - 1];

  if ((moTemp == 2) && (((yrTemp % 4 == 0) && (yrTemp % 100 != 0)) || (yrTemp % 400 == 0)))
  {
    daysInMonth = 29;   // Leap year
  }

  int dayTemp = day(timeTemp) + AdjDay;  // avoid changing month, unless requested

  if (dayTemp < 1)
  {
    dayTemp = daysInMonth;
  }

  if (dayTemp > daysInMonth)
    if (AdjDay > 0)
    {
      // Roll over day-of-month to 1, if explicitly requesting increase in date.
      dayTemp = 1;
    }
    else
    {
      // Otherwise, we should "truncate" the date to last day of month.
      dayTemp = daysInMonth;
    }

  setTime(hour(timeTemp), minute(timeTemp), second(timeTemp),
          dayTemp, moTemp, yrTemp);

  if (UseRTC)
  {
    RTC.set(now());
  }
}

void TimeDisplay(byte DisplayModeLocal, byte forceUpdateCopy)
{
  byte temp;
  char units;
  char WordIn[] =
  {
    "     "
  };
  byte SecNowTens,  SecNowOnes;
  byte SecNow;
  SecNow = second();

  if (SecLast != SecNow)
  {
    forceUpdateCopy = 1;
    DisplayModePhaseCount++;
  }

  if ((DisplayModeLocal <= 4) || (DisplayModeLocal == 20) || (DisplayModeLocal == 22))
  {
    // Normal time display OR Alarm time display
    // DisplayModeLocal 0: Standard-mode time-of-day display
    // DisplayModeLocal 1: Time-of-day w/ seconds spinner
    // DisplayModeLocal 2: Standard-mode time-of-day display + flashing separator
    // DisplayModeLocal 3: Time-of-day w/ seconds spinner + flashing separator
    // DisplayModeLocal 20: Standard-mode alarm-time display
    // DisplayModeLocal 22: Bedtime display (for the config menu)
    byte HrNowTens,  HrNowOnes, MinNowTens,  MinNowOnes;

    if (DisplayModeLocal == 20)
    {
      temp = AlarmTimeHr;
    }
    else if (DisplayModeLocal == 22)
    {
      temp = BedtimeMinutes / 60;
    }
    else
    {
      temp = hour();
    }

    if (HourMode24)
    {
      units = 'H';
    }
    else
    {
      units = 'A';

      if (temp >= 12)
      {
        units = 'P';
      }

      if (temp > 12)
      {
        temp -= 12;   //
      }

      if (temp == 0)  // Represent 00:00 as 12:00
      {
        temp += 12;
      }
    }

    HrNowTens = U8DIVBY10(temp);    // i.e.,  HrNowTens = temp / 10;
    HrNowOnes = temp - 10 * HrNowTens;

    if (DisplayModeLocal == 20)
    {
      temp = AlarmTimeMin;
    }
    else if (DisplayModeLocal == 22)
    {
      temp = BedtimeMinutes % 60;
    }
    else
    {
      temp = minute();
    }

    MinNowTens = U8DIVBY10(temp);      // i.e.,  MinNowTens = temp / 10;
    MinNowOnes = temp - 10 * MinNowTens;

    if (MinNowOnesLast != MinNowOnes)
    {
      forceUpdateCopy = 1;
    }

    SecNow = second();

    if (SecLast != SecNow)
    {
      forceUpdateCopy = 1;
    }

    if (DisplayModeLocal & 1) // Seconds Spinner Mode
    {
      // binary tree for 8 cases:  three tests max, rather than 8.
      // Split seconds into octants: 0-6,7-14,15-22,23-29,30-36,37-44,45-52,53-59
      if (SecNow < 30)
      {
        // temp in range 0-29
        if (SecNow < 15)
        {
          // temp in range 0-14
          if (SecNow < 7)
          {
            // temp in range 0-6
            temp = 15;   //  a5editFontChar('a',0,0,32);    // N arrow
          }
          else
          {
            // temp in range 7-14
            temp = 16; // a5editFontChar('a',0,0,64);    // NE arrow
          }
        }
        else
        {
          // temp in range 15-29
          if (SecNow < 23)
          {
            // temp in range 15-22
            temp = 5;  // a5editFontChar('a',32,0,0);    // E arrow
          }
          else
          {
            // temp in range 23-29
            temp = 17;  // a5editFontChar('a',0,0,128);    // SE arrow
          }
        }
      }
      else
      {
        // temp in range 30-59
        if (SecNow < 45)
        {
          // temp in range 30-44
          if (SecNow < 37)
          {
            // temp in range 30-36
            temp = 8;  // a5editFontChar('a',0,1,0);    // S arrow
          }
          else
          {
            // temp in range 37-44
            temp = 9;  // a5editFontChar('a',0,2,0);    // SW arrow
          }
        }
        else
        {
          // temp in range 45-59
          if (SecNow < 53)
          {
            // temp in range 45-52
            temp = 4;  //  a5editFontChar('a',16,0,0);    // W arrow
          }
          else
          {
            // temp in range 53-59
            temp = 14;  // a5editFontChar('a',0,0,16);    // NW arrow
          }
        }
      }
    }

    if ((HourMode24) || (HrNowTens > 0))
    {
      WordIn[0] =  HrNowTens + a5_integerOffset;    // Blank leading zero unless in 24-hour mode.
    }

    WordIn[1] =  HrNowOnes  + a5_integerOffset;
    WordIn[2] =  MinNowTens + a5_integerOffset;
    WordIn[3] =  MinNowOnes + a5_integerOffset;

    if (DisplayModeLocal & 1)  // Spinner
    {
      WordIn[4] =  ' ';
    }
    else
    {
      WordIn[4] =  units;
    }

    if (forceUpdateCopy)
    {
      a5clearOSB();
      a5loadOSB_Ascii(WordIn, a5_brightLevel);

      if (DisplayModeLocal & 1)
      {
        a5loadOSB_Segment(temp, a5_brightLevel);

        if (units == 'P')
        {
          a5loadOSB_DP("___1_", a5_brightLevel);    // DP dot in DisplayMode 1.
        }
      }

      if (GPSMode)
      {
        // Blink the rightmost bottom decimal point to show GPS fix status
        // Once every other second if there's no fix
        if (!(GPS.fix) && SecNow % 2 == 0)
        {
          a5loadOSB_DP("____1", a5_brightLevel);
        }

        // Once every 15 seconds if there's a GPS fix
        // else if (GPS.fix && SecNow % 15 == 0) {
        //   a5loadOSB_DP("____1",a5_brightLevel);
        // }
      }

      if (AlarmEnabled)
      {
        a5loadOSB_DP("2____", a5_brightLevel);
      }

      if ((DisplayModeLocal < 20) && (DisplayModeLocal & 2) && (SecNow & 1))
      {
        // no HOUR:MINUTE separators
      }
      else
      {
        a5loadOSB_DP("01200", a5_brightLevel);
      }

      a5BeginFadeToOSB();
    }

    MinNowOnesLast = MinNowOnes;
  }
  else if (DisplayModeLocal == 32)  // Seconds only
  {
    temp = SecNow;
    SecNowTens = U8DIVBY10(temp);      // i.e.,  SecNowTens = temp / 10;
    SecNowOnes = temp - 10 * SecNowTens;
    WordIn[2] =  SecNowTens + a5_integerOffset;
    WordIn[3] =  SecNowOnes + a5_integerOffset;

    if (forceUpdateCopy)
    {
      a5clearOSB();
      a5loadOSB_Ascii(WordIn, a5_brightLevel);

      if (AlarmEnabled)
      {
        a5loadOSB_DP("21200", a5_brightLevel);
      }
      else
      {
        a5loadOSB_DP("01200", a5_brightLevel);
      }

      a5BeginFadeToOSB();
    }

    SecLast = SecNow;
  }
  else if (DisplayModeLocal == 33)  // Month, Day
  {
    if (forceUpdateCopy)
    {
      temp = day();
      byte monthTemp = 3 * (month() - 1);
      // Month name (short):
      //      char a5monthShortNames_P[] PROGMEM = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";
      WordIn[0] = pgm_read_byte(&(a5_monthShortNames_P[monthTemp++]));
      WordIn[1] = pgm_read_byte(&(a5_monthShortNames_P[monthTemp++]));
      WordIn[2] = pgm_read_byte(&(a5_monthShortNames_P[monthTemp]));
      byte divtemp =  U8DIVBY10(temp);  // i.e.,  divtemp = day / 10;
      WordIn[3] =   divtemp + a5_integerOffset;
      WordIn[4] = (temp - 10 * divtemp) + a5_integerOffset;
      a5clearOSB();
      a5loadOSB_Ascii(WordIn, a5_brightLevel);

      if (AlarmEnabled)
      {
        a5loadOSB_DP("20100", a5_brightLevel);
      }
      else
      {
        a5loadOSB_DP("00100", a5_brightLevel);
      }

      a5BeginFadeToOSB();
    }

    SecLast = SecNow;
  }
  else if (DisplayModeLocal == 35)  // Year
  {
    unsigned int yeartemp = year();
    unsigned int divtemp =  U16DIVBY10(yeartemp);  // i.e.,  divtemp = yeartemp / 10;
    WordIn[4] =   yeartemp - 10 * divtemp + a5_integerOffset;
    yeartemp = U16DIVBY10(divtemp);
    WordIn[3] =   divtemp - 10 * yeartemp + a5_integerOffset;
    divtemp =  U16DIVBY10(yeartemp);
    WordIn[2] =   yeartemp - 10 * divtemp + a5_integerOffset;
    yeartemp = U16DIVBY10(divtemp);
    WordIn[1] =   divtemp - 10 * yeartemp + a5_integerOffset;

    if (forceUpdateCopy)
    {
      a5clearOSB();
      a5loadOSB_Ascii(WordIn, a5_brightLevel);

      if (AlarmEnabled)
      {
        a5loadOSB_DP("20000", a5_brightLevel);
      }
      else
      {
        a5loadOSB_DP("00000", a5_brightLevel);
      }

      a5BeginFadeToOSB();
    }
  }
  else if (DisplayModeLocal == 36)  // FLW - FIVE LETTER WORD mode
  {
    if (forceUpdateCopy)
    {
      if (DisplayModeLocalLast != 36)
      {
        // Pick new display word, but only when first entering mode 36.
        // Uncomment exactly one of the following two lines:
        FLWoffset = random(fiveLetterWordsMax);  // Random word order!
        // FLWoffset += 1;  // Alphebetical word order!
      }

      if (FLWoffset >= fiveLetterWordsMax)
      {
        FLWoffset = 0;
      }

      unsigned int index = 4 * FLWoffset;
      WordIn[1] = pgm_read_byte(&(fiveLetterWords[index++]));
      WordIn[2] = pgm_read_byte(&(fiveLetterWords[index++]));
      WordIn[3] = pgm_read_byte(&(fiveLetterWords[index++]));
      WordIn[4] = pgm_read_byte(&(fiveLetterWords[index]));
      temp = 0;

      while (temp < 25)
      {
        index = pgm_read_word(&(fiveLetterPosArray[temp]));

        if (FLWoffset < index)
        {
          WordIn[0] = 'A' + temp;
          temp = 50;
        }

        temp++;
      }

      if (temp < 50)
      {
        WordIn[0] = 'Z';
      }

      a5clearOSB();
      a5loadOSB_Ascii(WordIn, a5_brightLevel);

      if (AlarmEnabled)
      {
        a5loadOSB_DP("20000", a5_brightLevel);
      }
      else
      {
        a5loadOSB_DP("00000", a5_brightLevel);
      }

      a5BeginFadeToOSB();
    }
  }

  DisplayModeLocalLast = DisplayModeLocal;
  SecLast = SecNow;
}

void SerialPrintTime()
{
  //   Print time over serial interface.   Adapted from Time library.
  time_t timeTmp = now();
  Serial.print(hour(timeTmp));
  printDigits(minute(timeTmp));
  printDigits(second(timeTmp));
  Serial.print(" ");
  Serial.print(dayStr(weekday(timeTmp)));
  Serial.print(" ");
  Serial.print(day(timeTmp));
  Serial.print(" ");
  Serial.print(monthShortStr(month(timeTmp)));
  Serial.print(" ");
  Serial.print(year(timeTmp));
  Serial.println();
}

void printDigits(int digits)
{
  // utility function for digital clock serial output: prints preceding colon and leading 0
  // borrowed from Time library.
  Serial.print(":");

  if (digits < 10)
  {
    Serial.print('0');
  }

  Serial.print(digits);
}

void ApplyDefaults(void)
{
  // VARIABLES THAT HAVE EEPROM STORAGE AND DEFAULTS...
  a5_brightLevel =  a5brightLevelDefault;
  DayBrightness =   a5brightLevelDefault;
  HourMode24 =      a5HourMode24Default;
  AlarmEnabled =    a5AlarmEnabledDefault;
  AlarmTimeHr =     a5AlarmHrDefault;
  AlarmTimeMin =    a5AlarmMinDefault;
  AlarmTone =       a5AlarmToneDefault;
  NightLightType =  a5NightLightTypeDefault;
  numberCharSet =   a5NumberCharSetDefault;
  GPSMode =         a5GPSModeDefault;
  BedtimeMinutes =  a5BedtimeDefault;
}

void EEReadSettings(void)
{
  // Check values for sanity at THIS stage.
  byte value = 255;
  value = EEPROM.read(0);

  if ((value > 100 + BrightnessMax) || (value < 100))
  {
    DayBrightness = a5brightLevelDefault;
  }
  else
  {
    DayBrightness = value - 100;
  }

  Brightness = DayBrightness;

  value = EEPROM.read(1);

  if (value > 1)
  {
    HourMode24 = a5HourMode24Default;
  }
  else
  {
    HourMode24 = value;
  }

  value = EEPROM.read(2);

  if (value > 1)
  {
    AlarmEnabled = a5AlarmEnabledDefault;
  }
  else
  {
    AlarmEnabled = value;
  }

  value = EEPROM.read(3);

  if ((value > 123) || (value < 100))
  {
    AlarmTimeHr = a5AlarmHrDefault;
  }
  else
  {
    AlarmTimeHr = value - 100;
  }

  value = EEPROM.read(4);

  if ((value > 159) || (value < 100))
  {
    AlarmTimeMin = a5AlarmMinDefault;
  }
  else
  {
    AlarmTimeMin = value - 100;
  }

  value = EEPROM.read(5);

  if (value > 5)
  {
    AlarmTone = a5AlarmToneDefault;
  }
  else
  {
    AlarmTone = value;
  }

  value = EEPROM.read(6);

  if (value > 4)
  {
    NightLightType = a5NightLightTypeDefault;
  }
  else
  {
    NightLightType = value;
  }

  value = EEPROM.read(7);

  if (value > 9)
  {
    numberCharSet = a5NumberCharSetDefault;
  }
  else
  {
    numberCharSet = value;
  }

  value = EEPROM.read(8);

  if (value > 31)
  {
    DisplayMode = a5DisplayModeDefault;
  }
  else
  {
    DisplayMode = value;
  }

  value = EEPROM.read(9);

  if (value > 1)
  {
    GPSMode = a5GPSModeDefault;
  }
  else
  {
    GPSMode = value;
  }

  // Note: EEPROM addresses 10-15 hold the cached GPS location (see EEReadLocation).

  value = EEPROM.read(16);  // Bedtime, stored as half-hours past midnight

  if ((value < (BedtimeEarliestMinutes / 30)) || (value > (BedtimeLatestMinutes / 30)))
  {
    BedtimeMinutes = a5BedtimeDefault;
  }
  else
  {
    BedtimeMinutes = value * 30;
  }
}

void EESaveSettings(void)
{
  // If > 4 seconds since last button press, and
  // we suspect that we need to change the stored settings:
  byte value;
  byte indicateEEPROMwritten = 0;

  if (milliTemp >= (LastButtonPress + 4000))
  {
    // Careful if you use this function: EEPROM has a limited number of write
    // cycles in its life.  Good for human-operated buttons, bad for automation.
    // Also, no error checking is provided at this, the write EEPROM stage.
    value = EEPROM.read(0);

    // Save the daytime setting, never the live (possibly night-dimmed) brightness
    if (DayBrightness != (value - 100))
    {
      a5writeEEPROM(0, DayBrightness + 100);
      // NOTE:  Do not blink LEDs off to indicate saving of this value
    }

    value = EEPROM.read(1);

    if (HourMode24 != value)
    {
      a5writeEEPROM(1, HourMode24);
      indicateEEPROMwritten = 1;
    }

    value = EEPROM.read(2);

    if (AlarmEnabled != value)
    {
      a5writeEEPROM(2, AlarmEnabled);
      // NOTE:  Do not blink LEDs off to indicate saving of this value
    }

    value = EEPROM.read(3);

    if (AlarmTimeHr != (value - 100))
    {
      a5writeEEPROM(3, AlarmTimeHr + 100);
      // NOTE:  Do not blink LEDs off to indicate saving of this value
    }

    value = EEPROM.read(4);

    if (AlarmTimeMin != (value - 100))
    {
      a5writeEEPROM(4, AlarmTimeMin + 100);
      // NOTE:  Do not blink LEDs off to indicate saving of this value
    }

    value = EEPROM.read(5);

    if (AlarmTone != value)
    {
      a5writeEEPROM(5, AlarmTone);
      indicateEEPROMwritten = 1;
    }

    value = EEPROM.read(6);

    if (NightLightType != value)
    {
      a5writeEEPROM(6, NightLightType);
      indicateEEPROMwritten = 1;
    }

    value = EEPROM.read(7);

    if (numberCharSet != value)
    {
      a5writeEEPROM(7, numberCharSet);
      indicateEEPROMwritten = 1;
    }

    value = EEPROM.read(8);

    if (DisplayMode != value)
    {
      a5writeEEPROM(8, DisplayMode);
      indicateEEPROMwritten = 1;
    }

    value = EEPROM.read(9);

    if (GPSMode != value)
    {
      a5writeEEPROM(9, GPSMode);
      indicateEEPROMwritten = 1;
    }

    value = EEPROM.read(16);

    if ((BedtimeMinutes / 30) != value)
    {
      a5writeEEPROM(16, BedtimeMinutes / 30);
      indicateEEPROMwritten = 1;
    }

    if (indicateEEPROMwritten)   // Blink LEDs off to indicate when we're writing to the EEPROM
    {
      DisplayWord("     ", 100);
    }

    UpdateEE = 0;

    if (UseRTC)
    {
      RTC.set(now());    // Update time at RTC, in case time was changed in settings menu
    }
  }
}
