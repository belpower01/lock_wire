# lock_wire

One signal wire from an **Arduino Nano** to an **ATtiny** (Digispark). The Nano sends a packet that means **open** or **close**. The ATtiny checks **two checksums** and, if both match, **holds that command**. It does not change the held command by itself, and it does not change it when the frame is broken.

Idle level is HIGH. Common GND. In the lock this library was written for, Nano **D8** drives ATtiny **PB2** (Digispark pin 2).

---

## English

### Why this library

ATtiny85 has no hardware UART on the pin we can spare, and the chip may be busy for a few milliseconds doing something else (servo pulses that turn interrupts off). Stock options do not fit that wire:

- **Hardware Serial** — ATtiny85 does not have a UART, and Digispark pins 3 and 4 are the USB pair.
- **SoftwareSerial** — times bits from interrupts. A long `noInterrupts()` stretch eats start bits.
- **Dallas OneWire** — a bus for DS18B20-style devices, not an open/close command with two checksums.

`lock_wire` bit-bangs a short frame. The Nano is the only driver. The ATtiny only listens, with the pin in `INPUT_PULLUP`. The first long LOW is long enough that a few milliseconds of blindness at the start still leaves a recognizable attention pulse. A calibration LOW after that is measured with the ATtiny's own `micros()`, so the internal RC clock does not have to match the Nano's crystal.

### Wire

| | Nano (transmitter) | ATtiny (receiver) |
|---|---|---|
| Pin mode | `OUTPUT`, driven HIGH at rest | `INPUT_PULLUP`, never drives the line |
| Role | `lockWireSendOpen()` / `lockWireSendClose()` | `lockWirePoll()` |
| Direction | one way | one way |

One transmitter. Do not let a second pin drive the same wire.

### What the ATtiny holds

The library does not move a servo and does not write EEPROM. `lockWirePoll()` returns:

| Return | Meaning |
|--------|---------|
| `0` | no new command: idle, noise, truncated frame, bad sync, bad command byte, or either checksum failed |
| `1` | valid **open** packet |
| `2` | valid **close** packet |

`0` means **leave the held command as it is**. A sketch should apply `1` or `2` only when the value differs from the command already latched. Sending the same command again does not need to move anything. A different valid packet is the only event that replaces the latch. Power-up state is the sketch's job (for example a byte in EEPROM). The example `AttinyHold` keeps the latch in RAM and shows it on the Digispark LED.

### Frame

One bit slot is **1000 µs**: drive the level, then `delayMicroseconds(1000)`. The receiver does not trust that number. It measures the calibration pulse and divides by 4.

| Part | Slots | Time |
|------|------:|-----:|
| Attention, LOW | 20 | 20 ms |
| Gap, HIGH | 3 | 3 ms |
| Calibration, LOW | 4 | 4 ms |
| Gap, HIGH | 4 | 4 ms |
| Four bytes, 8N1, LSB first | 40 | 40 ms |
| **Transmit total** | **71** | **~71 ms** |

After the last stop bit the receiver waits until the line has stayed HIGH for **12 ms** (longer than any run of 1-bits inside this frame) before it will look for another packet. A LOW shorter than **8 ms** is not attention. A LOW longer than **35 ms** is treated as a stuck line: the receiver ignores LOW until the wire is HIGH again, and the held command stays.

`lockWireSendOpen()` and `lockWireSendClose()` block for the whole transmit (~71 ms). `lockWirePoll()` returns immediately while the line is HIGH. When attention is recognized it blocks until the frame is finished or a timeout fires (worst case about 150 ms on a stuck line). Call `poll()` from `loop()`. Do not bit-bang something else on this pin, and do not expect bits to be sampled while interrupts are off inside `poll()`'s caller — `poll()` itself keeps interrupts on and samples in a tight loop.

### Bytes

8N1, least significant bit first. Start bit LOW, eight data bits, stop bit HIGH. The receiver samples the center of each data bit (1.5, 2.5, … bit times after the falling edge of start) and requires the stop bit to read HIGH.

| Byte | Name | Value |
|------|------|-------|
| 0 | Sync | `0xA5` |
| 1 | Command | `0x17` open, `0xE8` close |
| 2 | Sum | `(uint8_t)(0xA5 + command)` |
| 3 | Second sum | `(uint8_t)(0xFF ^ 0xA5 ^ command)` |

Open and close differ in all eight bits (`0x17` is `00010111`, `0xE8` is `11101000`).

Worked checksums:

| Command | Sum | Second sum |
|---------|-----|------------|
| Open `0x17` | `0xBC` | `0x4D` |
| Close `0xE8` | `0x8D` | `0xB2` |

The frame is accepted only when all of these are true:

- byte 0 is `0xA5`
- byte 1 is exactly `0x17` or exactly `0xE8`
- byte 2 equals the sum of that command
- byte 3 equals the second sum of that command
- every stop bit was HIGH

Anything else returns `0`. The two sums are different functions of the same command (addition and inverted XOR), so a shifted or truncated frame does not satisfy both by accident often enough to move a lock. The command byte is also checked against the two legal values, not merely "whatever the sums describe".

### Clock

The Nano's `delayMicroseconds` follows a crystal. A Digispark runs from the internal oscillator at 16.5 MHz, and its `micros()` is not the same scale. The receiver times the 4-slot calibration LOW with its own `micros()` and uses `width/4` as the bit time for the four bytes that follow. Ratio errors between the two clocks cancel. Drift during the 40 ms of payload is small next to a 500 µs margin to the edge of a bit.

Attention is only a wake-up. Its measured length is allowed from 8 ms to 35 ms because the receiver may have been inside another blocking call when the LOW began, and will see only the tail.

### API

```cpp
#include <lock_wire.h>

void lockWireBeginTx(uint8_t pin);
void lockWireBeginRx(uint8_t pin);
void lockWireSendOpen();
void lockWireSendClose();
uint8_t lockWirePoll();
```

| Call | Who | Effect |
|------|-----|--------|
| `lockWireBeginTx(pin)` | Nano | `OUTPUT`, line HIGH |
| `lockWireBeginRx(pin)` | ATtiny | `INPUT_PULLUP` |
| `lockWireSendOpen()` | Nano | one open frame, blocks ~71 ms |
| `lockWireSendClose()` | Nano | one close frame, blocks ~71 ms |
| `lockWirePoll()` | ATtiny | `0` keep, `1` open, `2` close |

Constants: `LOCK_WIRE_SYNC` (`0xA5`), `LOCK_WIRE_OPEN` (`0x17`), `LOCK_WIRE_CLOSE` (`0xE8`).

### Install

Arduino IDE: clone this repo into `Documents/Arduino/libraries/lock_wire` (the folder must contain `library.properties`). Restart the IDE if the library is missing under *Sketch → Include Library*.

PlatformIO:

```ini
lib_deps =
  https://github.com/belpower01/lock_wire.git
```

Examples:

- [examples/NanoSend](examples/NanoSend/NanoSend.ino) — D8 alternates open and close every 2 s.
- [examples/AttinyHold](examples/AttinyHold/AttinyHold.ino) — pin 2 receives, the onboard LED follows the latched command (on = close).

### Limits

- AVR-oriented timing (`digitalWrite`, `delayMicroseconds`, `micros`). Not a multi-drop bus.
- The ATtiny sketch must keep calling `lockWirePoll()` often enough to see 8 ms of the 20 ms attention LOW.
- Duplicate of the command already held is still reported as `1` or `2`. Ignoring it is the sketch's latch, as in `AttinyHold`.
- No acknowledgement. The Nano does not learn whether the ATtiny accepted the frame.

---

## Русский

### Зачем эта библиотека

Нужно передать по **одному проводу** команду «открыть» или «закрыть» с Arduino Nano на ATtiny и не сдвинуть механизм из-за помехи. У ATtiny85 на свободном пине нет аппаратного UART. Готовые варианты сюда не подходят:

- **Hardware Serial** на ATtiny85 нет. У Digispark пины 3 и 4 заняты USB.
- **SoftwareSerial** меряет биты по прерываниям. Длинный участок с выключенными прерываниями съедает старт-бит.
- **Dallas OneWire** — шина датчиков вроде DS18B20, а не команда замка с двумя контрольными суммами.

`lock_wire` собирает короткий кадр вручную. Nano — единственный передатчик. ATtiny только слушает, пин в `INPUT_PULLUP`. Длинный LOW в начале кадра переживает несколько миллисекунд, когда приёмник ещё занят другим делом и видит только хвост импульса. Следующий за ним калибровочный LOW приёмник измеряет своим `micros()`, поэтому внутренний RC ATtiny не обязан совпасть с кварцем Nano.

### Провод

| | Nano (передаёт) | ATtiny (принимает) |
|---|---|---|
| Режим пина | `OUTPUT`, в покое HIGH | `INPUT_PULLUP`, линию не драйвит |
| Роль | `lockWireSendOpen()` / `lockWireSendClose()` | `lockWirePoll()` |
| Направление | только туда | только оттуда |

Передатчик один. Второй выход на этот же провод подключать нельзя. Общий GND обязателен.

В замке, для которого написана библиотека: **Nano D8 → ATtiny PB2** (пин 2 Digispark). В вызовах `Begin` можно указать другие пины.

### Что ATtiny удерживает

Библиотека не крутит серву и не пишет EEPROM. `lockWirePoll()` возвращает:

| Код | Смысл |
|-----|--------|
| `0` | новой команды нет: тишина, помеха, обрыв, чужой синхробайт, чужая команда или не сошлась хотя бы одна сумма |
| `1` | верный пакет **открыть** |
| `2` | верный пакет **закрыть** |

`0` значит: **удержанную команду не менять**. Скетч применяет `1` или `2` только если это значение отличается от уже защёлкнутого. Повтор той же команды ничего двигать не обязан. Заменить защёлкнутое состояние может только другой верный пакет. Что стоит на пинах после включения — решает скетч (например байт в EEPROM). Пример `AttinyHold` держит команду в ОЗУ и показывает её светодиодом Digispark.

### Кадр

Один слот — **1000 мкс**: выставить уровень и `delayMicroseconds(1000)`. Приёмник этой константе не верит. Он измеряет калибровочный импульс и делит длительность на 4.

| Участок | Слоты | Время |
|---------|------:|------:|
| Внимание, LOW | 20 | 20 мс |
| Пауза, HIGH | 3 | 3 мс |
| Калибровка, LOW | 4 | 4 мс |
| Пауза, HIGH | 4 | 4 мс |
| Четыре байта, 8N1, младший бит первый | 40 | 40 мс |
| **Передача целиком** | **71** | **~71 мс** |

После последнего стоп-бита приёмник ждёт, пока линия простоит HIGH **12 мс**. Это дольше любой пачки единиц внутри этого кадра, поэтому хвост пакета не читается как новое внимание. LOW короче **8 мс** вниманием не считается. LOW длиннее **35 мс** — залипшая линия: приёмник игнорирует LOW, пока провод снова не станет HIGH, команда остаётся прежней.

`lockWireSendOpen()` и `lockWireSendClose()` блокируют Nano на всю передачу (~71 мс). `lockWirePoll()` при HIGH возвращается сразу. Когда внимание узнано, функция не выходит, пока кадр не кончится или не сработает таймаут (на залипшей линии это до ~150 мс). Вызывать `poll()` из `loop()`. Пока он принимает кадр, биты выбираются в цикле. Не занимайте этот пин другим bit-bang и не рассчитывайте, что выборка идёт внутри чужого участка с выключенными прерываниями: сам `poll()` прерывания не глушит.

### Байты

8N1, первым идёт младший бит. Старт — LOW, восемь бит данных, стоп — HIGH. Приёмник берёт середину каждого бита данных (1.5, 2.5, … длительности бита от спада старта) и требует, чтобы стоп прочитался как HIGH.

| Байт | Имя | Значение |
|------|-----|----------|
| 0 | Синхро | `0xA5` |
| 1 | Команда | `0x17` открыть, `0xE8` закрыть |
| 2 | Сумма | `(uint8_t)(0xA5 + команда)` |
| 3 | Вторая сумма | `(uint8_t)(0xFF ^ 0xA5 ^ команда)` |

Открыть и закрыть различаются всеми восемью битами: `0x17` это `00010111`, `0xE8` это `11101000`.

Готовые суммы:

| Команда | Сумма | Вторая сумма |
|---------|-------|----------------|
| Открыть `0x17` | `0xBC` | `0x4D` |
| Закрыть `0xE8` | `0x8D` | `0xB2` |

Кадр принимается только если одновременно:

- байт 0 равен `0xA5`
- байт 1 равен ровно `0x17` или ровно `0xE8`
- байт 2 равен сумме этой команды
- байт 3 равен второй сумме этой команды
- стоп-бит каждого байта был HIGH

Иначе `poll()` возвращает `0`. Суммы считаются по-разному (сложение и инвертированное XOR от тех же двух байт), поэтому сдвиг или обрывок кадра редко удовлетворяет обеим сразу. Команда ещё и сверяется с двумя разрешёнными значениями, а не берётся «как вышло из сумм».

### Такт

`delayMicroseconds` на Nano идёт от кварца. Digispark работает от внутреннего генератора 16.5 МГц, и его `micros()` в другом масштабе. Приёмник меряет калибровочный LOW из четырёх слотов своим `micros()` и дальше считает бит как `длина/4`. Относительная ошибка двух тактов сокращается. Уход за 40 мс полезной нагрузки мал рядом с запасом 500 мкс до края бита.

Внимание — только пробуждение. Его длина принимается от 8 до 35 мс: в момент начала LOW приёмник мог быть внутри другого блокирующего вызова и увидеть лишь хвост.

### API

```cpp
#include <lock_wire.h>

void lockWireBeginTx(uint8_t pin);
void lockWireBeginRx(uint8_t pin);
void lockWireSendOpen();
void lockWireSendClose();
uint8_t lockWirePoll();
```

| Вызов | Кто | Действие |
|-------|-----|----------|
| `lockWireBeginTx(pin)` | Nano | `OUTPUT`, линия HIGH |
| `lockWireBeginRx(pin)` | ATtiny | `INPUT_PULLUP` |
| `lockWireSendOpen()` | Nano | один кадр «открыть», блок ~71 мс |
| `lockWireSendClose()` | Nano | один кадр «закрыть», блок ~71 мс |
| `lockWirePoll()` | ATtiny | `0` не менять, `1` открыть, `2` закрыть |

Константы: `LOCK_WIRE_SYNC` (`0xA5`), `LOCK_WIRE_OPEN` (`0x17`), `LOCK_WIRE_CLOSE` (`0xE8`).

### Установка

Arduino IDE: склонируйте репозиторий в `Документы/Arduino/libraries/lock_wire` (в папке должен лежать `library.properties`). Если библиотеки нет в *Скетч → Подключить библиотеку*, перезапустите IDE.

PlatformIO:

```ini
lib_deps =
  https://github.com/belpower01/lock_wire.git
```

Примеры:

- [examples/NanoSend](examples/NanoSend/NanoSend.ino) — D8 по очереди шлёт открыть и закрыть, пауза 2 с.
- [examples/AttinyHold](examples/AttinyHold/AttinyHold.ino) — пин 2 принимает, штатный светодиод показывает защёлкнутую команду (горит = закрыто).

### Ограничения

- Время рассчитано на AVR (`digitalWrite`, `delayMicroseconds`, `micros`). Это не шина на несколько ведомых.
- Скетч ATtiny должен вызывать `lockWirePoll()` достаточно часто, чтобы увидеть хотя бы 8 мс из 20 мс внимания.
- Повтор уже удержанной команды всё равно приходит как `1` или `2`. Отбрасывает его защёлка в скетче, как в `AttinyHold`.
- Ответа на кадр нет. Nano не узнаёт, принял ли ATtiny пакет.
