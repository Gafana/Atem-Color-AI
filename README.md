# Atem-Color-AI

Десктопное приложение на **Qt/C++** для автоматизации цветокоррекции и сведения настроек камер Blackmagic через пульт **ATEM**. Текущая версия — минимальный прототип: подключение к ATEM, список входов и опциональный захват одного кадра через GStreamer.

## Назначение

- Подключение к видеомикшеру Blackmagic ATEM по IP.
- Чтение списка входов (id, короткое/длинное имя, тип порта, поддержка Camera Control).
- Захват одного кадра из видеопотока (SRT, NDI, DeckLink, V4L2 и др.) в PNG.
- В следующих этапах: управление Camera Control через ATEM, анализ кадра и автоматическое сведение цвета.

## Требования

| Компонент | Версия / примечание |
|-----------|---------------------|
| CMake | ≥ 3.16 |
| C++ | C++17 |
| Qt | **Qt 6** (предпочтительно) или Qt 5.15+ |
| ATEM SDK | Только Windows/macOS, **не входит в репозиторий** |
| GStreamer | 1.x (опционально, `-DENABLE_GSTREAMER=ON`) |

### ATEM Switchers SDK (Blackmagic)

SDK **нельзя** коммитить в git (лицензия Blackmagic).

1. Скачайте с сайта Blackmagic: [Support → Software → ATEM Switchers SDK](https://www.blackmagicdesign.com/developer/products/atem/sdk-and-software) (раздел **ATEM Switchers SDK**).
2. Распакуйте локально, например в `third_party/atem-sdk/` (каталог `third_party/` в `.gitignore`).
3. Укажите **корень** распакованного архива (например `Blackmagic ATEM Switchers SDK 11.0.1`) при конфигурации CMake:

```bash
cmake -B build -DATEM_SDK_DIR="$PWD/third_party/atem-sdk/Blackmagic ATEM Switchers SDK 11.0.1"
```

Внутри архива 11.x ожидаются каталоги `Mac OS X/include/` (заголовки + `BMDSwitcherAPIDispatch.cpp`) и `Windows/include/` (`BMDSwitcherAPI.idl`; для C++ также можно использовать сгенерированный `Windows/Samples/DeviceInfo/BMDSwitcherAPI.h` после MIDL).

**Платформы SDK:**

- **Windows:** COM (`BMDSwitcherAPI.idl` / сгенерированные заголовки, `BMDSwitcherAPI.lib`).
- **macOS:** `BMDSwitcherAPI.framework` или заголовки `BMDSwitcherAPI.h`.
- **Linux:** официального SDK нет; без SDK проект собирается с **mock**-реализацией `AtemClient`.

Модуль поиска: `cmake/FindATEMSDK.cmake` (переменная `ATEM_SDK_DIR` или cache `ATEM_SDK_DIR`).

## Сборка

### Linux (без ATEM SDK)

```bash
sudo apt-get install -y \
  cmake g++ pkg-config \
  qt6-base-dev \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev

cmake -B build -DENABLE_GSTREAMER=ON
cmake --build build -j
```

### macOS

```bash
brew install qt@6 gstreamer gst-plugins-base pkg-config
cmake -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)" \
  -DATEM_SDK_DIR="$HOME/SDKs/ATEM Switchers SDK" \
  -DENABLE_GSTREAMER=ON
cmake --build build -j
```

### Windows (MSVC)

```bash
cmake -B build -G "Visual Studio 17 2022" -A x64 ^
  -DCMAKE_PREFIX_PATH="C:\Qt\6.7.0\msvc2019_64" ^
  -DATEM_SDK_DIR="C:\Program Files\Blackmagic Design\ATEM Switchers SDK" ^
  -DENABLE_GSTREAMER=OFF
cmake --build build --config Release
```

Опции CMake:

| Опция | По умолчанию | Описание |
|-------|--------------|----------|
| `ATEM_SDK_DIR` | — | Путь к локальному ATEM SDK |
| `ENABLE_GSTREAMER` | ON | Модуль захвата кадра |
| `ENABLE_GUI` | OFF | Qt Widgets (зарезервировано) |

## Запуск

Список входов ATEM (mock, если SDK не подключён):

```bash
./build/atem-color-ai --ip 192.168.10.240
```

Только список:

```bash
./build/atem-color-ai --list-only -i 192.168.10.240
```

Захват кадра (GStreamer):

```bash
./build/atem-color-ai --capture videotestsrc --output test.png
./build/atem-color-ai --capture v4l2:/dev/video0 --output cam.png
./build/atem-color-ai --capture "srt://192.168.1.5:9000?mode=caller" --output frame.png
```

Форматы источника `--capture`:

- `srt://...` — SRT
- `ndi:SourceName` — NDI (если установлен плагин `ndisrc`)
- `decklink:0` — DeckLink device number
- `v4l2:/dev/video0` — V4L2
- `videotestsrc` — тестовый паттерн (для проверки без железа)

По умолчанию IP ATEM: `192.168.10.240` (переопределяется `-i` / `--ip`).

## Архитектура

```
src/
  main.cpp                 — CLI
  atem/
    AtemClient.h           — абстракция
    AtemClientFactory.cpp  — выбор SDK / mock
    AtemClientMock.cpp     — заглушка без SDK
    AtemClientSdk.cpp      — реализация #ifdef HAVE_ATEM_SDK
  capture/
    GstFrameCapture.*      — один кадр в PNG (#ifdef HAVE_GSTREAMER)
cmake/
  FindATEMSDK.cmake
```

## Текущий этап (v0.1)

- [x] CMake, Qt6/Qt5, C++17
- [x] `AtemClient` + mock без SDK
- [x] Подключение FindATEMSDK и реализация под SDK (Windows/macOS)
- [x] Логирование входов ATEM
- [x] GStreamer: один кадр → PNG
- [ ] Camera Control (выставление gain, WB, iris через ATEM)
- [ ] Анализ кадра (histogram / vectorscope) и автосведение
- [ ] GUI для оператора

## Roadmap

1. **Camera Control API** — чтение/запись параметров камер через `IBMDSwitcherCameraControl`.
2. **Мультикамерный capture** — синхронные кадры с нескольких источников.
3. **Цветовой анализ** — оценка экспозиции и баланса белого по эталону.
4. **Автосведение** — итеративная подгонка параметров с логированием и откатом.
5. **GUI** — превью, таблица камер, сценарии сведения.

## Лицензия

Код проекта — см. репозиторий. **Blackmagic ATEM SDK** распространяется отдельно по условиям Blackmagic Design; не включайте файлы SDK в git.