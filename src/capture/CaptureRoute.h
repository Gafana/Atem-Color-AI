#pragma once

#include <QString>

enum class CaptureRouteKind
{
    Invalid,
    Aux,
    Multiview,
    Program
};

inline CaptureRouteKind captureRouteKind(const QString &route)
{
    if (route == QLatin1String("aux"))
        return CaptureRouteKind::Aux;
    if (route == QLatin1String("multiview"))
        return CaptureRouteKind::Multiview;
    if (route == QLatin1String("pgm"))
        return CaptureRouteKind::Program;
    return CaptureRouteKind::Invalid;
}

inline QString captureRouteId(CaptureRouteKind kind)
{
    switch (kind) {
    case CaptureRouteKind::Aux:
        return QStringLiteral("aux");
    case CaptureRouteKind::Multiview:
        return QStringLiteral("multiview");
    case CaptureRouteKind::Program:
        return QStringLiteral("pgm");
    case CaptureRouteKind::Invalid:
        break;
    }
    return {};
}

/** Program bus may change only in PGM mode and only after the user confirms. */
inline bool maySwitchProgram(CaptureRouteKind kind, bool userConfirmed)
{
    return kind == CaptureRouteKind::Program && userConfirmed;
}

/** AUX routing is used only when that mode is selected. */
inline bool maySwitchAux(CaptureRouteKind kind)
{
    return kind == CaptureRouteKind::Aux;
}
