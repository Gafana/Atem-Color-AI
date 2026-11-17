#pragma once

#include "VideoDevice.h"

#include <QString>
#include <QVector>

struct OpenedVideoSource
{
    /** GstElement*. Released with gst_object_unref by the caller. */
    void *element = nullptr;
    bool hasMode = false;
    /** Source was configured with its own mode property (DeckLink). Do not add a capsfilter. */
    bool hardwareMode = false;
    VideoModeOption mode;
    QString error;
};

namespace GstDevices {

QVector<VideoDevice> enumerate(QString *error);

/**
 * Opens the device the user selected.
 * If @p inputKey or @p modeKey is set and that option is gone, the call fails
 * and does not substitute another input or mode.
 */
OpenedVideoSource openSource(const QString &deviceId, const QString &inputKey,
                             const QString &modeKey);

} // namespace GstDevices
