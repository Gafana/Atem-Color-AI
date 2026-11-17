#pragma once

#include <QHash>
#include <QImage>
#include <QString>

struct SnapshotBundle
{
    QString directory;
    QImage source;
    QHash<QString, QImage> instruments;
};

class SnapshotManager
{
public:
    static QString snapshotsRoot();
    static SnapshotBundle capture(const QImage &source,
                                  const QHash<QString, QPair<QString, QImage>> &instruments);
};