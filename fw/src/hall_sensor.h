// Hall sensor helper: reads Hall input and drives status2 LED
#pragma once
#include <Arduino.h>

class HallSensor {
public:
  HallSensor(uint8_t hallPin = PA5, uint8_t ledPin = PA4);
  void start();
  void loop();
  bool isMagnetPresent() const { return _magnetPresent; }
private:
  uint8_t _hallPin;
  uint8_t _ledPin;
  bool _magnetPresent;
  unsigned long _lastChangeMs;
  const unsigned long _debounceMs = 5;
};
