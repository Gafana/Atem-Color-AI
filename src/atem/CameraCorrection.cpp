#include "CameraCorrection.h"

#include <QJsonArray>
#include <QtMath>

using namespace BmdCameraProtocol;

static float clampf(float v, float lo, float hi)
{
    return qBound(lo, v, hi);
}

void CameraCorrectionBundle::clamp()
{
    clampBundle(*this);
}

void clampBundle(CameraCorrectionBundle &bundle)
{
    bundle.liftR = clampf(bundle.liftR, kLiftMin, kLiftMax);
    bundle.liftG = clampf(bundle.liftG, kLiftMin, kLiftMax);
    bundle.liftB = clampf(bundle.liftB, kLiftMin, kLiftMax);
    bundle.liftY = clampf(bundle.liftY, kLiftMin, kLiftMax);

    bundle.gammaR = clampf(bundle.gammaR, kGammaMin, kGammaMax);
    bundle.gammaG = clampf(bundle.gammaG, kGammaMin, kGammaMax);
    bundle.gammaB = clampf(bundle.gammaB, kGammaMin, kGammaMax);
    bundle.gammaY = clampf(bundle.gammaY, kGammaMin, kGammaMax);

    bundle.gainR = clampf(bundle.gainR, kGainMin, kGainMax);
    bundle.gainG = clampf(bundle.gainG, kGainMin, kGainMax);
    bundle.gainB = clampf(bundle.gainB, kGainMin, kGainMax);
    bundle.gainY = clampf(bundle.gainY, kGainMin, kGainMax);

    bundle.saturation = clampf(bundle.saturation, kSatMin, kSatMax);
    bundle.hue = clampf(bundle.hue, -1.0f, 1.0f);

    bundle.whiteBalanceKelvin = qBound(kWbKelvinMin, bundle.whiteBalanceKelvin, kWbKelvinMax);
    bundle.whiteBalanceTint = qBound(kWbTintMin, bundle.whiteBalanceTint, kWbTintMax);
    bundle.gainDb = qBound(kGainDbMin, bundle.gainDb, kGainDbMax);
}

CameraCorrectionBundle CameraCorrectionBundle::damped(float factor) const
{
    const float f = qBound(0.0f, factor, 1.0f);
    CameraCorrectionBundle out = *this;

    const auto lerp = [f](float target, float neutral) {
        return neutral + (target - neutral) * f;
    };

    out.liftR = lerp(liftR, 0.0f);
    out.liftG = lerp(liftG, 0.0f);
    out.liftB = lerp(liftB, 0.0f);
    out.liftY = lerp(liftY, 0.0f);

    out.gammaR = lerp(gammaR, 0.0f);
    out.gammaG = lerp(gammaG, 0.0f);
    out.gammaB = lerp(gammaB, 0.0f);
    out.gammaY = lerp(gammaY, 0.0f);

    out.gainR = lerp(gainR, 1.0f);
    out.gainG = lerp(gainG, 1.0f);
    out.gainB = lerp(gainB, 1.0f);
    out.gainY = lerp(gainY, 1.0f);

    out.saturation = lerp(saturation, 1.0f);
    out.hue = lerp(hue, 0.0f);

    out.gainDb = static_cast<int>(qRound(lerp(static_cast<float>(gainDb), 0.0f)));
    out.whiteBalanceKelvin =
        static_cast<int>(qRound(lerp(static_cast<float>(whiteBalanceKelvin), 5600.0f)));
    out.whiteBalanceTint =
        static_cast<int>(qRound(lerp(static_cast<float>(whiteBalanceTint), 0.0f)));

    out.clamp();
    return out;
}

bool CameraCorrectionBundle::operator==(const CameraCorrectionBundle &other) const
{
    return liftR == other.liftR && liftG == other.liftG && liftB == other.liftB
           && liftY == other.liftY && gammaR == other.gammaR && gammaG == other.gammaG
           && gammaB == other.gammaB && gammaY == other.gammaY && gainR == other.gainR
           && gainG == other.gainG && gainB == other.gainB && gainY == other.gainY
           && saturation == other.saturation && hue == other.hue
           && whiteBalanceKelvin == other.whiteBalanceKelvin
           && whiteBalanceTint == other.whiteBalanceTint && gainDb == other.gainDb
           && irisFStopAv == other.irisFStopAv;
}

QJsonObject CameraCorrectionBundle::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("lift"), QJsonArray{liftR, liftG, liftB, liftY});
    o.insert(QStringLiteral("gamma"), QJsonArray{gammaR, gammaG, gammaB, gammaY});
    o.insert(QStringLiteral("gain"), QJsonArray{gainR, gainG, gainB, gainY});
    o.insert(QStringLiteral("saturation"), saturation);
    o.insert(QStringLiteral("hue"), hue);
    o.insert(QStringLiteral("whiteBalanceKelvin"), whiteBalanceKelvin);
    o.insert(QStringLiteral("whiteBalanceTint"), whiteBalanceTint);
    o.insert(QStringLiteral("gainDb"), gainDb);
    if (irisFStopAv >= 0.0f)
        o.insert(QStringLiteral("irisFStopAv"), irisFStopAv);
    return o;
}

CameraCorrectionBundle CameraCorrectionBundle::fromJson(const QJsonObject &obj)
{
    CameraCorrectionBundle b;
    const auto read4 = [&](const char *key, float &a, float &g, float &bl, float &y) {
        const QJsonArray arr = obj.value(QString::fromLatin1(key)).toArray();
        if (arr.size() >= 4) {
            a = static_cast<float>(arr.at(0).toDouble());
            g = static_cast<float>(arr.at(1).toDouble());
            bl = static_cast<float>(arr.at(2).toDouble());
            y = static_cast<float>(arr.at(3).toDouble());
        }
    };
    read4("lift", b.liftR, b.liftG, b.liftB, b.liftY);
    read4("gamma", b.gammaR, b.gammaG, b.gammaB, b.gammaY);
    read4("gain", b.gainR, b.gainG, b.gainB, b.gainY);
    b.saturation = static_cast<float>(obj.value(QStringLiteral("saturation")).toDouble(1.0));
    b.hue = static_cast<float>(obj.value(QStringLiteral("hue")).toDouble(0.0));
    b.whiteBalanceKelvin = obj.value(QStringLiteral("whiteBalanceKelvin")).toInt(5600);
    b.whiteBalanceTint = obj.value(QStringLiteral("whiteBalanceTint")).toInt(0);
    b.gainDb = obj.value(QStringLiteral("gainDb")).toInt(0);
    if (obj.contains(QStringLiteral("irisFStopAv")))
        b.irisFStopAv = static_cast<float>(obj.value(QStringLiteral("irisFStopAv")).toDouble());
    b.clamp();
    return b;
}