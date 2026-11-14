#include "AtemClient.h"

#include <QThread>

class AtemClientMock final : public AtemClient
{
public:
    QString backendName() const override { return QStringLiteral("Mock (ATEM SDK not linked)"); }

    QString connectTo(const QString &address) override
    {
        if (address.isEmpty())
            return QStringLiteral("Empty address");

        m_connectedAddress = address;
        m_connected = true;
        // Simulate light network delay
        QThread::msleep(50);
        return {};
    }

    void disconnect() override
    {
        m_connected = false;
        m_connectedAddress.clear();
    }

    bool isConnected() const override { return m_connected; }

    QVector<AtemInputInfo> listInputs() override
    {
        if (!m_connected)
            return {};

        return {
            {1, QStringLiteral("Cam1"), QStringLiteral("Studio Camera 1"), QStringLiteral("SDI"), true},
            {2, QStringLiteral("Cam2"), QStringLiteral("Studio Camera 2"), QStringLiteral("SDI"), true},
            {3, QStringLiteral("Cam3"), QStringLiteral("Studio Camera 3"), QStringLiteral("HDMI"), true},
            {4, QStringLiteral("PC"), QStringLiteral("Presenter PC"), QStringLiteral("HDMI"), false},
            {5, QStringLiteral("BG"), QStringLiteral("Color Background"), QStringLiteral("Internal"), false},
        };
    }

private:
    bool m_connected = false;
    QString m_connectedAddress;
};

std::unique_ptr<AtemClient> createAtemClientMock()
{
    return std::make_unique<AtemClientMock>();
}