#include "lock_wire.h"

#include <Arduino.h>

static const uint16_t BIT_US = 1000;
static const uint8_t ATTN_BITS = 20;
static const uint8_t PRE_BITS = 3;
static const uint8_t CAL_BITS = 4;
static const uint8_t GAP_BITS = 4;
static const uint32_t ATTN_MIN_US = 8000;
static const uint32_t ATTN_MAX_US = 35000;
static const uint32_t CAL_MIN_US = 2500;
static const uint32_t CAL_MAX_US = 7000;
static const uint32_t PRE_MAX_US = 10000;
static const uint32_t IDLE_US = 12000;
static const uint32_t IDLE_MAX_US = 150000;

static uint8_t s_pin = 0;
static bool s_ignoreLow = false;

static void bitSlot(uint8_t level) {
  digitalWrite(s_pin, level);
  delayMicroseconds(BIT_US);
}

static void slots(uint8_t level, uint8_t n) {
  while (n--) bitSlot(level);
}

static void sendByte(uint8_t v) {
  bitSlot(LOW);
  for (uint8_t i = 0; i < 8; i++) {
    bitSlot((v & 1u) ? HIGH : LOW);
    v >>= 1;
  }
  bitSlot(HIGH);
}

static void sendFrame(uint8_t cmd) {
  const uint8_t sum = (uint8_t)(LOCK_WIRE_SYNC + cmd);
  const uint8_t sum2 = (uint8_t)(0xFFu ^ LOCK_WIRE_SYNC ^ cmd);
  slots(LOW, ATTN_BITS);
  slots(HIGH, PRE_BITS);
  slots(LOW, CAL_BITS);
  slots(HIGH, GAP_BITS);
  sendByte(LOCK_WIRE_SYNC);
  sendByte(cmd);
  sendByte(sum);
  sendByte(sum2);
}

static bool waitLevel(uint8_t level, uint32_t timeoutUs) {
  const uint32_t t0 = micros();
  while (digitalRead(s_pin) != level) {
    if ((uint32_t)(micros() - t0) >= timeoutUs) return false;
  }
  return true;
}

static void waitIdle() {
  uint32_t highAt = micros();
  bool high = digitalRead(s_pin) == HIGH;
  const uint32_t t0 = highAt;
  while ((uint32_t)(micros() - t0) < IDLE_MAX_US) {
    if (digitalRead(s_pin) == HIGH) {
      if (!high) {
        high = true;
        highAt = micros();
      } else if ((uint32_t)(micros() - highAt) >= IDLE_US) {
        return;
      }
    } else {
      high = false;
    }
  }
  s_ignoreLow = true;
}

static bool readByte(uint32_t bitUs, uint8_t *out) {
  if (!waitLevel(LOW, bitUs * 8u)) return false;
  const uint32_t tEdge = micros();
  uint8_t v = 0;
  for (uint8_t b = 0; b < 8; b++) {
    const uint32_t at = (bitUs * (uint32_t)(3u + 2u * b)) / 2u;
    while ((uint32_t)(micros() - tEdge) < at) {
    }
    if (digitalRead(s_pin) == HIGH) v |= (uint8_t)(1u << b);
  }
  const uint32_t stopAt = (bitUs * 19u) / 2u;
  while ((uint32_t)(micros() - tEdge) < stopAt) {
  }
  if (digitalRead(s_pin) != HIGH) return false;
  *out = v;
  return true;
}

static bool sumsOk(const uint8_t *frame) {
  const uint8_t cmd = frame[1];
  if (frame[0] != LOCK_WIRE_SYNC) return false;
  if (cmd != LOCK_WIRE_OPEN && cmd != LOCK_WIRE_CLOSE) return false;
  if (frame[2] != (uint8_t)(LOCK_WIRE_SYNC + cmd)) return false;
  if (frame[3] != (uint8_t)(0xFFu ^ LOCK_WIRE_SYNC ^ cmd)) return false;
  return true;
}

void lockWireBeginTx(uint8_t pin) {
  s_pin = pin;
  pinMode(s_pin, OUTPUT);
  digitalWrite(s_pin, HIGH);
}

void lockWireBeginRx(uint8_t pin) {
  s_pin = pin;
  s_ignoreLow = false;
  pinMode(s_pin, INPUT_PULLUP);
}

void lockWireSendOpen() { sendFrame(LOCK_WIRE_OPEN); }

void lockWireSendClose() { sendFrame(LOCK_WIRE_CLOSE); }

uint8_t lockWirePoll() {
  if (digitalRead(s_pin) != LOW) {
    s_ignoreLow = false;
    return 0;
  }
  if (s_ignoreLow) return 0;

  const uint32_t t0 = micros();
  while (digitalRead(s_pin) == LOW) {
    if ((uint32_t)(micros() - t0) >= ATTN_MAX_US) {
      s_ignoreLow = true;
      return 0;
    }
  }
  const uint32_t attn = (uint32_t)(micros() - t0);
  if (attn < ATTN_MIN_US) {
    if (attn > 2000u) waitIdle();
    return 0;
  }

  if (!waitLevel(LOW, PRE_MAX_US)) {
    waitIdle();
    return 0;
  }
  const uint32_t tCal = micros();
  while (digitalRead(s_pin) == LOW) {
    if ((uint32_t)(micros() - tCal) >= CAL_MAX_US) {
      s_ignoreLow = true;
      return 0;
    }
  }
  const uint32_t cal = (uint32_t)(micros() - tCal);
  if (cal < CAL_MIN_US) {
    waitIdle();
    return 0;
  }

  const uint32_t bitUs = cal / CAL_BITS;
  uint8_t frame[4];
  bool ok = true;
  for (uint8_t i = 0; i < 4; i++) {
    if (!readByte(bitUs, &frame[i])) {
      ok = false;
      break;
    }
  }
  waitIdle();
  if (!ok || !sumsOk(frame)) return 0;
  return frame[1] == LOCK_WIRE_OPEN ? 1 : 2;
}
