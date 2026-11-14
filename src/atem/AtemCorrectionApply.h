#pragma once

#include "AtemClient.h"

QString applyCorrectionBundleToClient(AtemClient &client,
                                      qint64 destinationId,
                                      const CameraCorrectionBundle &bundle,
                                      CameraCorrectionBundle *previousOut = nullptr);