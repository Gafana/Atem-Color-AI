# Цветовое сведение камер

Модуль `src/color/` анализирует кадры в sRGB и сравнивает их с эталонной камерой. Поправки переводятся в команды **Blackmagic Camera Control** (группы Video и Color Correction) и отправляются через `IBMDSwitcherCameraControl` ATEM SDK.

## Анализ кадра

Для каждого пикселя в `QImage` (RGB32):

1. Строятся гистограммы каналов \(R,G,B\) и яркости/цветоразностных \(Y,U,V\) (BT.709).
2. Считаются среднее, медиана, перцентили \(p_{2}\), \(p_{98}\) по каждому каналу.
3. Вектороскоп — 2D-гистограмма \(128\times128\) в плоскости \((U,V)\).

\[
Y = 0.2126 R + 0.7152 G + 0.0722 B
\]
\[
U = -0.1146 R - 0.3854 G + 0.5 B + 0.5,\quad
V = 0.5 R - 0.4542 G - 0.0458 B + 0.5
\]
(нормализация в \([0,1]\), бининг в 256 уровней.)

Расстояние гистограмм — L1 по нормированным плотностям (усреднение по RGB).

## Сравнение с эталоном

Для кандидата относительно эталона:

| Метрика | Формула |
|---------|---------|
| ΔY | \(\mathrm{mean}_Y^{cand} - \mathrm{mean}_Y^{ref}\) |
| L1 RGB | среднее \(\sum_i \|p^{ref}_i - p^{cand}_i\|\) по R,G,B |
| UV energy ratio | \(\sqrt{(U_c^2+V_c^2)/(U_r^2+V_r^2)}\) по средним |

## Расчёт поправок

Обозначения: эталон \(ref\), кандидат \(cand\).

**Gain (CC 8.2, каналы RGBY):**

\[
g_R = \mathrm{clamp}( \mathrm{mean}_R^{ref} / \max(\mathrm{mean}_R^{cand}, 1), 0.25, 4 )
\]

аналогично для G, B, Y. Дефолт 1.0.

**Lift (CC 8.0)** — сдвиг теней по \(p_2\):

\[
\mathrm{lift}_R = \mathrm{clamp}\bigl( 4 \cdot (\mathrm{p2}_R^{ref} - \mathrm{p2}_R^{cand}) / 255,\ -2,\ 2 \bigr)
\]

**Gamma (CC 8.1)** — середина тона:

\[
\gamma_R = \mathrm{clamp}\bigl( 2 \cdot \log_2(\mathrm{mean}_R^{ref}/\mathrm{mean}_R^{cand}),\ -4,\ 4 \bigr)
\]

**Saturation (CC 8.6):**

\[
sat = \mathrm{clamp}(1 / ratio_{UV},\ 0.5,\ 1.5),\quad hue = 0
\]

**White balance (Video 1.2):**

\[
K = 5600 + 2500 \cdot (d_B - d_R)/255,\quad tint = \mathrm{clamp}(-d_G/255 \cdot 80,\ -50,\ 50)
\]

где \(d_* = \mathrm{mean}_*^{ref} - \mathrm{mean}_*^{cand}\).

**Gain dB (Video 1.13):**

\[
gainDb = \mathrm{clamp}(\lfloor \Delta Y / 255 \cdot 18 \rfloor,\ -12,\ 12)
\]

Опционально **iris** (Lens 0.2) не выставляется автоматически (\(irisFStopAv < 0\)).

### Демпфирование

Итоговая команда:

\[
v_{send} = v_{neutral} + \alpha (v_{target} - v_{neutral}),\quad \alpha \in [0,1]
\]

По умолчанию \(\alpha = 0.35\) (CLI/GUI). Значения затем **clamp** по спецификации BMD (Lift ±2, Gamma ±4, Gain 0–16, Sat 0–2, WB 2500–10000 K, tint ±50, gain dB ±128).

## Итерации

Рекомендуемый цикл: захват → анализ → применение с \(\alpha < 1\) → повторный захват. Это снижает перерегулирование при расхождении экспозиции и баланса белого.

## ATEM / Camera Control

Отправка: `SetFloats` (категория 8: lift/gamma/gain/color), `SetInt16s` (1.2 WB), `SetInt8s` (1.13 gain dB). `destinationDevice` — id входа ATEM. Перед применением читается текущий bundle (`GetFloats` / `GetInt16s` / `GetInt8s`) для отката.