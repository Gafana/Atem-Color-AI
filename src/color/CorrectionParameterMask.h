#pragma once

#include "atem/CameraCorrection.h"

/** Which correction fields may be sent (works on top of any tuning mode). */
struct CorrectionParameterMask
{
    bool whiteBalance = true;
    bool tint = true;
    bool lift = true;
    bool gamma = true;
    bool gain = true;
    bool level = true;
    bool saturation = true;
    bool hue = false;
    bool iris = true;     ///< lens aperture (only sent when the value changes)
    bool offset = true;   ///< 8.3 offset (only changed by presets / manual edits)
    bool contrast = true; ///< 8.4 contrast + 8.5 luma mix (presets / manual edits)

    static CorrectionParameterMask allEnabled();
    static CorrectionParameterMask fromSettingsBits(unsigned bits);
    unsigned toSettingsBits() const;
};

/** Keep only enabled fields from @p proposed; other fields copy @p baseline (camera state). */
CameraCorrectionBundle mergeBundleWithMask(const CameraCorrectionBundle &proposed,
                                           const CameraCorrectionBundle &baseline,
                                           const CorrectionParameterMask &mask);

QString describeMaskRu(const CorrectionParameterMask &mask);