#include "SnapshotManager.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

QString SnapshotManager::snapshotsRoot()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    const QString root = QDir(base).filePath(QStringLiteral("snapshots"));
    QDir().mkpath(root);
    return root;
}

SnapshotBundle SnapshotManager::capture(
    const QImage &source, const QHash<QString, QPair<QString, QImage>> &instruments)
{
    SnapshotBundle bundle;
    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HH-mm-ss"));
    bundle.directory = QDir(snapshotsRoot()).filePath(stamp);
    QDir().mkpath(bundle.directory);
    bundle.source = source;
    if (!source.isNull())
        source.save(QDir(bundle.directory).filePath(QStringLiteral("source.png")));
    for (auto it = instruments.cbegin(); it != instruments.cend(); ++it) {
        const QString fileName = it.value().first + QStringLiteral("_") + it.key() + QStringLiteral(".png");
        it.value().second.save(QDir(bundle.directory).filePath(fileName));
        bundle.instruments.insert(it.key(), it.value().second);
    }
    return bundle;
}