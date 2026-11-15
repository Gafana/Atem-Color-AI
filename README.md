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

## Установка (Windows, GitHub Release)

1. Скачайте **`.exe`-инсталлятор** из [Releases](https://github.com/Gafana/Atem-Color-AI/releases) (имя `…-win64-atem-sdk.exe`; SDK в установщик **не** входит, только код, собранный с `HAVE_ATEM_SDK`).
2. **Обязательно** установите [Blackmagic ATEM Switchers / ATEM Software Control](https://www.blackmagicdesign.com/support/family/atem-live-production-switchers) — без него COM-сервер `BMDSwitcherAPI` недоступен, и приложение не сможет подключиться к пульту (при установке нашего инсталлятора показывается предупреждение, если Blackmagic ПО не найдено).
3. Запустите **Atem Color AI** из меню Пуск.

### Что внутри Windows-инсталлятора

| Компонент | Включено |
|-----------|----------|
| `atem-color-ai.exe` (GUI + CLI) | да |
| Qt DLL и платформенные плагины (`windeployqt`) | да |
| Среда выполнения **GStreamer** (DLL GLib/GStreamer в `bin/` рядом с exe; плагины в `lib/gstreamer-1.0/`; `GST_PLUGIN_PATH` / scanner — при старте) | да |
| Распространяемые библиотеки **MSVC** | да |
| Файлы **Blackmagic ATEM SDK** или ATEM Software Control | **нет** |
| Изменение системного **PATH** при установке | **нет** (запуск через ярлыки Пуск / рабочий стол) |

Лицензия SDK: [docs/atem-sdk-license-notes.md](docs/atem-sdk-license-notes.md).

## Требования

| Компонент | Примечание |
|-----------|------------|
| CMake ≥ 3.16, C++17 | |
| Qt 6 или 5.15+ | Core, Gui, Widgets (GUI), Test |
| ATEM SDK | Windows/macOS, не в git |
| GStreamer 1.x | опционально |
| Windows: ATEM Software Control | для реального ATEM (COM), не поставляется с нашим инсталлятором |

## Сборка (Linux, mock)

```bash
sudo apt-get install -y cmake g++ pkg-config qt6-base-dev qt6-base-dev-tools \
  libgstreamer1.0-dev libgstreamer-plugins-base1.0-dev

cmake -B build -DENABLE_GSTREAMER=ON -DENABLE_GUI=ON -DBUILD_TESTING=ON
cmake --build build -j
ctest --test-dir build --output-on-failure
```

## Сборка (Windows, локально)

Требуется: Visual Studio 2022, Qt 6, GStreamer MSVC (`GSTREAMER_ROOT`), опционально распакованный ATEM SDK (`ATEM_SDK_DIR`), **NSIS** для CPack.

```powershell
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DENABLE_GUI=ON -DENABLE_GSTREAMER=ON -DBUILD_WINDOWS_INSTALLER=ON `
  -DCMAKE_PREFIX_PATH="C:\Qt\6.8.0\msvc2022_64" `
  -DGSTREAMER_ROOT="C:\gstreamer\1.0\msvc_x86_64" `
  -DATEM_SDK_DIR="D:\SDK\Blackmagic ATEM Switchers SDK 11.0.1"

cmake --build build --config Release
cmake --build build --config Release --target package
```

Инсталлятор (при заданном `ATEM_SDK_DIR`): `build\AtemColorAI-<version>-win64-atem-sdk.exe`. Без SDK локально собирается **mock** (`…-win64-mock.exe`) — только для разработки, не публикуется.

## CI и секрет `ATEM_SDK_URL`

Workflow [`.github/workflows/windows-installer.yml`](.github/workflows/windows-installer.yml) — одна job **`win64-atem-sdk`** (без matrix):

- `pull_request` → сборка + артефакт инсталлятора;
- тег `v*` → pre-release с инсталлятором;
- `workflow_dispatch`;
- smoke-тест: тихая установка и `atem-color-ai --help` без GStreamer в PATH.

Секрет **`ATEM_SDK_URL`** (приватная ссылка на zip SDK) **обязателен** в CI: скачивание SDK, `midl` по `BMDSwitcherAPI.idl`, сборка с `HAVE_ATEM_SDK`. Файлы SDK **не** попадают в инсталлятор. Кэшируются Qt и префикс GStreamer MSVC.

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

## Первый запуск и настройки

При первом запуске **GUI** (пока в настройках не выставлен флаг `firstRunDone`) открывается диалог **«Настройка путей ATEM»** с одним полем:

| Поле | Назначение |
|------|------------|
| **ATEM Software Control** | Папка установки Blackmagic ATEM Switchers или `ATEM Software Control.exe`. Через `BMDSwitcherAPI.dll` и зарегистрированный COM-сервер приложение подключается к пульту. |

Кнопки **«Обзор…»**, **«Автоопределение»** (реестр / типовые пути `Program Files\Blackmagic Design\Blackmagic ATEM Switchers`), индикаторы валидности (✓/✗), **«Сохранить»** и **«Пропустить»**.

**Путь к ATEM SDK** в первом запуске не запрашивается: SDK нужен только при **сборке** (MIDL). Необязательную проверку каталога SDK для разработчиков можно задать в **«Настройки → Дополнительно…»** (на работу с пультом не влияет).

**Хранение:** файл INI в каталоге `QStandardPaths::AppConfigLocation` (Windows: `%AppData%\<org>\<app>\settings.ini`). Ключи: `atem/sdkPath`, `atem/softwareControlPath`, `atem/firstRunDone` (старые `settings.ini` с `sdkPath` остаются совместимыми).

Пути можно сменить в меню **«Настройки → Пути ATEM…»** / **«Дополнительно…»** или из CLI (значения записываются в тот же INI):

```bash
./build/atem-color-ai --atem-control-path "C:/Program Files (x86)/Blackmagic Design/Blackmagic ATEM Switchers"
./build/atem-color-ai --sdk-path "D:/SDK/Blackmagic ATEM Switchers SDK 11.0.1"
./build/atem-color-ai --reset-settings
```

Если путь к **ATEM Software Control** не задан или неверен, приложение **не завершается**: в главном окне показывается предупреждение, **«Применить поправки»** отключена; анализ PNG и GStreamer доступны. Неверный путь к SDK **не** блокирует UI. В CLI выводится предупреждение в лог (`atem.setup`).

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
  gui/             — AtemPathsSetupDialog, AtemAdvancedSettingsDialog, MainWindow (ENABLE_GUI)
  platform/        — QSettings (пути ATEM), валидация, ATEM runtime, GStreamer (Windows)
  main.cpp         — CLI + GUI entry
cmake/
  WindowsPackaging.cmake, AtemSdkMidl.cmake, nsis/
docs/
  color-matching.md, atem-sdk-license-notes.md
```

## Ограничения

- Живое подключение к ATEM и Camera Control на железе — Windows/macOS с runtime Blackmagic.
- `destinationDevice` = id входа ATEM; на реальном сетапе может потребоваться сверка с документацией микшера.
- Авто-iris и hue пока не используются активно; итеративное сведение — вручную (повторный «Захватить/Анализировать»).

## Лицензия

Код проекта — репозиторий. **Blackmagic ATEM SDK** — отдельная лицензия BMD; не коммитить файлы SDK.