#include "hall_sensor.h"

#include "config.h"

HallSensor::HallSensor(uint8_t hallPin, uint8_t ledPin)
  : _hallPin(hallPin), _ledPin(ledPin), _magnetPresent(false), _lastChangeMs(0) {}

void HallSensor::start() {
  // Hall pin: input. Use pullup to avoid floating when open.
  pinMode(_hallPin, INPUT_PULLUP);
  // Status2 LED pin: output. The board uses active-low LED, drive HIGH to turn off.
  pinMode(_ledPin, OUTPUT);
  digitalWrite(_ledPin, HIGH); // LED off (active low)
  _magnetPresent = false;
  _lastChangeMs = millis();
}

void HallSensor::loop() {
  int raw = digitalRead(_hallPin);

#if defined(HALLACTIVETYPE) && (HALLACTIVETYPE == 1)
  // active HIGH: sensor reads HIGH when magnet present
  bool present = (raw == HIGH);
#else
  // default/fallback: active LOW (HALLACTIVETYPE == 2)
  bool present = (raw == LOW);
#endif

  unsigned long now = millis();
  if (present != _magnetPresent) {
    // debounce: only accept change if stable for _debounceMs
    if ((now - _lastChangeMs) >= _debounceMs) {
      _magnetPresent = present;
      _lastChangeMs = now;
      // Drive status2 LED: active-low LED -> LOW to turn on
      //if (_magnetPresent) {
      //  digitalWrite(_ledPin, LOW); // LED ON
      //} else {
      //  digitalWrite(_ledPin, HIGH); // LED OFF
      //}
    }
    // else: ignore brief changes
  } else {
    _lastChangeMs = now; // keep timestamp fresh
  }
}
