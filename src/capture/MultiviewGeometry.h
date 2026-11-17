#pragma once

#include <QRect>
#include <QString>
#include <QStringList>

struct MultiviewCell
{
    double x = 0;
    double y = 0;
    double w = 0;
    double h = 0;
    bool valid = false;
};

QStringList multiviewLayoutIds();
QString multiviewLayoutLabel(const QString &layoutId);
int multiviewWindowCount(const QString &layoutId);

/** Normalized cell (0..1) for a multiview window. Invalid when the index is out of range. */
MultiviewCell multiviewCell(const QString &layoutId, int window);

/**
 * Pixel rect of a window inside a captured multiview frame, inset so borders and labels
 * stay out of the color sample. Empty when the layout or window is not valid.
 */
QRect multiviewPixelRect(const QString &layoutId, int window, int imageWidth, int imageHeight);
