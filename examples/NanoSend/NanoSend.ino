#include <lock_wire.h>

/* Nano D8 -> ATtiny pin 2. Common GND. Idle HIGH. */

const uint8_t PIN_LINE = 8;

void setup() {
  lockWireBeginTx(PIN_LINE);
}

void loop() {
  lockWireSendOpen();
  delay(2000);
  lockWireSendClose();
  delay(2000);
}
