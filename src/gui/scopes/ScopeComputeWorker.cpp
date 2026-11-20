#include "ScopeComputeWorker.h"

#include "scopes/ScopeMath.h"
#include "scopes/ScopeProcessor.h"
#include "scopes/ScopeRender.h"

#include <QFont>
#include <QFontMetrics>
#include <QPainter>

namespace {

/** Small label in the corner: what the scope is computed from. */
void drawSourceBadge(QImage &image, bool zone)
{
    if (image.width() < 80 || image.height() < 30)
        return;
    if (image.format() != QImage::Format_RGB32 && image.format() != QImage::Format_ARGB32
        && image.format() != QImage::Format_ARGB32_Premultiplied)
        image = image.convertToFormat(QImage::Format_RGB32);
    QPainter p(&image);
    QFont f = p.font();
    f.setPixelSize(qBound(10, image.height() / 22, 14));
    p.setFont(f);
    const QString text = zone ? QStringLiteral("Зона") : QStringLiteral("Весь кадр");
    const QRect tr = p.fontMetrics().boundingRect(text).adjusted(-5, -2, 5, 2);
    const QRect box(image.width() - tr.width() - 6, 4, tr.width(), tr.height());
    p.fillRect(box, QColor(0, 0, 0, 170));
    p.setPen(zone ? QColor(80, 230, 140) : QColor(170, 175, 185));
    p.drawText(box, Qt::AlignCenter, text);
}

} // namespace

ScopeComputeWorker::ScopeComputeWorker(QObject *parent)
    : QObject(parent)
{
}

void ScopeComputeWorker::compute(const ScopeComputeRequest &request)
{
    struct Release
    {
        std::atomic<bool> &flag;
        ~Release() { flag.store(false); }
    } release{m_busy};
    if (request.frameA.isNull())
        return;
    const bool zoneA = !ScopeMath::roiPixelRect(request.frameA.size(), request.roiA).isEmpty();
    const QImage zA = zoneA ? ScopeMath::cropToRoi(request.frameA, request.roiA) : request.frameA;
    const QImage zB = request.frameB.isNull() ? request.frameB : ScopeMath::cropToRoi(request.frameB, request.roiB);
    for (const ScopeInstrumentJob &job : request.instruments) {
        if (job.kind == ScopeKind::None || job.kind == ScopeKind::AiTips)
            continue;
        const bool overlayKind = job.kind == ScopeKind::FalseColor || job.kind == ScopeKind::Zebra;
        ScopeFrameInput input;
        input.frameA = overlayKind ? request.frameA : zA;
        input.frameB = overlayKind ? request.frameB : zB;
        input.plotWidth = job.plotSize.width() >= 16 ? job.plotSize.width() : request.plotWidth;
        input.plotHeight = job.plotSize.height() >= 16 ? job.plotSize.height() : request.plotHeight;
        input.settings = job.settings;
        ScopeFrameOutput out;
        if (job.kind == ScopeKind::CompareWipe)
            out = ScopeRender::compareWipe(
                ScopeMath::scaledRgb32(zA, request.plotWidth),
                zB.isNull() ? QImage() : ScopeMath::scaledRgb32(zB, request.plotWidth),
                request.compareWipe);
        else
            out = ScopeProcessor::render(job.kind, input);
        if (out.image.isNull())
            continue;
        if (overlayKind && zoneA)
            ScopeMath::dimOutsideRoi(out.image, request.roiA);
        if (!overlayKind)
            drawSourceBadge(out.image, zoneA);
        Q_EMIT instrumentResult(job.instanceId, out.image, job.kind);
    }
}