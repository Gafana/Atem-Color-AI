# Управление через vMix

В панели «Источник» выбирается **режим управления** — `ATEM (IP пульта)` или `vMix (адрес ПК)`.
Работает один режим за раз (при переключении прежнее подключение закрывается). Выбор хранится в
`settings.ini` (`control/backend`), адрес vMix — `vmix/address` (по умолчанию `localhost:8088`).

## Что нужно в vMix

* Settings → Web Controller: включён, порт 8088 (стандарт). Программа ходит на `http://<адрес>:8088/api`.
* Выход vMix, который идёт на карту захвата: Output 2/3/4 (SDI/NDI), External 2 (виртуальная камера
  «vMix Video») или Fullscreen. Его выбирают в списке «Выход» вместо AUX ATEM. Перед кадром выход
  переключается на вход-камеру и после замера возвращается на программу (`Value=Output`).

## Используемые функции vMix (только документированные)

Источник: [Developer API](https://www.vmix.com/help28/DeveloperAPI.html),
[Shortcut Function Reference](https://www.vmix.com/help28/ShortcutFunctionReference.html),
[Colour Correction](https://www.vmix.com/help28/ColourCorrection.html).

| Что делаем | Функция | Значения |
|---|---|---|
| состояние (входы, preview/active, версия) | `GET /api` (XML) | — |
| Lift R/G/B/Y | `SetCCLiftR` `SetCCLiftG` `SetCCLiftB` `SetCCLiftY` | -1…1, 0 = исходное |
| Gamma R/G/B/Y | `SetCCGammaR` … `SetCCGammaY` | -1…1, 0 = исходное |
| Gain R/G/B/Y | `SetCCGainR` … `SetCCGainY` | 0…2, 1 = исходное |
| насыщенность | `SetCCSaturation` | -1…1, 0 = исходное (-1 = ч/б) |
| оттенок | `SetCCHue` | -1…1 |
| режим «Авто камер» | `ColourCorrectionAuto` | Basic Auto Colour Correction входа |
| выход на карту захвата | `SetOutput2/3/4`, `SetOutputExternal2`, `SetOutputFullscreen/2` | `Value=Input&Input=n`, `Value=Output` |
| программа (двойной клик / программный маршрут) | `ActiveInput` | `Input=n` |

Пример: `http://127.0.0.1:8088/api/?Function=SetCCGainR&Input=2&Value=1.05`.

Соответствие внутренним значениям: Lift/Gamma/Gain/Hue — 1:1 (диапазоны обрезаются по документации),
насыщенность vMix = значение программы − 1. Отправляются только изменившиеся параметры.

## Что работает в vMix и что нет

| | ATEM | vMix |
|---|---|---|
| цвет | баланс белого K/tint + CC Gain | только CC Gain R/G/B |
| уровень | диафрагма, усиление dB, CC Gain Y | только Gain Y |
| Lift / Gamma / насыщенность / Hue | да | да |
| Offset / Contrast / Luma mix | да | нет |
| «Авто камер» | авто ББ (+ авто диафрагма) | `ColourCorrectionAuto` (без диафрагмы) |
| пресеты | свои | свои (пресеты ATEM в режиме vMix не показываются и наоборот) |
| связка камер, выход AUX | свои | свои (номера входов vMix) |

В режиме vMix «цвет через …» и «уровень через …» принудительно CC Gain (выпадающие списки
заблокированы, есть пояснение). «ИИ-подсказки» про пересвет, завал чёрного, экспозицию, шум и усиление
помечаются «В vMix не исправить, крутите камеру».

## Ограничения

* vMix не публикует значения цветокоррекции в XML `/api` (по документации). Программа берёт их из XML,
  если они там есть, иначе — последние отправленные ей самой (кэш `vmix/ccCache`, по GUID входа).
  Если цветокоррекцию крутили руками в vMix, программа об этом не узнает: «Откат» вернёт значения,
  известные программе. После `ColourCorrectionAuto` кэш входа сбрасывается.
* Какой источник сейчас на Output 2/3/4 vMix не сообщает: программа помнит то, что ставила сама, и
  возвращает выход на программу (`Value=Output`).
* Модель Lift/Gamma в vMix может отличаться от Blackmagic: авто-подгонка работает по замеру (замкнутый
  цикл), поэтому сходится, но шаг первого прохода может быть другим.

## Проверка без vMix

`build/fake-vmix.exe [порт] [--expose-cc]` — тестовый «vMix» на `localhost:8088` (XML как в документации,
три входа CAM1/CAM2/NewsHD.xaml, печатает каждый вызов функции). Не устанавливается. Тесты: `tst_vmix`.
