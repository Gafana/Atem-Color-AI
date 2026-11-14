# Atem-Color-AI

Десктопное приложение на **Qt/C++** для автоматизации цветокоррекции и сведения настроек камер Blackmagic через пульт **ATEM**.

## Возможности (v0.2)

- Подключение к ATEM, список входов (mock без SDK на Linux).
- **Анализ кадра**: гистограммы R/G/B и Y/U/V (BT.709), статистики, вектороскоп UV (`src/color/`).
- **Сведение с эталоном** и расчёт поправок (Lift/Gamma/Gain, WB, gain dB, Saturation) с демпфированием.
- **Camera Control** через ATEM SDK (`SetFloats` / `SetInt16s` / `SetInt8s`) + mock с логированием.
- GStreamer: один кадр (appsink → `QImage` / PNG).
- CLI `--analyze` (PNG → таблица / JSON), GUI `-DENABLE_GUI=ON --gui`.

Подробная математика: [docs/color-matching.md](docs/color-matching.md).

## Требования

| Компонент | Примечание |
|-----------|------------|
| CMake ≥ 3.16, C++17 | |
| Qt 6 или 5.15+ | Core, Gui, Widgets (GUI), Test |
| ATEM SDK | Windows/macOS, не в git |
| GStreamer 1.x | опционально |

## Сборка (Linux, mock)

```bash
sudo apt-get install -y cmake g++ pkg-config qt6-base-dev qt6-base-dev-tools \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev

cmake -B build -DENABLE_GSTREAMER=ON -DENABLE_GUI=ON -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Запуск

Список входов:

```bash
./build/atem-color-ai --ip 192.168.10.240
```

Анализ PNG без ATEM:

```bash
./build/atem-color-ai --analyze ref.png cam2.png cam3.png
./build/atem-color-ai --analyze ref.png cam2.png --json
```

Первый PNG — эталон, остальные — кандидаты (позиционные аргументы).

GUI:

```bash
./build/atem-color-ai --gui -i 192.168.10.240
```

Захват кадра:

```bash
./build/atem-color-ai --capture videotestsrc --output test.png
```

## Архитектура

```
src/
  color/           — анализ и сведение
  atem/            — AtemClient, Camera Control, mock/SDK
  capture/         — GStreamer appsink
  gui/             — Qt Widgets (ENABLE_GUI)
  main.cpp         — CLI + GUI entry
tests/
  tst_coloranalysis.cpp
docs/
  color-matching.md
```

## Ограничения

- Живое подключение к ATEM и Camera Control на железе — Windows/macOS с runtime Blackmagic.
- `destinationDevice` = id входа ATEM; на реальном сетапе может потребоваться сверка с документацией микшера.
- Авто-iris и hue пока не используются активно; итеративное сведение — вручную (повторный «Захватить/Анализировать»).

## Лицензия

Код проекта — репозиторий. **Blackmagic ATEM SDK** — отдельная лицензия BMD; не коммитить файлы SDK.