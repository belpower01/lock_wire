#include <lock_wire.h>

/* Digispark P2 listens. P1 is the onboard LED.
 * A valid frame latches open or close. A bad frame leaves the latch alone.
 */

const uint8_t PIN_LINE = 2;
const uint8_t PIN_LED = 1;

uint8_t held = 2;

void setup() {
  lockWireBeginRx(PIN_LINE);
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);
}

void loop() {
  const uint8_t cmd = lockWirePoll();
  if (cmd != 0 && cmd != held) {
    held = cmd;
  }
  digitalWrite(PIN_LED, held == 2 ? HIGH : LOW);
}
