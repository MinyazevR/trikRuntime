# AGENT: Адаптация trikRuntime под новую плату (kernel 5.15 + nRF52833)

## Репозиторий прошивки nRF52833

`/media/ntyrreuir/PC711NVME21/trik-nrf52833/` — прошивка периферийного модуля.

Документация:
- `docs/usb-architecture.md` — USB-стек в прошивке (udc_nrf, usbd, usb_raw class, DMA, SOF)
- `docs/telemetry-transport.md` — телеметрия: BULK vs ISO, TIMER2, снапшоты портов

Протокол (`nrfUsbProtocol.h`, зеркало в `trikRuntime/trikControl/src/nrfUsbProtocol.h`):
- Индексы телеметрии: 0-3 encoders, 4-9 SAADC raw 12-bit (A1-A6), 10-13 PWM servo width
- Фрейм: 71 байт (6 header + 65 payload)
- Команды: fire-and-forget, port get/set/reset

## Сделано

### 1. model-config.xml (kernel-5.15)

`trikControl/configs/kernel-5.15/model-config.xml` — создан на основе `kernel-4.14/model-config.xml`.
Содержит **только** A1-A6 (аналоговые сенсоры) и E1-E4 (энкодеры). Остальное удалено.

### 2. system-config.xml (kernel-5.15)

`trikControl/configs/kernel-5.15/system-config.xml` — расширен минимальный `kernel-5.10/system-config.xml`:
- `version="model-2026"` (маркер для заглушек)
- `deviceClasses`: analogSensor (raw 0-4095 для 12-bit SAADC), encoder
- `devicePorts`: A1-A6, E1-E4
- `deviceTypes`: sharpGP2Sensor, touchSensor, lightSensor, encoder95
- `nrfUsb` — USB-коммуникация с nRF52833

### 3. Заглушки в brick.cpp / brick.h

**Принцип:** если `config.version() == "model-2026"`, `createDevice()` создаёт только `analogSensor` и `encoder`, всё остальное молча пропускает.

Изменения:
- `brick.h`: добавлен член `bool mIsRestrictedBoard = false`
- `brick.cpp` конструктор: `mIsRestrictedBoard = (mConfigurer.version() == "model-2026")`
- `brick.cpp` `createDevice()`: guard — `if (mIsRestrictedBoard && deviceClass != "analogSensor" && deviceClass != "encoder")` — ранний return с логом
- Keys, Led, playWavFileCommand, playMp3FileCommand обёрнуты в try/catch `MalformedConfigException` — graceful fallback, если секции отсутствуют в конфиге

### 4. AGENT.md (этот файл)

## Что дальше: отладка nrf-usb-communicator

Порядок:
1. Подключить плату, проверить `lsusb: 2FE9:0100`
2. Запустить runtime с `configs/kernel-5.15/system-config.xml` + `model-config.xml`
3. Проверить чтение телеметрии: парсинг фреймов, индексы портов
4. Проверить fire-and-forget команды на порты
5. Проверить синхронное чтение (`getValue`) через портовый протокол
6. Проверить raw значения: SAADC 12-bit (0-4095) для analog, счётчик для encoder

Ключевые файлы:
- `trikControl/src/nrfUsbProtocol.h` — константы
- `trikControl/src/nrfUsbCommunicator.h/.cpp` — USB-коммуникатор
- `trikControl/src/nrfBusAutoDetector.h/.cpp` — авто-детект USB/I2C