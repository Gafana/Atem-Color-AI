#include "MultiviewGeometry.h"

#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {

MultiviewCell cellAt(double x, double y, double w, double h)
{
    MultiviewCell c;
    c.x = x;
    c.y = y;
    c.w = w;
    c.h = h;
    c.valid = w > 0.0 && h > 0.0;
    return c;
}

MultiviewCell gridCell(int window, int columns, int rows)
{
    if (window < 0 || window >= columns * rows)
        return {};
    const double w = 1.0 / static_cast<double>(columns);
    const double h = 1.0 / static_cast<double>(rows);
    const int col = window % columns;
    const int row = window / columns;
    return cellAt(col * w, row * h, w, h);
}

int smallIndex(int window)
{
    return window - 1;
}

} // namespace

QStringList multiviewLayoutIds()
{
    return {QStringLiteral("2x2"),          QStringLiteral("3x3"),
            QStringLiteral("4x4"),          QStringLiteral("program-top"),
            QStringLiteral("program-bottom"), QStringLiteral("program-left"),
            QStringLiteral("program-right")};
}

QString multiviewLayoutLabel(const QString &layoutId)
{
    if (layoutId == QLatin1String("2x2"))
        return QStringLiteral("2×2 (4 окна)");
    if (layoutId == QLatin1String("3x3"))
        return QStringLiteral("3×3 (9 окон)");
    if (layoutId == QLatin1String("4x4"))
        return QStringLiteral("4×4 (16 окон)");
    if (layoutId == QLatin1String("program-top"))
        return QStringLiteral("Program сверху + 8 окон");
    if (layoutId == QLatin1String("program-bottom"))
        return QStringLiteral("Program снизу + 8 окон");
    if (layoutId == QLatin1String("program-left"))
        return QStringLiteral("Program слева + 8 окон");
    if (layoutId == QLatin1String("program-right"))
        return QStringLiteral("Program справа + 8 окон");
    return layoutId;
}

int multiviewWindowCount(const QString &layoutId)
{
    if (layoutId == QLatin1String("2x2"))
        return 4;
    if (layoutId == QLatin1String("3x3"))
        return 9;
    if (layoutId == QLatin1String("4x4"))
        return 16;
    if (layoutId == QLatin1String("program-top") || layoutId == QLatin1String("program-bottom")
        || layoutId == QLatin1String("program-left") || layoutId == QLatin1String("program-right"))
        return 9;
    return 0;
}

MultiviewCell multiviewCell(const QString &layoutId, int window)
{
    if (layoutId == QLatin1String("2x2"))
        return gridCell(window, 2, 2);
    if (layoutId == QLatin1String("3x3"))
        return gridCell(window, 3, 3);
    if (layoutId == QLatin1String("4x4"))
        return gridCell(window, 4, 4);

    const bool verticalLarge = layoutId == QLatin1String("program-top")
                               || layoutId == QLatin1String("program-bottom");
    const bool horizontalLarge = layoutId == QLatin1String("program-left")
                                 || layoutId == QLatin1String("program-right");
    if (!verticalLarge && !horizontalLarge)
        return {};
    if (window < 0 || window > 8)
        return {};

    if (window == 0) {
        if (layoutId == QLatin1String("program-top"))
            return cellAt(0.0, 0.0, 1.0, 0.5);
        if (layoutId == QLatin1String("program-bottom"))
            return cellAt(0.0, 0.5, 1.0, 0.5);
        if (layoutId == QLatin1String("program-left"))
            return cellAt(0.0, 0.0, 0.5, 1.0);
        return cellAt(0.5, 0.0, 0.5, 1.0);
    }

    const int index = smallIndex(window);
    if (verticalLarge) {
        const int col = index % 4;
        const int row = index / 4;
        const double y0 = layoutId == QLatin1String("program-top") ? 0.5 : 0.0;
        return cellAt(col * 0.25, y0 + row * 0.25, 0.25, 0.25);
    }

    const int col = index % 2;
    const int row = index / 2;
    const double x0 = layoutId == QLatin1String("program-left") ? 0.5 : 0.0;
    return cellAt(x0 + col * 0.25, row * 0.25, 0.25, 0.25);
}

QRect multiviewPixelRect(const QString &layoutId, int window, int imageWidth, int imageHeight)
{
    const MultiviewCell cell = multiviewCell(layoutId, window);
    if (!cell.valid || imageWidth <= 1 || imageHeight <= 1)
        return {};

    const int x = std::clamp(static_cast<int>(std::floor(cell.x * imageWidth)), 0, imageWidth - 1);
    const int y = std::clamp(static_cast<int>(std::floor(cell.y * imageHeight)), 0, imageHeight - 1);
    const int right = std::clamp(static_cast<int>(std::ceil((cell.x + cell.w) * imageWidth)), x + 1, imageWidth);
    const int bottom =
        std::clamp(static_cast<int>(std::ceil((cell.y + cell.h) * imageHeight)), y + 1, imageHeight);

    QRect rect(x, y, right - x, bottom - y);
    const int marginX = std::max(1, static_cast<int>(std::lround(rect.width() * 0.02)));
    const int marginY = std::max(1, static_cast<int>(std::lround(rect.height() * 0.02)));
    rect.adjust(marginX, marginY, -marginX, -marginY);
    if (rect.width() < 2 || rect.height() < 2)
        return {};
    return rect;
}
