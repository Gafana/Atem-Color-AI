#pragma once

#include <QString>

class QImage;

#ifdef HAVE_GSTREAMER

class GstFrameCapture
{
public:
    /**
     * Capture a single frame from @p sourceUri.
     * @return Empty on success, error message otherwise.
     */
    static QString captureFrame(const QString &sourceUri, QImage &out);

    /**
     * Capture a single frame from @p sourceUri and write PNG to @p outputPath.
     * @return Empty on success, error message otherwise.
     */
    static QString capturePng(const QString &sourceUri, const QString &outputPath);
};

#endif