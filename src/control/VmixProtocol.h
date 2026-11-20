#pragma once

#include "atem/CameraCorrection.h"

#include <QByteArray>
#include <QPair>
#include <QString>
#include <QVector>

struct CorrectionParameterMask;

/**
 * vMix control: pure helpers (no I/O) used by VmixClient and unit tests.
 *
 * Documented API only (vMix 28 user guide):
 *  - HTTP Web API  https://www.vmix.com/help28/DeveloperAPI.html  (GET /api/?Function=...&Input=...&Value=...,
 *    GET /api with no parameters returns the XML state)
 *  - Shortcut Function Reference  https://www.vmix.com/help28/ShortcutFunctionReference.html :
 *    SetCCLiftR/G/B/Y, SetCCGammaR/G/B/Y (-1..1, 0 = original), SetCCGainR/G/B/Y (0..2, 1 = original),
 *    SetCCSaturation (-1..1, 0 = original, -1 = greyscale), SetCCHue (-1..1), ColourCorrectionAuto,
 *    ColourCorrectionReset, SetOutput2/3/4, SetOutputExternal2, SetOutputFullscreen/Fullscreen2
 *    (Value = Output | Preview | Input, Input = n), PreviewInput, ActiveInput.
 *
 * Mapping to CameraCorrectionBundle (same units as the Blackmagic CC the engine works with):
 *   lift/gamma/hue 1:1, gain 1:1, saturation: vMix = bundle - 1. vMix has no white balance K/tint, sensor dB,
 *   iris, shutter, offset or contrast: those stay untouched.
 */
namespace VmixProtocol {

constexpr int kDefaultHttpPort = 8088;
constexpr qint64 kSourceProgram = 10010; ///< same id as the ATEM programme source: «Output» in vMix
constexpr qint64 kSourcePreview = 10011; ///< «Preview»

struct Input
{
    int number = 0;
    QString key;   ///< GUID (stable id of the input)
    QString type;
    QString title;
    QString state;
    bool hasCc = false;          ///< the XML carried colour correction values for this input
    CameraCorrectionBundle cc;   ///< valid when hasCc
};

struct State
{
    bool ok = false;
    QString error;
    QString version;
    QString edition;
    QVector<Input> inputs;
    int preview = 0;
    int active = 0;
};

/** Parse the XML returned by GET /api. Colour correction values are read when present (any child element or
 *  attribute named like liftR / gammaG / gainB / gainY / saturation / hue); otherwise hasCc = false. */
State parseState(const QByteArray &xml);

/** Query string for one function call, e.g. "Function=SetCCGainR&Input=2&Value=1.05". Empty args are omitted. */
QString functionQuery(const QString &function, const QString &input = QString(), const QString &value = QString());

struct Command
{
    QString function;
    QString value; ///< already formatted
};

/** vMix values of a bundle (clamped to the documented ranges). */
double toVmixLift(float v);
double toVmixGamma(float v);
double toVmixGain(float v);
double toVmixSaturation(float bundleSat);
double toVmixHue(float v);
float fromVmixSaturation(double v);

/** CC functions needed to go from @p from to @p to (only changed and mask-enabled values; @p all = every
 *  mask-enabled value, used when the current vMix values are unknown, e.g. after ColourCorrectionAuto). */
QVector<Command> ccCommands(const CameraCorrectionBundle &from, const CameraCorrectionBundle &to,
                            const CorrectionParameterMask *mask = nullptr, bool all = false);

/** Values that vMix cannot change kept from @p current (WB, dB, iris, offset, contrast). */
CameraCorrectionBundle keepUnsupported(const CameraCorrectionBundle &wanted, const CameraCorrectionBundle &current);

/** vMix outputs a capture card / virtual camera can be fed from (used like ATEM AUX outputs). */
struct Output
{
    qint64 id = 0;
    QString name;
    QString function;
};
QVector<Output> outputs();
/** Routing call for an output: Value=Output (programme), Value=Preview or Value=Input&Input=n. */
QString routeQuery(qint64 outputId, qint64 sourceId, QString *error = nullptr);

/** "host", "host:port" or "http://host:port" -> host + port (default 8088, host default localhost). */
QPair<QString, int> parseAddress(const QString &address);

} // namespace VmixProtocol
