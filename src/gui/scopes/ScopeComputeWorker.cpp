#include "ScopeComputeWorker.h"

#include "scopes/ScopeMath.h"
#include "scopes/ScopeProcessor.h"
#include "scopes/ScopeRender.h"

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
    for (const ScopeInstrumentJob &job : request.instruments) {
        if (job.kind == ScopeKind::None)
            continue;
        ScopeFrameInput input;
        input.frameA = request.frameA;
        input.frameB = request.frameB;
        input.plotWidth = job.plotSize.width() >= 16 ? job.plotSize.width() : request.plotWidth;
        input.plotHeight = job.plotSize.height() >= 16 ? job.plotSize.height() : request.plotHeight;
        input.settings = job.settings;
        ScopeFrameOutput out;
        if (job.kind == ScopeKind::CompareWipe)
            out = ScopeRender::compareWipe(
                ScopeMath::scaledRgb32(request.frameA, request.plotWidth),
                request.frameB.isNull() ? QImage() : ScopeMath::scaledRgb32(request.frameB, request.plotWidth),
                request.compareWipe);
        else
            out = ScopeProcessor::render(job.kind, input);
        if (!out.image.isNull())
            Q_EMIT instrumentResult(job.instanceId, out.image, job.kind);
    }
}