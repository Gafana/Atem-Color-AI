#pragma once

#include "scopes/ScopeTypes.h"

#include <QDialog>

class ScopeSettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ScopeSettingsDialog(ScopeKind kind, const ScopeDisplaySettings &settings,
                                 QWidget *parent = nullptr);

    ScopeDisplaySettings result() const { return m_settings; }

private:
    ScopeDisplaySettings m_settings;
    ScopeKind m_kind = ScopeKind::None;
};