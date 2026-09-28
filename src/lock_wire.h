#pragma once

#include <stdint.h>

/* Один провод, покой HIGH. Ячейка передаёт, ATtiny только слушает.
 * Кадр: длинный LOW, короткий LOW для такта, затем четыре байта 8N1
 * (младший бит первый): синхро, команда, сумма, вторая сумма.
 * poll: 0 суммы нет или они не сошлись, 1 открыть, 2 закрыть.
 * 0 значит удержанную команду не менять. */

#define LOCK_WIRE_SYNC 0xA5u
#define LOCK_WIRE_OPEN 0x17u
#define LOCK_WIRE_CLOSE 0xE8u

void lockWireBeginTx(uint8_t pin);
void lockWireBeginRx(uint8_t pin);
void lockWireSendOpen();
void lockWireSendClose();
uint8_t lockWirePoll();
