#pragma once

#include "AtemClient.h"
#include "color/CorrectionParameterMask.h"

QString applyCorrectionBundleToClient(AtemClient &client,
                                      qint64 destinationId,
                                      const CameraCorrectionBundle &bundle,
                                      CameraCorrectionBundle *previousOut = nullptr,
                                      const CorrectionParameterMask *mask = nullptr,
                                      const QString &tuningMode = QString());