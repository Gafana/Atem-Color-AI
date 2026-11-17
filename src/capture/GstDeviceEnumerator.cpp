#include "GstDeviceEnumerator.h"

#ifdef HAVE_GSTREAMER

#include <QSet>
#include <algorithm>

#include <gst/gst.h>

namespace {

QString fromGst(const gchar *text)
{
    return text ? QString::fromUtf8(text) : QString();
}

QString takeString(gchar *text)
{
    const QString value = fromGst(text);
    g_free(text);
    return value;
}

void ensureGStreamer(QString *error)
{
    GError *gstError = nullptr;
    if (!gst_init_check(nullptr, nullptr, &gstError)) {
        if (error) {
            *error = gstError ? QString::fromUtf8(gstError->message)
                              : QStringLiteral("GStreamer не инициализировался");
        }
        if (gstError)
            g_error_free(gstError);
        return;
    }
    if (error)
        error->clear();
}

QString deviceIdOf(GstDevice *device, const QString &displayName)
{
    QString api;
    QString ident;
    GstStructure *props = gst_device_get_properties(device);
    if (props) {
        api = fromGst(gst_structure_get_string(props, "device.api"));
        ident = fromGst(gst_structure_get_string(props, "device.path"));
        if (ident.isEmpty())
            ident = fromGst(gst_structure_get_string(props, "device.guid"));
        if (ident.isEmpty())
            ident = fromGst(gst_structure_get_string(props, "device.name"));
        gst_structure_free(props);
    }
    if (ident.isEmpty())
        ident = displayName;
    if (api.isEmpty())
        return ident;
    return api + QLatin1Char('|') + ident;
}

QString modeKey(int width, int height, int fpsNum, int fpsDen, const QString &format, bool interlaced)
{
    return QStringLiteral("%1x%2@%3/%4:%5:%6")
        .arg(width)
        .arg(height)
        .arg(fpsNum)
        .arg(fpsDen)
        .arg(format)
        .arg(interlaced ? QLatin1Char('i') : QLatin1Char('p'));
}

QString modeLabel(int width, int height, int fpsNum, int fpsDen, const QString &format, bool fpsIsMax,
                  bool interlaced)
{
    QString label = interlaced ? QStringLiteral("%1×%2i").arg(width).arg(height)
                               : QStringLiteral("%1×%2").arg(width).arg(height);
    if (fpsNum > 0 && fpsDen > 0) {
        const double fps = static_cast<double>(fpsNum) / static_cast<double>(fpsDen);
        label += fpsIsMax ? QStringLiteral(" до %1 кадр/с").arg(fps, 0, 'f', 2)
                          : QStringLiteral(" @ %1 кадр/с").arg(fps, 0, 'f', 2);
    }
    if (!format.isEmpty())
        label += QStringLiteral(" (%1)").arg(format);
    return label;
}

void appendMode(VideoDevice &info, int width, int height, int fpsNum, int fpsDen,
                const QString &format, bool fpsIsMax, bool interlaced, QSet<QString> *seen)
{
    if (width <= 0 || height <= 0)
        return;
    const int den = fpsDen > 0 ? fpsDen : 1;
    const QString key = modeKey(width, height, fpsNum, den, format, interlaced);
    if (seen->contains(key))
        return;
    seen->insert(key);
    VideoModeOption mode;
    mode.key = key;
    mode.label = modeLabel(width, height, fpsNum, den, format, fpsIsMax, interlaced);
    mode.width = width;
    mode.height = height;
    mode.fpsNum = fpsNum;
    mode.fpsDen = den;
    mode.format = format;
    mode.interlaced = interlaced;
    info.modes.push_back(mode);
}

void readFraction(const GValue *value, int *num, int *den)
{
    if (!value || !GST_VALUE_HOLDS_FRACTION(value))
        return;
    *num = gst_value_get_fraction_numerator(value);
    *den = gst_value_get_fraction_denominator(value);
}

void collectModes(GstDevice *device, VideoDevice &info)
{
    GstCaps *caps = gst_device_get_caps(device);
    if (!caps)
        return;

    QSet<QString> seen;
    const guint size = gst_caps_get_size(caps);
    for (guint i = 0; i < size; ++i) {
        const GstStructure *structure = gst_caps_get_structure(caps, i);
        if (!structure)
            continue;
        const QString media = fromGst(gst_structure_get_name(structure));
        if (!media.startsWith(QStringLiteral("video/")))
            continue;
        if (!gst_structure_has_field_typed(structure, "width", G_TYPE_INT)
            || !gst_structure_has_field_typed(structure, "height", G_TYPE_INT))
            continue;

        int width = 0;
        int height = 0;
        gst_structure_get_int(structure, "width", &width);
        gst_structure_get_int(structure, "height", &height);
        const QString format = fromGst(gst_structure_get_string(structure, "format"));
        const QString scan = fromGst(gst_structure_get_string(structure, "interlace-mode"));
        const bool interlaced = scan == QLatin1String("interleaved") || scan == QLatin1String("mixed")
                                || scan == QLatin1String("fields");

        const GValue *fps = gst_structure_get_value(structure, "framerate");
        if (fps && GST_VALUE_HOLDS_FRACTION(fps)) {
            int num = 0;
            int den = 1;
            readFraction(fps, &num, &den);
            appendMode(info, width, height, num, den, format, false, interlaced, &seen);
        } else if (fps && GST_VALUE_HOLDS_LIST(fps)) {
            const guint count = gst_value_list_get_size(fps);
            for (guint f = 0; f < count; ++f) {
                int num = 0;
                int den = 1;
                readFraction(gst_value_list_get_value(fps, f), &num, &den);
                appendMode(info, width, height, num, den, format, false, interlaced, &seen);
            }
        } else if (fps && GST_VALUE_HOLDS_FRACTION_RANGE(fps)) {
            int num = 0;
            int den = 1;
            readFraction(gst_value_get_fraction_range_max(fps), &num, &den);
            appendMode(info, width, height, num, den, format, true, interlaced, &seen);
        } else {
            appendMode(info, width, height, 0, 1, format, false, interlaced, &seen);
        }
    }
    gst_caps_unref(caps);

    std::sort(info.modes.begin(), info.modes.end(), [](const VideoModeOption &a, const VideoModeOption &b) {
        const int areaA = a.width * a.height;
        const int areaB = b.width * b.height;
        if (areaA != areaB)
            return areaA > areaB;
        const double fpsA = a.fpsDen > 0 ? static_cast<double>(a.fpsNum) / a.fpsDen : 0.0;
        const double fpsB = b.fpsDen > 0 ? static_cast<double>(b.fpsNum) / b.fpsDen : 0.0;
        if (fpsA != fpsB)
            return fpsA > fpsB;
        return a.format < b.format;
    });
    constexpr int kMaxModes = 96;
    if (info.modes.size() > kMaxModes)
        info.modes.resize(kMaxModes);
}

bool isInputProperty(const char *name)
{
    const QString property = QString::fromUtf8(name ? name : "");
    return property == QLatin1String("connection") || property == QLatin1String("connector")
           || property == QLatin1String("video-input") || property == QLatin1String("input");
}

void collectInputs(GstDevice *device, VideoDevice &info)
{
    GstElement *element = gst_device_create_element(device, nullptr);
    if (!element)
        return;

    guint count = 0;
    GParamSpec **specs = g_object_class_list_properties(G_OBJECT_GET_CLASS(element), &count);
    for (guint i = 0; i < count; ++i) {
        GParamSpec *spec = specs[i];
        if (!spec || !(spec->flags & G_PARAM_WRITABLE) || !G_IS_PARAM_SPEC_ENUM(spec))
            continue;
        if (!isInputProperty(spec->name))
            continue;
        GEnumClass *enumClass = G_PARAM_SPEC_ENUM(spec)->enum_class;
        if (!enumClass)
            continue;
        for (guint v = 0; v < enumClass->n_values; ++v) {
            const GEnumValue &value = enumClass->values[v];
            VideoInputOption option;
            option.property = QString::fromUtf8(spec->name);
            option.intValue = value.value;
            option.key = option.property + QLatin1Char('=') + QString::number(option.intValue);
            const char *nick = value.value_nick ? value.value_nick : value.value_name;
            option.label = QString::fromUtf8(nick ? nick : "input");
            if (count > 0)
                info.inputs.push_back(option);
        }
    }
    g_free(specs);
    gst_object_unref(element);
}

VideoDevice describeDevice(GstDevice *device)
{
    VideoDevice info;
    info.displayName = takeString(gst_device_get_display_name(device));
    info.deviceClass = takeString(gst_device_get_device_class(device));
    info.id = deviceIdOf(device, info.displayName);
    if (info.displayName.isEmpty())
        info.displayName = info.id;

    QString api;
    GstStructure *props = gst_device_get_properties(device);
    if (props) {
        api = fromGst(gst_structure_get_string(props, "device.api"));
        gst_structure_free(props);
    }
    if (!api.isEmpty())
        info.displayName = QStringLiteral("%1 [%2]").arg(info.displayName, api);

    collectInputs(device, info);
    collectModes(device, info);
    return info;
}

const VideoInputOption *findInput(const VideoDevice &device, const QString &key)
{
    for (const VideoInputOption &input : device.inputs) {
        if (input.key == key)
            return &input;
    }
    return nullptr;
}

const VideoModeOption *findMode(const VideoDevice &device, const QString &key)
{
    const VideoModeOption *legacyProgressive = nullptr;
    const VideoModeOption *legacyAny = nullptr;
    for (const VideoModeOption &mode : device.modes) {
        if (mode.key == key)
            return &mode;
        if ((mode.key.endsWith(QLatin1String(":p")) || mode.key.endsWith(QLatin1String(":i")))
            && mode.key.left(mode.key.size() - 2) == key) {
            if (!mode.interlaced)
                legacyProgressive = &mode;
            else if (!legacyAny)
                legacyAny = &mode;
        }
    }
    return legacyProgressive ? legacyProgressive : legacyAny;
}

bool sameFps(int num, int den, int wantNum, int wantDen)
{
    if (den <= 0 || wantDen <= 0)
        return false;
    return static_cast<long long>(num) * wantDen == static_cast<long long>(wantNum) * den;
}

QByteArray decklinkModeNick(const VideoModeOption &mode)
{
    const int w = mode.width;
    const int h = mode.height;
    const int n = mode.fpsNum;
    const int d = mode.fpsDen;
    const bool i = mode.interlaced;

    if (w == 720 && h == 486 && i && sameFps(n, d, 30000, 1001))
        return "ntsc";
    if (w == 720 && h == 576 && i && sameFps(n, d, 25, 1))
        return "pal";
    if (w == 1280 && h == 720 && !i) {
        if (sameFps(n, d, 50, 1))
            return "720p50";
        if (sameFps(n, d, 60000, 1001))
            return "720p5994";
        if (sameFps(n, d, 60, 1))
            return "720p60";
    }
    if (w == 1920 && h == 1080 && i) {
        if (sameFps(n, d, 25, 1))
            return "1080i50";
        if (sameFps(n, d, 30000, 1001))
            return "1080i5994";
        if (sameFps(n, d, 30, 1))
            return "1080i60";
    }
    if (w == 1920 && h == 1080 && !i) {
        if (sameFps(n, d, 24000, 1001))
            return "1080p2398";
        if (sameFps(n, d, 24, 1))
            return "1080p24";
        if (sameFps(n, d, 25, 1))
            return "1080p25";
        if (sameFps(n, d, 30000, 1001))
            return "1080p2997";
        if (sameFps(n, d, 30, 1))
            return "1080p30";
        if (sameFps(n, d, 50, 1))
            return "1080p50";
        if (sameFps(n, d, 60000, 1001))
            return "1080p5994";
        if (sameFps(n, d, 60, 1))
            return "1080p60";
    }
    if (w == 3840 && h == 2160 && !i) {
        if (sameFps(n, d, 24000, 1001))
            return "2160p2398";
        if (sameFps(n, d, 24, 1))
            return "2160p24";
        if (sameFps(n, d, 25, 1))
            return "2160p25";
        if (sameFps(n, d, 30000, 1001))
            return "2160p2997";
        if (sameFps(n, d, 30, 1))
            return "2160p30";
        if (sameFps(n, d, 50, 1))
            return "2160p50";
        if (sameFps(n, d, 60000, 1001))
            return "2160p5994";
        if (sameFps(n, d, 60, 1))
            return "2160p60";
    }
    if (w == 4096 && h == 2160 && !i) {
        if (sameFps(n, d, 24000, 1001))
            return "4kdcip2398";
        if (sameFps(n, d, 24, 1))
            return "4kdcip24";
        if (sameFps(n, d, 25, 1))
            return "4kdcip25";
        if (sameFps(n, d, 30000, 1001))
            return "4kdcip2997";
        if (sameFps(n, d, 30, 1))
            return "4kdcip30";
        if (sameFps(n, d, 50, 1))
            return "4kdcip50";
        if (sameFps(n, d, 60000, 1001))
            return "4kdcip5994";
        if (sameFps(n, d, 60, 1))
            return "4kdcip60";
    }
    return {};
}

bool applyDecklinkMode(GstElement *element, const VideoModeOption &mode)
{
    GstElementFactory *factory = gst_element_get_factory(element);
    if (!factory || g_strcmp0(GST_OBJECT_NAME(factory), "decklinkvideosrc") != 0)
        return false;
    const QByteArray nick = decklinkModeNick(mode);
    if (nick.isEmpty())
        return false;
    GParamSpec *spec = g_object_class_find_property(G_OBJECT_GET_CLASS(element), "mode");
    if (!spec || !G_IS_PARAM_SPEC_ENUM(spec))
        return false;
    GEnumValue *value = g_enum_get_value_by_nick(G_PARAM_SPEC_ENUM(spec)->enum_class, nick.constData());
    if (!value)
        return false;
    g_object_set(element, "mode", value->value, nullptr);
    return true;
}

} // namespace

namespace GstDevices {

QVector<VideoDevice> enumerate(QString *error)
{
    QVector<VideoDevice> devices;
    ensureGStreamer(error);
    if (error && !error->isEmpty())
        return devices;

    GstDeviceMonitor *monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);
    if (!gst_device_monitor_start(monitor)) {
        gst_object_unref(monitor);
        if (error)
            *error = QStringLiteral("Не удалось запустить GstDeviceMonitor");
        return devices;
    }

    GList *list = gst_device_monitor_get_devices(monitor);
    QSet<QString> seenIds;
    for (GList *it = list; it; it = it->next) {
        GstDevice *device = GST_DEVICE(it->data);
        if (!device)
            continue;
        VideoDevice info = describeDevice(device);
        gst_object_unref(device);
        if (info.id.isEmpty() || seenIds.contains(info.id))
            continue;
        seenIds.insert(info.id);
        devices.push_back(info);
    }
    g_list_free(list);
    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);

    std::sort(devices.begin(), devices.end(), [](const VideoDevice &a, const VideoDevice &b) {
        return QString::localeAwareCompare(a.displayName, b.displayName) < 0;
    });
    return devices;
}

OpenedVideoSource openSource(const QString &deviceId, const QString &inputKey, const QString &modeKey)
{
    OpenedVideoSource opened;
    if (deviceId.isEmpty()) {
        opened.error = QStringLiteral("Устройство захвата не выбрано");
        return opened;
    }

    QString enumError;
    ensureGStreamer(&enumError);
    if (!enumError.isEmpty()) {
        opened.error = enumError;
        return opened;
    }

    GstDeviceMonitor *monitor = gst_device_monitor_new();
    gst_device_monitor_add_filter(monitor, "Video/Source", nullptr);
    if (!gst_device_monitor_start(monitor)) {
        gst_object_unref(monitor);
        opened.error = QStringLiteral("Не удалось запустить GstDeviceMonitor");
        return opened;
    }

    GList *list = gst_device_monitor_get_devices(monitor);
    bool found = false;
    for (GList *it = list; it && opened.error.isEmpty() && !opened.element; it = it->next) {
        GstDevice *device = GST_DEVICE(it->data);
        if (!device)
            continue;
        const VideoDevice info = describeDevice(device);
        if (info.id != deviceId)
            continue;
        found = true;

        if (!inputKey.isEmpty() && !findInput(info, inputKey)) {
            opened.error = QStringLiteral(
                               "Вход «%1» больше не доступен на устройстве. Другой вход не выбирается.")
                               .arg(inputKey);
            break;
        }
        if (!modeKey.isEmpty() && !findMode(info, modeKey)) {
            opened.error = QStringLiteral(
                               "Режим «%1» больше не доступен на устройстве. Другой режим не выбирается.")
                               .arg(modeKey);
            break;
        }

        GstElement *element = gst_device_create_element(device, "videosrc");
        if (!element) {
            opened.error = QStringLiteral("Не удалось открыть устройство «%1»").arg(info.displayName);
            break;
        }

        if (!inputKey.isEmpty()) {
            const VideoInputOption *input = findInput(info, inputKey);
            const QByteArray property = input->property.toUtf8();
            g_object_set(element, property.constData(), input->intValue, nullptr);
        }
        if (!modeKey.isEmpty()) {
            opened.hasMode = true;
            opened.mode = *findMode(info, modeKey);
            opened.hardwareMode = applyDecklinkMode(element, opened.mode);
        }
        opened.element = element;
    }
    for (GList *it = list; it; it = it->next) {
        if (it->data)
            gst_object_unref(it->data);
    }
    g_list_free(list);
    gst_device_monitor_stop(monitor);
    gst_object_unref(monitor);

    if (!found && opened.error.isEmpty())
        opened.error = QStringLiteral("Устройство захвата не найдено. Другое устройство не выбирается.");
    return opened;
}

} // namespace GstDevices

#endif
