/*
 * diskfan_controller
 *
 * Drives 2x Noctua 5V PWM fans (wired via a Y-splitter) from PWM pin 9
 * of an Arduino Uno. Fan speed is set over USB serial as a plaintext
 * integer percentage (0-100) followed by a newline, sent by
 * diskfanspeedd running on the host.
 *
 * Protocol (host -> Arduino), one command per line, newline terminated:
 *   "<0-100>"  set fan duty cycle to this percent, replies "OK <pct>"
 *   "?"        query current duty cycle, replies "PWM <pct>"
 *
 * Fail-safe: Noctua fans need real cooling behind them. If no valid
 * command is received for FAILSAFE_TIMEOUT_MS, the controller assumes
 * the host/daemon has died or the USB link dropped and forces the fans
 * to FAILSAFE_PWM (full speed) until a new command arrives. The same
 * default applies at boot, before the daemon has connected.
 */

#include <Arduino.h>

const uint8_t FAN_PIN = 9;              // OC1A on Uno
const unsigned long FAILSAFE_TIMEOUT_MS = 90000UL; // > 2x expected 30s poll interval
const uint8_t FAILSAFE_PWM = 100;

unsigned long lastCommandMillis = 0;
uint8_t currentPwmPercent = FAILSAFE_PWM;
String lineBuf;

// 25kHz fast PWM on Timer1 (pin 9 = OC1A), matching the Noctua PWM spec.
// 16MHz / (prescaler=1 * (ICR1+1)) = 25kHz  =>  ICR1 = 639
void setupPwm25kHz() {
  pinMode(FAN_PIN, OUTPUT);

  TCCR1A = 0;
  TCCR1B = 0;
  TCNT1 = 0;

  ICR1 = 639; // TOP, defines 25kHz period

  // Fast PWM, TOP = ICR1 (mode 14), non-inverting output on OC1A
  TCCR1A = _BV(COM1A1) | _BV(WGM11);
  TCCR1B = _BV(WGM13) | _BV(WGM12) | _BV(CS10); // prescaler = 1

  OCR1A = 0;
}

void setDutyPercent(uint8_t pct) {
  if (pct > 100) pct = 100;
  currentPwmPercent = pct;
  OCR1A = (uint32_t)ICR1 * pct / 100;
}

void setup() {
  Serial.begin(9600);
  setupPwm25kHz();
  setDutyPercent(FAILSAFE_PWM); // safe default until host takes over
  lineBuf.reserve(16);
}

void handleLine(const String &line) {
  String cmd = line;
  cmd.trim();
  if (cmd.length() == 0) return;

  if (cmd == "?") {
    Serial.print("PWM ");
    Serial.println(currentPwmPercent);
    return;
  }

  bool numeric = true;
  for (unsigned int i = 0; i < cmd.length(); i++) {
    if (!isDigit(cmd[i])) { numeric = false; break; }
  }
  if (!numeric) {
    Serial.println("ERR bad command");
    return;
  }

  long pct = cmd.toInt();
  if (pct < 0 || pct > 100) {
    Serial.println("ERR out of range");
    return;
  }

  setDutyPercent((uint8_t)pct);
  lastCommandMillis = millis();

  Serial.print("OK ");
  Serial.println(currentPwmPercent);
}

void loop() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      handleLine(lineBuf);
      lineBuf = "";
    } else if (c != '\r') {
      lineBuf += c;
    }
  }

  if (millis() - lastCommandMillis > FAILSAFE_TIMEOUT_MS) {
    if (currentPwmPercent != FAILSAFE_PWM) {
      setDutyPercent(FAILSAFE_PWM);
    }
  }
}
