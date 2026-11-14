#pragma once

#include <QString>

#ifdef HAVE_GSTREAMER

class GstFrameCapture
{
public:
    /**
     * Capture a single frame from @p sourceUri and write PNG to @p outputPath.
     * @return Empty on success, error message otherwise.
     */
    static QString capturePng(const QString &sourceUri, const QString &outputPath);
};

#endif