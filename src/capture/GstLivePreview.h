#pragma once

#include "color/ColorTypes.h"

#include <QImage>
#include <QMutex>
#include <QThread>
#include <QWaitCondition>

#include <atomic>

class GstLivePreview final : public QThread
{
    Q_OBJECT
public:
    explicit GstLivePreview(QObject *parent = nullptr);
    ~GstLivePreview() override;

    void startDevice(const QString &deviceId, const QString &inputKey, const QString &modeKey);
    void stopPreview();

    quint64 frameGeneration() const;
    bool copyLatest(QImage &out) const;
    bool waitForNewerFrame(quint64 afterGeneration, int timeoutMs, QImage &out, quint64 *gotGeneration,
                           const std::atomic<bool> *cancel);

Q_SIGNALS:
    void frameReady(const QImage &image);
    void analysisReady(const FrameColorAnalysis &analysis);
    void previewError(const QString &message);

protected:
    void run() override;

private:
    mutable QMutex m_mutex;
    QWaitCondition m_wait;
    QImage m_latest;
    quint64 m_generation = 0;
    std::atomic<bool> m_stop{false};
    QString m_deviceId;
    QString m_inputKey;
    QString m_modeKey;
};
