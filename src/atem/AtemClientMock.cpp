#include "AtemClient.h"
#include "AtemCorrectionApply.h"
#include "CameraControlProtocol.h"

#include <QHash>
#include <QLoggingCategory>
#include <QThread>

Q_LOGGING_CATEGORY(lcAtemMock, "atem.mock")

using namespace BmdCameraProtocol;

struct MockCameraState
{
    CameraCorrectionBundle bundle;
};

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

    QString readCorrectionBundle(qint64 destinationId, CameraCorrectionBundle &out) override
    {
        out = stateFor(destinationId).bundle;
        return {};
    }

    QString applyCorrectionBundle(qint64 destinationId,
                                  const CameraCorrectionBundle &bundle,
                                  CameraCorrectionBundle *previousOut) override
    {
        if (!m_connected)
            return QStringLiteral("Not connected");
        return applyCorrectionBundleToClient(*this, destinationId, bundle, previousOut);
    }

    QString rollbackCorrection(qint64 destinationId, const CameraCorrectionBundle &previous) override
    {
        return applyCorrectionBundle(destinationId, previous, nullptr);
    }

    QString setCameraFloats(qint64 destinationId,
                            uint8_t category,
                            uint8_t parameter,
                            const QVector<float> &values) override
    {
        qCInfo(lcAtemMock).noquote()
            << QStringLiteral("SetFloats dest=%1 cat=%2 param=%3 values=%4")
                   .arg(destinationId)
                   .arg(category)
                   .arg(parameter)
                   .arg([&values]() {
                       QStringList parts;
                       for (float v : values)
                           parts << QString::number(v, 'f', 4);
                       return parts.join(QLatin1Char(','));
                   }());

        MockCameraState &st = stateFor(destinationId);
        if (category == kCategoryColorCorrection) {
            if (parameter == kCcLift && values.size() >= 4) {
                st.bundle.liftR = values[0];
                st.bundle.liftG = values[1];
                st.bundle.liftB = values[2];
                st.bundle.liftY = values[3];
            } else if (parameter == kCcGamma && values.size() >= 4) {
                st.bundle.gammaR = values[0];
                st.bundle.gammaG = values[1];
                st.bundle.gammaB = values[2];
                st.bundle.gammaY = values[3];
            } else if (parameter == kCcGain && values.size() >= 4) {
                st.bundle.gainR = values[0];
                st.bundle.gainG = values[1];
                st.bundle.gainB = values[2];
                st.bundle.gainY = values[3];
            } else if (parameter == kCcColorAdjust && values.size() >= 2) {
                st.bundle.hue = values[0];
                st.bundle.saturation = values[1];
            }
        } else if (category == kCategoryLens && parameter == kLensApertureFStop && !values.isEmpty()) {
            st.bundle.irisFStopAv = values[0];
        }
        st.bundle.clamp();
        return {};
    }

    QString setCameraInt16Pair(qint64 destinationId,
                               uint8_t category,
                               uint8_t parameter,
                               int value0,
                               int value1) override
    {
        qCInfo(lcAtemMock).noquote()
            << QStringLiteral("SetInt16s dest=%1 cat=%2 param=%3 v0=%4 v1=%5")
                   .arg(destinationId)
                   .arg(category)
                   .arg(parameter)
                   .arg(value0)
                   .arg(value1);
        if (category == kCategoryVideo && parameter == kVideoManualWhiteBalance) {
            MockCameraState &st = stateFor(destinationId);
            st.bundle.whiteBalanceKelvin = value0;
            st.bundle.whiteBalanceTint = value1;
            st.bundle.clamp();
        }
        return {};
    }

    QString setCameraGainDb(qint64 destinationId, int gainDb) override
    {
        qCInfo(lcAtemMock) << "SetInt8 gain dB dest=" << destinationId << "gain=" << gainDb;
        MockCameraState &st = stateFor(destinationId);
        st.bundle.gainDb = gainDb;
        st.bundle.clamp();
        return {};
    }

private:
    MockCameraState &stateFor(qint64 id)
    {
        return m_states[id];
    }

    bool m_connected = false;
    QString m_connectedAddress;
    QHash<qint64, MockCameraState> m_states;
};

std::unique_ptr<AtemClient> createAtemClientMock()
{
    return std::make_unique<AtemClientMock>();
}