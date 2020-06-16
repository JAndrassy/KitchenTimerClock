#include <TM1637Display.h> // author Avishay Orpaz
#include <Encoder.h> // author Paul Stoffregen
#include <Bounce2.h> // maintainer Thomas O Fredericks
#include <toneAC.h> // author Tim Eckel
#include <Wire.h>
#include <DS3231.h> // maintainer Andrew Wickert

const byte BUTTON_PIN = 2;
const byte ENCODER_PIN_A = 3; // external interrupt pin
const byte ENCODER_PIN_B = 4;
const byte DISPLAY_DATA_PIN = 5;
const byte DISPLAY_SCLK_PIN = 6;
const byte LDR_PIN = A2;

const byte ENCODER_PULSES_PER_STEP = 4;
const unsigned DISPLAY_REFRESH_INTERVAL = 100; // milliseconds
const unsigned LONG_PUSH_INTERVAL = 3000; // miliseconds
const unsigned SET_TIME_BLINK_MILLIS = 250;
const unsigned ALARM_BLINK_MILLIS = 700;
const unsigned BELL_REPEAT_INTERVAL = 60000; // miliseconds
const unsigned TIMER_DISPLAY_TIMEOUT = 60000; // miliseconds
const unsigned MINUTE_MILIS = 60000 - 6;
const unsigned CLOCK_MILIS_CORRECTION_PER_HOUR = 41;

enum {
  CLOCK,
  SET_HOUR,
  SET_MINUTE,
  SET_TIMER,
  COUNTDOWN,
  ALARM
};

TM1637Display display(DISPLAY_SCLK_PIN, DISPLAY_DATA_PIN);
Encoder encoder(ENCODER_PIN_A, ENCODER_PIN_B);
Bounce button;

byte clockHour = 0;
byte clockMinute = 0;
bool rtcPresent = true;
unsigned long minuteMillis;
unsigned timerSeconds = 0;
byte displayData[4];

void setup() {
  button.attach(BUTTON_PIN, INPUT_PULLUP);
  pinMode(LED_BUILTIN, OUTPUT);
  Wire.begin();
  loadRTCTime();
}

void loop() {

  static unsigned long previousMillis;
  static unsigned long displayRefreshMillis;
  static unsigned long displayTimeoutMillis;
  static unsigned long alarmStartMillis;
  static unsigned timerStartSeconds;
  static byte blink;
  static byte state = CLOCK;

  unsigned long currentMillis = millis();

  // clock
  if (state == SET_HOUR || state == SET_MINUTE) {
    if (currentMillis - previousMillis >= SET_TIME_BLINK_MILLIS) {
      previousMillis = currentMillis;
      blink = !blink;
      if (state == SET_HOUR) {
        showClock(true, blink, true);
      } else {
        showClock(true, true, blink);
      }
    }
  } else if (currentMillis - minuteMillis > MINUTE_MILIS) {
    minuteMillis += MINUTE_MILIS;
    if (clockMinute < 59) {
      clockMinute++;
    } else {
      clockMinute = 0;
      clockHour = (clockHour < 23) ? (clockHour + 1) : 0;
      minuteMillis -= CLOCK_MILIS_CORRECTION_PER_HOUR;
      saveTimeToRTC();
    }
  }

  // every second
  if (currentMillis - previousMillis >= 1000) {
    previousMillis += 1000;
    switch (state) {
      case COUNTDOWN:
        if (timerSeconds > 0) {
          timerSeconds--;
          showTimer();
          if (timerSeconds == 0) {
            state = ALARM;
            alarmStartMillis = 0;
          }
        }
        break;
      case CLOCK:
        blink = !blink;
        showClock(blink, true, true); // update time display and blink the colon
        break;
    }
    if (!rtcPresent) {
      digitalWrite(LED_BUILTIN, blink);
    }
  }

  // encoder
  int dir = encoder.read();
  if (abs(dir) >= ENCODER_PULSES_PER_STEP) {
    if (state == SET_HOUR) {
      if (dir > 0) {
        clockHour = (clockHour < 23) ? (clockHour + 1) : 0;
      } else {
        clockHour = (clockHour > 0) ? (clockHour - 1) : 23;
      }
    } else if (state == SET_MINUTE) {
      if (dir > 0) {
        clockMinute = (clockMinute < 59) ? (clockMinute + 1) : 0;
      } else {
        clockMinute = (clockMinute > 0) ? (clockMinute - 1) : 59;
      }
    } else {
      displayTimeoutMillis = 0; // reset timeout
      byte step;
      if (timerSeconds + dir > 6 * 60) {
        step = 60;
      } else if (timerSeconds + dir > 3 * 60) {
        step = 30;
      } else if (timerSeconds + dir > 60) {
        step = 15;
      } else if (timerSeconds == 0) {
        step = 10;
      } else {
        step = 5;
      }
      if (dir > 0) {
        if (state != COUNTDOWN) {
          step = step - (timerSeconds % step);
        }
        if (timerSeconds + step < 6000) { // 100 minutes
          timerSeconds += step;
          showTimer();
        }
      } else {
        if (state != COUNTDOWN) {
          int m = timerSeconds % step;
          if (m != 0) {
            step = m;
          }
        }
        if (timerSeconds >= step) {
          timerSeconds -= step;
          showTimer();
        }
      }
      if (state != COUNTDOWN && dir > 0) {
        state = SET_TIMER;
      }
    }
    encoder.write(0);
  }

  // button
  static unsigned long buttonPushedMillis;
  button.update();
  if (button.fell()) {
    buttonPushedMillis = currentMillis;
  }
  if (buttonPushedMillis && currentMillis - buttonPushedMillis > LONG_PUSH_INTERVAL) {
    buttonPushedMillis = 0;
    displayTimeoutMillis = 0;
    switch (state) {
      case SET_TIMER:
        timerSeconds = 0;
        timerStartSeconds = 0;
        showTimer();
        break;
      case CLOCK:
        state = SET_HOUR;
        break;
    }
  }
  if (button.rose() && buttonPushedMillis != 0) {
    buttonPushedMillis = 0;
    displayTimeoutMillis = 0;
    switch (state) {
      case SET_HOUR:
        state = SET_MINUTE;
        break;
      case SET_MINUTE:
        state = CLOCK;
        minuteMillis = millis();
        saveTimeToRTC();
        break;
      case CLOCK:
        state = SET_TIMER;
        showTimer();
        break;
      case COUNTDOWN:
        state = SET_TIMER;
        break;
      case ALARM:
        state = SET_TIMER;
        timerSeconds = timerStartSeconds;
        showTimer();
        break;
      case SET_TIMER:
        if (timerSeconds > 0) {
          state = COUNTDOWN;
          previousMillis = millis();
          if (timerStartSeconds < timerSeconds) {
            timerStartSeconds = timerSeconds;
          }
        } else {
          state = CLOCK;
          timerStartSeconds = 0;
        }
        break;
    }
  }

  // timer display timeout
  if (state == SET_TIMER) {
    if (displayTimeoutMillis == 0) {
      displayTimeoutMillis = currentMillis;
    }
    if ((currentMillis - displayTimeoutMillis > TIMER_DISPLAY_TIMEOUT)) {
      displayTimeoutMillis = 0;
      state = CLOCK;
      timerSeconds = 0;
      timerStartSeconds = 0;
    }
  }

  // alarm
  if (state == ALARM) {
    if (currentMillis - previousMillis >= ALARM_BLINK_MILLIS) {
      previousMillis = currentMillis;
      blink = !blink;
    }
    bool resetBell = false;
    if (currentMillis - alarmStartMillis > BELL_REPEAT_INTERVAL || !alarmStartMillis) {
      alarmStartMillis = currentMillis;
      resetBell = true;
    }
    bellSound(resetBell);
  }

  // display refresh
  if (currentMillis - displayRefreshMillis > DISPLAY_REFRESH_INTERVAL) {
    displayRefreshMillis = currentMillis;
    static int lastLDRReading = 1300; // init out of range
    if (state == ALARM) {
      display.setBrightness(7, blink);
      lastLDRReading = 1300;
    } else {
      int a = analogRead(LDR_PIN);
      if (abs(a - lastLDRReading) > 20) {
        lastLDRReading = a;
        byte brightness = map(a, 0, 1024, 1, 8);
        display.setBrightness(brightness, true);
      }
    }
    display.setSegments(displayData);
  }

}

void loadRTCTime() {
  DateTime now = RTClib::now();
  if (now.hour() > 23) {
    rtcPresent = false;
    return;
  }
  clockHour = now.hour();
  clockMinute = now.minute();
  minuteMillis = millis() - (1000L * now.second());
}

void saveTimeToRTC() {
  if (!rtcPresent)
    return;
  DS3231 rtc;
  rtc.setSecond(0);
  rtc.setMinute(clockMinute);
  rtc.setHour(clockHour);
}

void showClock(bool showColon, bool showHour, bool showMinute) {

  byte digitBuffer[4];
  digitBuffer[0] = clockHour / 10;
  digitBuffer[1] = clockHour % 10;
  digitBuffer[2] = clockMinute / 10;
  digitBuffer[3] = clockMinute % 10;

  refreshDisplay(digitBuffer, showColon, showHour, showMinute);
}

void showTimer() {

  byte minutes = timerSeconds / 60;
  byte secs = timerSeconds % 60;

  byte digitBuffer[4];
  digitBuffer[0] = minutes / 10;
  digitBuffer[1] = minutes % 10;
  digitBuffer[2] = secs / 10;
  digitBuffer[3] = secs % 10;

  refreshDisplay(digitBuffer, true, true, true);
}

void refreshDisplay(byte digitBuffer[4], bool showColon, bool showLeft, bool showRight) {
  if (showLeft) {
    displayData[0] = !digitBuffer[0] ? 0 : display.encodeDigit(digitBuffer[0]);
    displayData[1] = display.encodeDigit(digitBuffer[1]);
  } else {
    displayData[0] = 0;
    displayData[1] = 0;
  }
  if (showRight) {
    displayData[2] = display.encodeDigit(digitBuffer[2]);
    displayData[3] = display.encodeDigit(digitBuffer[3]);
  } else {
    displayData[2] = 0;
    displayData[3] = 0;
  }
  if (showColon) {
    displayData[1] |= 0x80;
  }
}

void bellSound(bool restart) {
  const byte REPEAT_COUNT = 3;
  const int BELL_FREQUENCY = 2093; // C7
  const unsigned short LENGTH = 1200;
  const byte VOLUME_STEPS = 9;
  const unsigned short STEP_LENGTH = LENGTH / VOLUME_STEPS;

  static byte volume = VOLUME_STEPS;
  static byte count = 0;
  static unsigned long previousMillis;

  if (restart) {
    count = 0;
    volume = VOLUME_STEPS;
  }
  if (count == REPEAT_COUNT)
    return;
  unsigned long currentMillis = millis();
  if (currentMillis - previousMillis > STEP_LENGTH) {
    previousMillis = currentMillis;
    toneAC(BELL_FREQUENCY, volume, STEP_LENGTH * 2, true);
    volume--;
    if (volume == 0) {
      volume = VOLUME_STEPS;
      count++;
    }
  }
}
