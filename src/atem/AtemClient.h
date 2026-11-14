#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include <memory>

struct AtemInputInfo
{
    qint64 id = 0;
    QString shortName;
    QString longName;
    QString portType; // HDMI, SDI, etc.
    bool cameraControlSupported = false;
};

class AtemClient
{
public:
    virtual ~AtemClient() = default;

    /** @return Human-readable backend name (SDK / Mock). */
    virtual QString backendName() const = 0;

    /**
     * Connect to switcher at @p address (IPv4/hostname).
     * @return Empty string on success, otherwise error description.
     */
    virtual QString connectTo(const QString &address) = 0;

    virtual void disconnect() = 0;

    virtual bool isConnected() const = 0;

    /** Enumerate switcher inputs (cameras / sources). */
    virtual QVector<AtemInputInfo> listInputs() = 0;
};

std::unique_ptr<AtemClient> createAtemClient();