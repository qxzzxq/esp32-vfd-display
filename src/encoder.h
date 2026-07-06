#ifndef encoder_h
#define encoder_h

#include <stdint.h>
#include "freertos/FreeRTOS.h"

// KY-040 rotary encoder + push button, decoded in GPIO ISRs (the ESP32-C3 has
// no PCNT peripheral). Rotation events fire once per detent; button events are
// raw edges (press/release timing is interpreted by the consumer).
enum class EncEvent : uint8_t { StepCW, StepCCW, BtnDown, BtnUp };

void encoder_init();
// Blocks up to `timeout` for the next event; returns false on timeout.
bool encoder_wait(EncEvent* ev, TickType_t timeout);

#endif
