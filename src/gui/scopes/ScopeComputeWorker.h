#pragma once

#include "scopes/ScopeTypes.h"

#include <QImage>
#include <QRectF>
#include <QObject>
#include <QString>
#include <QSize>
#include <QVector>

#include <atomic>

struct ScopeInstrumentJob
{
    QString instanceId;
    ScopeKind kind = ScopeKind::None;
    ScopeDisplaySettings settings;
    QSize plotSize;
};

struct ScopeComputeRequest
{
    QImage frameA;
    QImage frameB;
    QVector<ScopeInstrumentJob> instruments;
    int plotWidth = 640;
    int plotHeight = 360;
    float compareWipe = 0.5f;
    /** Measurement zones (normalized). Empty = whole frame. All scopes are computed from the zone only;
     *  false colour / zebra keep the whole picture but dim everything outside the zone. */
    QRectF roiA;
    QRectF roiB;
};

Q_DECLARE_METATYPE(ScopeComputeRequest)

class ScopeComputeWorker final : public QObject
{
    Q_OBJECT

public:
    explicit ScopeComputeWorker(QObject *parent = nullptr);

    /** Back-pressure: the GUI only queues a new request when the previous one finished. */
    bool tryAcquire() { bool expected = false; return m_busy.compare_exchange_strong(expected, true); }

public Q_SLOTS:
    void compute(const ScopeComputeRequest &request);

private:
    std::atomic<bool> m_busy{false};

public:

Q_SIGNALS:
    void instrumentResult(const QString &instanceId, const QImage &image, ScopeKind kind);
};