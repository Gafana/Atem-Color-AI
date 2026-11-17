#include "GstLivePreview.h"

#ifdef HAVE_GSTREAMER

#include "GstDeviceEnumerator.h"
#include "color/ColorAnalysis.h"

#include <QElapsedTimer>

#include <cstring>

#include <gst/app/gstappsink.h>
#include <gst/gst.h>

namespace {

QImage scaledBox(const QImage &image, int boxWidth, int boxHeight)
{
    if (image.isNull() || (image.width() <= boxWidth && image.height() <= boxHeight))
        return image;
    return image.scaled(boxWidth, boxHeight, Qt::KeepAspectRatio, Qt::FastTransformation);
}

QImage imageFromSample(GstSample *sample)
{
    if (!sample)
        return {};
    GstCaps *caps = gst_sample_get_caps(sample);
    GstBuffer *buffer = gst_sample_get_buffer(sample);
    if (!caps || !buffer)
        return {};
    const GstStructure *structure = gst_caps_get_structure(caps, 0);
    int width = 0;
    int height = 0;
    if (!structure || !gst_structure_get_int(structure, "width", &width)
        || !gst_structure_get_int(structure, "height", &height) || width <= 0 || height <= 0)
        return {};

    GstMapInfo map;
    if (!gst_buffer_map(buffer, &map, GST_MAP_READ))
        return {};

    const int rowBytes = width * 3;
    int stride = rowBytes;
    if (map.size >= static_cast<gsize>(height) * static_cast<gsize>(rowBytes)
        && map.size % static_cast<gsize>(height) == 0) {
        const int candidate = static_cast<int>(map.size / static_cast<gsize>(height));
        if (candidate >= rowBytes)
            stride = candidate;
    }

    QImage image(width, height, QImage::Format_RGB888);
    if (image.isNull()) {
        gst_buffer_unmap(buffer, &map);
        return {};
    }
    for (int y = 0; y < height; ++y) {
        const gsize offset = static_cast<gsize>(y) * static_cast<gsize>(stride);
        if (offset + static_cast<gsize>(rowBytes) > map.size)
            break;
        memcpy(image.scanLine(y), map.data + offset, static_cast<size_t>(rowBytes));
    }
    gst_buffer_unmap(buffer, &map);
    return image;
}

void unrefElement(GstElement *element)
{
    if (element)
        gst_object_unref(element);
}

} // namespace

GstLivePreview::GstLivePreview(QObject *parent)
    : QThread(parent)
{
    qRegisterMetaType<FrameColorAnalysis>();
}

GstLivePreview::~GstLivePreview()
{
    stopPreview();
}

void GstLivePreview::startDevice(const QString &deviceId, const QString &inputKey, const QString &modeKey)
{
    stopPreview();
    if (isRunning()) {
        Q_EMIT previewError(QStringLiteral("Предыдущий поток захвата ещё не остановился"));
        return;
    }
    m_deviceId = deviceId;
    m_inputKey = inputKey;
    m_modeKey = modeKey;
    {
        QMutexLocker lock(&m_mutex);
        m_latest = QImage();
        m_generation = 0;
    }
    m_stop.store(false);
    start();
}

void GstLivePreview::stopPreview()
{
    m_stop.store(true);
    m_wait.wakeAll();
    if (isRunning() && QThread::currentThread() != this)
        wait(5000);
}

quint64 GstLivePreview::frameGeneration() const
{
    QMutexLocker lock(&m_mutex);
    return m_generation;
}

bool GstLivePreview::copyLatest(QImage &out) const
{
    QMutexLocker lock(&m_mutex);
    if (m_latest.isNull())
        return false;
    out = m_latest;
    return true;
}

bool GstLivePreview::waitForNewerFrame(quint64 afterGeneration, int timeoutMs, QImage &out,
                                       quint64 *gotGeneration, const std::atomic<bool> *cancel)
{
    QElapsedTimer timer;
    timer.start();
    QMutexLocker lock(&m_mutex);
    while (!m_stop.load() && !(cancel && cancel->load())) {
        if (m_generation > afterGeneration && !m_latest.isNull()) {
            out = m_latest;
            if (gotGeneration)
                *gotGeneration = m_generation;
            return true;
        }
        const int left = timeoutMs - static_cast<int>(timer.elapsed());
        if (left <= 0)
            return false;
        m_wait.wait(&m_mutex, static_cast<unsigned long>(qMax(1, left)));
    }
    return false;
}

void GstLivePreview::run()
{
    const OpenedVideoSource opened = GstDevices::openSource(m_deviceId, m_inputKey, m_modeKey);
    if (!opened.element) {
        Q_EMIT previewError(opened.error.isEmpty()
                                ? QStringLiteral("Не удалось открыть устройство захвата")
                                : opened.error);
        return;
    }

    GstElement *source = static_cast<GstElement *>(opened.element);
    GstElement *pipeline = gst_pipeline_new("atem-preview");
    GstElement *convert = gst_element_factory_make("videoconvert", "convert");
    GstElement *scale = gst_element_factory_make("videoscale", "scale");
    GstElement *rgbCaps = gst_element_factory_make("capsfilter", "rgb");
    GstElement *sink = gst_element_factory_make("appsink", "sink");
    // DeckLink starts in NTSC until the signal locks. A capsfilter for the chosen
    // mode rejects that first frame and the pipeline dies with not-negotiated
    // ("Internal data stream error") before the real picture arrives. The mode
    // property is set instead, so the card opens directly in the selected mode.
    const bool wantModeCaps = opened.hasMode && !opened.hardwareMode;
    GstElement *modeCaps = wantModeCaps ? gst_element_factory_make("capsfilter", "mode") : nullptr;

    QString error;
    const auto missing = [&](GstElement *element, const char *name) {
        if (element)
            return false;
        if (error.isEmpty())
            error = QStringLiteral("Нет элемента GStreamer «%1»").arg(QString::fromLatin1(name));
        return true;
    };
    const bool partsOk = pipeline && !missing(convert, "videoconvert") && !missing(scale, "videoscale")
                         && !missing(rgbCaps, "capsfilter") && !missing(sink, "appsink")
                         && (!wantModeCaps || !missing(modeCaps, "capsfilter"));
    if (!partsOk) {
        unrefElement(source);
        unrefElement(convert);
        unrefElement(scale);
        unrefElement(rgbCaps);
        unrefElement(sink);
        unrefElement(modeCaps);
        unrefElement(pipeline);
        Q_EMIT previewError(error.isEmpty() ? QStringLiteral("Не удалось собрать пайплайн превью") : error);
        return;
    }

    if (opened.hasMode && modeCaps) {
        GstCaps *caps = gst_caps_new_empty_simple("video/x-raw");
        gst_caps_set_simple(caps, "width", G_TYPE_INT, opened.mode.width, "height", G_TYPE_INT,
                            opened.mode.height, nullptr);
        if (!opened.mode.format.isEmpty()) {
            const QByteArray format = opened.mode.format.toUtf8();
            gst_caps_set_simple(caps, "format", G_TYPE_STRING, format.constData(), nullptr);
        }
        if (opened.mode.fpsNum > 0 && opened.mode.fpsDen > 0) {
            gst_caps_set_simple(caps, "framerate", GST_TYPE_FRACTION, opened.mode.fpsNum,
                                opened.mode.fpsDen, nullptr);
        }
        g_object_set(modeCaps, "caps", caps, nullptr);
        gst_caps_unref(caps);
    }

    GstCaps *rgb = gst_caps_from_string("video/x-raw,format=RGB,width=[1,1920]");
    g_object_set(rgbCaps, "caps", rgb, nullptr);
    gst_caps_unref(rgb);
    g_object_set(sink, "emit-signals", FALSE, "sync", FALSE, "max-buffers", 1, "drop", TRUE, nullptr);

    GstElement *src = source;
    gst_bin_add(GST_BIN(pipeline), src);
    source = nullptr;
    gst_bin_add_many(GST_BIN(pipeline), convert, scale, rgbCaps, sink, nullptr);
    if (modeCaps)
        gst_bin_add(GST_BIN(pipeline), modeCaps);

    const bool linked = modeCaps
                            ? gst_element_link_many(src, modeCaps, convert, scale, rgbCaps, sink, nullptr)
                            : gst_element_link_many(src, convert, scale, rgbCaps, sink, nullptr);
    if (!linked)
        error = QStringLiteral(
            "Выбранный режим не принят устройством. Другой режим автоматически не включается.");

    GstBus *bus = nullptr;
    if (error.isEmpty()) {
        bus = gst_element_get_bus(pipeline);
        if (gst_element_set_state(pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
            error = QStringLiteral(
                "Устройство не перешло в воспроизведение. Выбранный режим не подменяется другим.");
        }
    }

    QElapsedTimer emitClock;
    emitClock.start();
    qint64 lastEmitMs = -1000;
    QElapsedTimer firstFrame;
    firstFrame.start();
    bool gotFrame = false;

    while (error.isEmpty() && !m_stop.load()) {
        if (bus) {
            GstMessage *message = gst_bus_pop_filtered(
                bus, static_cast<GstMessageType>(GST_MESSAGE_ERROR | GST_MESSAGE_EOS));
            if (message) {
                if (GST_MESSAGE_TYPE(message) == GST_MESSAGE_ERROR) {
                    GError *gstError = nullptr;
                    gchar *debug = nullptr;
                    gst_message_parse_error(message, &gstError, &debug);
                    error = gstError ? QString::fromUtf8(gstError->message)
                                     : QStringLiteral("Ошибка GStreamer");
                    const QString detail = debug ? QString::fromUtf8(debug) : QString();
                    if (detail.contains(QLatin1String("not-negotiated"))) {
                        error += QStringLiteral(
                            ". Устройство не приняло выбранный режим или карта занята другой программой "
                            "(OmniScope, Media Express, OBS) — режим не подменяется.");
                    }
                    if (gstError)
                        g_error_free(gstError);
                    g_free(debug);
                } else {
                    error = QStringLiteral("Поток устройства завершился");
                }
                gst_message_unref(message);
                break;
            }
        }

        GstSample *sample = gst_app_sink_try_pull_sample(GST_APP_SINK(sink), 50 * GST_MSECOND);
        if (!sample) {
            if (!gotFrame && firstFrame.elapsed() > 5000) {
                error = QStringLiteral(
                    "Нет кадра от выбранного устройства в течение 5 с. Режим не подменяется.");
            }
            continue;
        }

        const QImage full = imageFromSample(sample);
        gst_sample_unref(sample);
        if (full.isNull())
            continue;
        gotFrame = true;

        {
            QMutexLocker lock(&m_mutex);
            m_latest = full;
            ++m_generation;
            m_wait.wakeAll();
        }

        if (emitClock.elapsed() - lastEmitMs < 66)
            continue;
        lastEmitMs = emitClock.elapsed();
        Q_EMIT frameReady(scaledBox(full, 960, 540));
        Q_EMIT analysisReady(ColorAnalysis::analyze(scaledBox(full, 480, 270)));
    }

    if (bus)
        gst_object_unref(bus);
    if (pipeline) {
        gst_element_set_state(pipeline, GST_STATE_NULL);
        gst_object_unref(pipeline);
    }
    unrefElement(source);

    if (!error.isEmpty())
        Q_EMIT previewError(error);
}

#endif
