#include "ReferenceFrameStore.h"

#include "ColorAnalysis.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>

namespace ReferenceFrameStore {

QString referencesDirectory()
{
    const QString base = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    const QString dir = QDir(base).filePath(QStringLiteral("references"));
    QDir().mkpath(dir);
    return dir;
}

QString saveReference(const QImage &image, const FrameColorAnalysis &analysis, const QString &label,
                      SavedReferenceFrame *out)
{
    if (image.isNull())
        return QStringLiteral("Пустое изображение эталона");
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    QString safeLabel = label.isEmpty() ? QStringLiteral("ref") : label;
    for (QChar &ch : safeLabel) {
        if (!ch.isLetterOrNumber() && ch != QLatin1Char('_') && ch != QLatin1Char('-'))
            ch = QLatin1Char('_');
    }
    const QString baseName = QStringLiteral("%1_%2").arg(safeLabel, stamp);
    const QString pngPath = QDir(referencesDirectory()).filePath(baseName + QStringLiteral(".png"));
    const QString jsonPath = QDir(referencesDirectory()).filePath(baseName + QStringLiteral(".json"));
    if (!image.save(pngPath))
        return QStringLiteral("Не удалось сохранить PNG: %1").arg(pngPath);

    QJsonObject root;
    root.insert(QStringLiteral("label"), label);
    root.insert(QStringLiteral("savedAt"), stamp);
    root.insert(QStringLiteral("sampleCount"), static_cast<double>(analysis.sampleCount));
    root.insert(QStringLiteral("width"), analysis.width);
    root.insert(QStringLiteral("height"), analysis.height);
    root.insert(QStringLiteral("meanR"), analysis.statsR.mean);
    root.insert(QStringLiteral("meanG"), analysis.statsG.mean);
    root.insert(QStringLiteral("meanB"), analysis.statsB.mean);
    root.insert(QStringLiteral("meanY"), analysis.statsY.mean);
    QFile json(jsonPath);
    if (!json.open(QIODevice::WriteOnly))
        return QStringLiteral("Не удалось сохранить статистику: %1").arg(jsonPath);
    json.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    json.close();

    if (out) {
        out->imagePath = pngPath;
        out->sidecarPath = jsonPath;
        out->analysis = analysis;
        out->label = label;
        out->savedAt = stamp;
    }
    return {};
}

QString loadReferenceImage(const QString &imagePath, QImage *image, FrameColorAnalysis *analysis)
{
    QImage img;
    const QString err = ColorAnalysis::loadImage(imagePath, img);
    if (!err.isEmpty())
        return err;
    if (image)
        *image = img;
    if (analysis)
        *analysis = ColorAnalysis::analyze(img);
    return {};
}

QString loadReference(const QString &imagePath, SavedReferenceFrame *out)
{
    if (!out)
        return QStringLiteral("Нет буфера результата");
    QImage img;
    const QString err = loadReferenceImage(imagePath, &img, &out->analysis);
    if (!err.isEmpty())
        return err;
    out->imagePath = imagePath;
    out->label = QFileInfo(imagePath).completeBaseName();
    const QString jsonPath =
        QFileInfo(imagePath).absolutePath() + QLatin1Char('/') + QFileInfo(imagePath).completeBaseName()
        + QStringLiteral(".json");
    out->sidecarPath = jsonPath;
    if (QFile::exists(jsonPath)) {
        QFile f(jsonPath);
        if (f.open(QIODevice::ReadOnly)) {
            const QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
            out->label = root.value(QStringLiteral("label")).toString(out->label);
            out->savedAt = root.value(QStringLiteral("savedAt")).toString();
        }
    }
    Q_UNUSED(img);
    return {};
}

} // namespace ReferenceFrameStore