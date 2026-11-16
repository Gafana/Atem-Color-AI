#pragma once

#include "ScopeTypes.h"

class ScopeProcessor
{
public:
    static ScopeFrameOutput render(ScopeKind kind, const ScopeFrameInput &input);
    static QVector<ScopeFrameOutput> renderMany(const QVector<ScopeKind> &kinds, const ScopeFrameInput &input);
};