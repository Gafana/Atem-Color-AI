#pragma once

#include "ColorTypes.h"

#include <QString>

struct SavedReferenceFrame
{
    QString imagePath;
    QString sidecarPath;
    FrameColorAnalysis analysis;
    QString label;
    QString savedAt;
};

namespace ReferenceFrameStore {

QString referencesDirectory();

/** Saves PNG and JSON sidecar with analysis; returns error or empty. */
QString saveReference(const QImage &image, const FrameColorAnalysis &analysis, const QString &label,
                      SavedReferenceFrame *out);

QString loadReference(const QString &imagePath, SavedReferenceFrame *out);
QString loadReferenceImage(const QString &imagePath, QImage *image, FrameColorAnalysis *analysis);

} // namespace ReferenceFrameStore