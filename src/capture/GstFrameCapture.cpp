#include "GstFrameCapture.h"

#ifdef HAVE_GSTREAMER

#include <QImage>

#include <gst/gst.h>
#include <gst/app/gstappsink.h>

namespace {

QString buildPipeline(const QString &sourceUri)
{
    QString lower = sourceUri.toLower();
    QString src;

    if (lower.startsWith(QStringLiteral("srt://"))) {
        src = QStringLiteral("srtsrc uri=\"%1\" ! tsdemux ! decodebin").arg(sourceUri);
    } else if (lower.startsWith(QStringLiteral("ndi"))) {
        src = QStringLiteral("ndisrc name=%1 ! decodebin")
                  .arg(sourceUri.mid(4).trimmed());
    } else if (lower.startsWith(QStringLiteral("decklink:"))) {
        const QString device = sourceUri.mid(9).trimmed();
        src = QStringLiteral("decklinkvideosrc device-number=%1 ! decodebin").arg(device);
    } else if (lower.startsWith(QStringLiteral("v4l2:"))) {
        const QString dev = sourceUri.mid(5).trimmed();
        src = QStringLiteral("v4l2src device=%1 ! decodebin").arg(dev);
    } else if (lower.startsWith(QStringLiteral("videotestsrc"))) {
        src = QStringLiteral("videotestsrc num-buffers=1");
    } else {
        src = sourceUri;
    }

    return QStringLiteral("%1 ! videoconvert ! video/x-raw,format=RGB "
                          "! appsink name=sink emit-signals=false max-buffers=1 drop=true")
        .arg(src);
}

} // namespace

QString GstFrameCapture::captureFrame(const QString &sourceUri, QImage &out)
{
    gst_init(nullptr, nullptr);

    const QString pipelineDesc = buildPipeline(sourceUri);
    GError *error = nullptr;
    GstElement *pipeline = gst_parse_launch(pipelineDesc.toUtf8().constData(), &error);
    if (!pipeline) {
        const QString msg = error ? QString::fromUtf8(error->message)
                                  : QStringLiteral("Failed to parse pipeline");
        if (error)
            g_error_free(error);
        return msg;
    }

    GstElement *appsink = gst_bin_get_by_name(GST_BIN(pipeline), "sink");
    if (!appsink) {
        gst_object_unref(pipeline);
        return QStringLiteral("Pipeline has no appsink");
    }

    g_object_set(appsink, "emit-signals", FALSE, "sync", FALSE, nullptr);

    GstStateChangeReturn ret = gst_element_set_state(pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        gst_object_unref(appsink);
        gst_object_unref(pipeline);
        return QStringLiteral("Failed to set pipeline PLAYING");
    }

    QString err;
    GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(appsink), 5 * GST_SECOND);
    if (!sample) {
        err = QStringLiteral("Timeout waiting for video frame (5s)");
    } else {
        GstBuffer *buffer = gst_sample_get_buffer(sample);
        GstCaps *caps = gst_sample_get_caps(sample);
        GstStructure *s = gst_caps_get_structure(caps, 0);
        int width = 0;
        int height = 0;
        gst_structure_get_int(s, "width", &width);
        gst_structure_get_int(s, "height", &height);

        GstMapInfo map;
        if (gst_buffer_map(buffer, &map, GST_MAP_READ)) {
            out = QImage(map.data, width, height, width * 3, QImage::Format_RGB888).copy();
            gst_buffer_unmap(buffer, &map);
        } else {
            err = QStringLiteral("Failed to map GStreamer buffer");
        }
        gst_sample_unref(sample);
    }

    gst_element_set_state(pipeline, GST_STATE_NULL);
    gst_object_unref(appsink);
    gst_object_unref(pipeline);
    return err;
}

QString GstFrameCapture::capturePng(const QString &sourceUri, const QString &outputPath)
{
    QImage image;
    const QString err = captureFrame(sourceUri, image);
    if (!err.isEmpty())
        return err;
    if (!image.save(outputPath, "PNG"))
        return QStringLiteral("Failed to write PNG to %1").arg(outputPath);
    return {};
}

#endif