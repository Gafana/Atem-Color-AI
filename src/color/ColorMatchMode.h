#pragma once

#include <QString>
#include <QVector>

/** Manual color tuning strategy (stored in settings.ini as tuning/mode). */
enum class ColorMatchMode
{
    ReferenceCamera,
    ReferenceFrame,
    GrayCard,
    ColorChecker,
    NeutralBalance, ///< automatic: neutral balance + exposure computed by the program
    CameraAverage,
    CameraAuto,     ///< automatic: trigger the camera's own auto WB / auto aperture
    CommonSample,   ///< all cameras look at the same object (white sheet, person): per-camera ROI
};

inline QVector<ColorMatchMode> colorMatchModesForUi()
{
    return {ColorMatchMode::NeutralBalance, ColorMatchMode::CameraAuto,   ColorMatchMode::ReferenceCamera,
            ColorMatchMode::CommonSample,   ColorMatchMode::ColorChecker, ColorMatchMode::GrayCard,
            ColorMatchMode::ReferenceFrame, ColorMatchMode::CameraAverage};
}

inline QString colorMatchModeId(ColorMatchMode mode)
{
    switch (mode) {
    case ColorMatchMode::ReferenceCamera:
        return QStringLiteral("reference_camera");
    case ColorMatchMode::ReferenceFrame:
        return QStringLiteral("reference_frame");
    case ColorMatchMode::GrayCard:
        return QStringLiteral("gray_card");
    case ColorMatchMode::ColorChecker:
        return QStringLiteral("color_checker");
    case ColorMatchMode::NeutralBalance:
        return QStringLiteral("neutral_balance");
    case ColorMatchMode::CameraAverage:
        return QStringLiteral("camera_average");
    case ColorMatchMode::CameraAuto:
        return QStringLiteral("camera_auto");
    case ColorMatchMode::CommonSample:
        return QStringLiteral("common_sample");
    }
    return QStringLiteral("reference_camera");
}

inline ColorMatchMode colorMatchModeFromId(const QString &id)
{
    for (ColorMatchMode m : colorMatchModesForUi())
        if (colorMatchModeId(m) == id)
            return m;
    return ColorMatchMode::ReferenceCamera;
}

inline QString colorMatchModeLabel(ColorMatchMode mode)
{
    switch (mode) {
    case ColorMatchMode::ReferenceCamera:
        return QStringLiteral("По эталонной камере");
    case ColorMatchMode::ReferenceFrame:
        return QStringLiteral("По эталонному кадру (файл)");
    case ColorMatchMode::GrayCard:
        return QStringLiteral("Серая / белая карта (нейтраль)");
    case ColorMatchMode::ColorChecker:
        return QStringLiteral("По таблице цветов");
    case ColorMatchMode::NeutralBalance:
        return QStringLiteral("Авто: баланс + экспозиция (расчёт программы)");
    case ColorMatchMode::CameraAverage:
        return QStringLiteral("К среднему всех камер");
    case ColorMatchMode::CameraAuto:
        return QStringLiteral("Авто: встроенный автобаланс камер");
    case ColorMatchMode::CommonSample:
        return QStringLiteral("По общему образцу (белый лист / человек)");
    }
    return QStringLiteral("По эталонной камере");
}

inline QString colorMatchModeHint(ColorMatchMode mode)
{
    switch (mode) {
    case ColorMatchMode::ReferenceCamera:
        return QStringLiteral("Камеры подгоняются по цвету и яркости под эталонную (сравниваются распределения "
                              "R/G/B по кадру или по выделенной области). Лучше, когда камеры видят похожую сцену.");
    case ColorMatchMode::ReferenceFrame:
        return QStringLiteral("Камеры подгоняются под сохранённый кадр-эталон.");
    case ColorMatchMode::GrayCard:
        return QStringLiteral("Выделите на превью серую/белую карту для каждой камеры: она станет нейтральной, "
                              "уровень — к целевому IRE (если включён «Уровень»).");
    case ColorMatchMode::ColorChecker:
        return QStringLiteral("Отметьте 4 угла таблицы (по часовой, от любого угла — поворот определяется сам). "
                              "Лифт/гамма/гейн подбираются по 24 патчам с известными значениями.");
    case ColorMatchMode::NeutralBalance:
        return QStringLiteral("Как авто в камере: нейтральный баланс по светлым серым участкам и средняя "
                              "экспозиция к целевому IRE, с защитой светов.");
    case ColorMatchMode::CameraAverage:
        return QStringLiteral("Каждая отмеченная камера подгоняется к средней картинке всех отмеченных.");
    case ColorMatchMode::CameraAuto:
        return QStringLiteral("Отправляет камерам команду автобаланса белого (и, по желанию, авто-диафрагмы). "
                              "Захват не нужен: нажмите «Применить».");
    case ColorMatchMode::CommonSample:
        return QStringLiteral("Все камеры смотрят на один объект (белый лист, ведущий). Выделите его на каждой "
                              "камере; цель — эталонная камера, среднее или нейтраль.");
    }
    return {};
}

inline QString colorMatchModeLogName(ColorMatchMode mode)
{
    return colorMatchModeId(mode);
}
