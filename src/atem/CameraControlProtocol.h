#pragma once

#include <cstdint>

/** Blackmagic SDI Camera Control Protocol (category / parameter ids). */
namespace BmdCameraProtocol {

constexpr uint8_t kCategoryLens = 0;
constexpr uint8_t kCategoryVideo = 1;
constexpr uint8_t kCategoryColorCorrection = 8;

constexpr uint8_t kLensApertureFStop = 2;

constexpr uint8_t kVideoManualWhiteBalance = 2;
constexpr uint8_t kVideoGainDb = 13;

constexpr uint8_t kCcLift = 0;
constexpr uint8_t kCcGamma = 1;
constexpr uint8_t kCcGain = 2;
constexpr uint8_t kCcOffset = 3;
constexpr uint8_t kCcContrast = 4;
constexpr uint8_t kCcLumaMix = 5;
constexpr uint8_t kCcColorAdjust = 6;

constexpr int kWbTintMin = -50;
constexpr int kWbTintMax = 50;
constexpr int kWbKelvinMin = 2500;
constexpr int kWbKelvinMax = 10000;

constexpr float kLiftMin = -2.0f;
constexpr float kLiftMax = 2.0f;
constexpr float kGammaMin = -4.0f;
constexpr float kGammaMax = 4.0f;
constexpr float kGainMin = 0.0f;
constexpr float kGainMax = 16.0f;
constexpr float kSatMin = 0.0f;
constexpr float kSatMax = 2.0f;
constexpr int kGainDbMin = -128;
constexpr int kGainDbMax = 127;

} // namespace BmdCameraProtocol