#pragma once

#include <QString>
#include <QVector>

struct VideoInputOption
{
    QString key;
    QString label;
    QString property;
    int intValue = 0;
};

struct VideoModeOption
{
    QString key;
    QString label;
    int width = 0;
    int height = 0;
    int fpsNum = 0;
    int fpsDen = 1;
    QString format;
    bool interlaced = false;
};

struct VideoDevice
{
    QString id;
    QString displayName;
    QString deviceClass;
    QVector<VideoInputOption> inputs;
    QVector<VideoModeOption> modes;
};
