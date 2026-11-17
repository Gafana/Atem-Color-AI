#include "CorrectionParameterMask.h"

CorrectionParameterMask CorrectionParameterMask::allEnabled()
{
    return {};
}

CorrectionParameterMask CorrectionParameterMask::fromSettingsBits(unsigned bits)
{
    CorrectionParameterMask mask;
    mask.whiteBalance = (bits & (1u << 0)) != 0;
    mask.tint = (bits & (1u << 1)) != 0;
    mask.lift = (bits & (1u << 2)) != 0;
    mask.gamma = (bits & (1u << 3)) != 0;
    mask.gain = (bits & (1u << 4)) != 0;
    mask.level = (bits & (1u << 5)) != 0;
    mask.saturation = (bits & (1u << 6)) != 0;
    mask.hue = (bits & (1u << 7)) != 0;
    mask.iris = (bits & (1u << 8)) != 0 || bits < (1u << 8); // older settings: default on
    mask.offset = (bits & (1u << 9)) != 0 || bits < (1u << 8);
    mask.contrast = (bits & (1u << 10)) != 0 || bits < (1u << 8);
    if (bits == 0)
        return allEnabled();
    return mask;
}

unsigned CorrectionParameterMask::toSettingsBits() const
{
    unsigned bits = 0;
    if (whiteBalance)
        bits |= 1u << 0;
    if (tint)
        bits |= 1u << 1;
    if (lift)
        bits |= 1u << 2;
    if (gamma)
        bits |= 1u << 3;
    if (gain)
        bits |= 1u << 4;
    if (level)
        bits |= 1u << 5;
    if (saturation)
        bits |= 1u << 6;
    if (hue)
        bits |= 1u << 7;
    if (iris)
        bits |= 1u << 8;
    if (offset)
        bits |= 1u << 9;
    if (contrast)
        bits |= 1u << 10;
    bits |= 1u << 11; // marks the extended format
    return bits;
}

CameraCorrectionBundle mergeBundleWithMask(const CameraCorrectionBundle &proposed,
                                           const CameraCorrectionBundle &baseline,
                                           const CorrectionParameterMask &mask)
{
    CameraCorrectionBundle out = baseline;
    if (mask.lift) {
        out.liftR = proposed.liftR;
        out.liftG = proposed.liftG;
        out.liftB = proposed.liftB;
        out.liftY = proposed.liftY;
    }
    if (mask.gamma) {
        out.gammaR = proposed.gammaR;
        out.gammaG = proposed.gammaG;
        out.gammaB = proposed.gammaB;
        out.gammaY = proposed.gammaY;
    }
    if (mask.gain) {
        out.gainR = proposed.gainR;
        out.gainG = proposed.gainG;
        out.gainB = proposed.gainB;
        out.gainY = proposed.gainY;
    }
    if (mask.whiteBalance)
        out.whiteBalanceKelvin = proposed.whiteBalanceKelvin;
    if (mask.tint)
        out.whiteBalanceTint = proposed.whiteBalanceTint;
    if (mask.level)
        out.gainDb = proposed.gainDb;
    if (mask.saturation)
        out.saturation = proposed.saturation;
    if (mask.hue)
        out.hue = proposed.hue;
    if (mask.iris)
        out.irisFStopAv = proposed.irisFStopAv;
    if (mask.offset) {
        out.offsetR = proposed.offsetR;
        out.offsetG = proposed.offsetG;
        out.offsetB = proposed.offsetB;
        out.offsetY = proposed.offsetY;
    }
    if (mask.contrast) {
        out.contrastPivot = proposed.contrastPivot;
        out.contrastAdj = proposed.contrastAdj;
        out.lumaMix = proposed.lumaMix;
    }
    if (mask.level == false && mask.gain)
        out.gainY = proposed.gainY;
    out.clamp();
    return out;
}

QString describeMaskRu(const CorrectionParameterMask &mask)
{
    QStringList parts;
    if (mask.whiteBalance)
        parts << QStringLiteral("баланс белого");
    if (mask.tint)
        parts << QStringLiteral("тинт");
    if (mask.lift)
        parts << QStringLiteral("лифт");
    if (mask.gamma)
        parts << QStringLiteral("гамма");
    if (mask.gain)
        parts << QStringLiteral("гейн");
    if (mask.level)
        parts << QStringLiteral("уровень");
    if (mask.saturation)
        parts << QStringLiteral("насыщенность");
    if (mask.hue)
        parts << QStringLiteral("оттенок");
    if (parts.isEmpty())
        return QStringLiteral("ничего");
    return parts.join(QStringLiteral(", "));
}