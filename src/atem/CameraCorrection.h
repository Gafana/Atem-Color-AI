#pragma once

#include "CameraControlProtocol.h"

#include <QJsonObject>

/** Camera correction targets mapped to Blackmagic Camera Control groups. */
struct CameraCorrectionBundle
{
    float liftR = 0.0f;
    float liftG = 0.0f;
    float liftB = 0.0f;
    float liftY = 0.0f;

    float gammaR = 0.0f;
    float gammaG = 0.0f;
    float gammaB = 0.0f;
    float gammaY = 0.0f;

    float gainR = 1.0f;
    float gainG = 1.0f;
    float gainB = 1.0f;
    float gainY = 1.0f;

    float saturation = 1.0f;
    float hue = 0.0f;

    int whiteBalanceKelvin = 5600;
    int whiteBalanceTint = 0;
    int gainDb = 0;
    float irisFStopAv = -1.0f; /**< Negative = not used. */

    void clamp();
    CameraCorrectionBundle damped(float factor) const;
    bool operator==(const CameraCorrectionBundle &other) const;
    QJsonObject toJson() const;
    static CameraCorrectionBundle fromJson(const QJsonObject &obj);
};

void clampBundle(CameraCorrectionBundle &bundle);