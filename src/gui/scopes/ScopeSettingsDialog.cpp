#include "ScopeSettingsDialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QSpinBox>
#include <QVBoxLayout>

ScopeSettingsDialog::ScopeSettingsDialog(ScopeKind kind, const ScopeDisplaySettings &settings,
                                         QWidget *parent)
    : QDialog(parent)
    , m_settings(settings)
    , m_kind(kind)
{
    setWindowTitle(QStringLiteral("Настройки прибора"));
    auto *form = new QFormLayout();
    auto *traceSpin = new QDoubleSpinBox(this);
    traceSpin->setRange(0.2, 2.0);
    traceSpin->setSingleStep(0.05);
    traceSpin->setValue(settings.traceBrightness);
    form->addRow(QStringLiteral("Яркость следа"), traceSpin);

    auto *refreshSpin = new QSpinBox(this);
    refreshSpin->setRange(0, 60);
    refreshSpin->setSpecialValueText(QStringLiteral("По кадру (0)"));
    refreshSpin->setValue(settings.refreshFps);
    form->addRow(QStringLiteral("Обновление (кадр/с, 0=кадр)"), refreshSpin);

    QCheckBox *ire100 = nullptr;
    if (kind == ScopeKind::WaveformLuma || kind == ScopeKind::RgbParade || kind == ScopeKind::YcbcrParade
        || kind == ScopeKind::RgbOverlayWaveform) {
        ire100 = new QCheckBox(QStringLiteral("Шкала 0–100 IRE"), this);
        ire100->setChecked(settings.ireScale100);
        form->addRow(ire100);
    }

    QComboBox *vsZoom = nullptr;
    QCheckBox *vsZones = nullptr;
    if (kind == ScopeKind::Vectorscope) {
        vsZoom = new QComboBox(this);
        vsZoom->addItem(QStringLiteral("×1"), 1);
        vsZoom->addItem(QStringLiteral("×2"), 2);
        vsZoom->addItem(QStringLiteral("×5"), 5);
        const int zi = vsZoom->findData(settings.vectorscopeZoom);
        vsZoom->setCurrentIndex(zi >= 0 ? zi : 0);
        form->addRow(QStringLiteral("Зум"), vsZoom);
        vsZones = new QCheckBox(QStringLiteral("Режим по зонам яркости (L/M/H)"), this);
        vsZones->setChecked(settings.vectorscopeLumaZones);
        form->addRow(vsZones);
    }

    QCheckBox *monoTrace = nullptr;
    QDoubleSpinBox *tolLow = nullptr;
    QDoubleSpinBox *tolHigh = nullptr;
    if (kind == ScopeKind::WaveformLuma || kind == ScopeKind::RgbParade || kind == ScopeKind::YcbcrParade
        || kind == ScopeKind::RgbOverlayWaveform) {
        monoTrace = new QCheckBox(QStringLiteral("Монохромный след"), this);
        monoTrace->setChecked(settings.waveformMonochrome);
        form->addRow(monoTrace);
        tolLow = new QDoubleSpinBox(this);
        tolLow->setRange(-20, 100);
        tolLow->setValue(settings.ireToleranceLow);
        tolHigh = new QDoubleSpinBox(this);
        tolHigh->setRange(0, 120);
        tolHigh->setValue(settings.ireToleranceHigh);
        form->addRow(QStringLiteral("Допуск нижний IRE"), tolLow);
        form->addRow(QStringLiteral("Допуск верхний IRE"), tolHigh);
    }

    QCheckBox *ghostSnap = new QCheckBox(QStringLiteral("Полупрозрачный снимок для сравнения"), this);
    ghostSnap->setChecked(settings.snapshotOverlay);
    form->addRow(ghostSnap);

    QDoubleSpinBox *zebraIre = nullptr;
    if (kind == ScopeKind::Zebra) {
        zebraIre = new QDoubleSpinBox(this);
        zebraIre->setRange(50, 109);
        zebraIre->setValue(settings.zebraThresholdIre);
        form->addRow(QStringLiteral("Порог IRE"), zebraIre);
    }

    auto *box = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(box, &QDialogButtonBox::accepted, this,
            [this, traceSpin, refreshSpin, ire100, vsZoom, vsZones, zebraIre, monoTrace, tolLow, tolHigh,
             ghostSnap]() {
                m_settings.traceBrightness = static_cast<float>(traceSpin->value());
                m_settings.refreshFps = refreshSpin->value();
                if (ire100)
                    m_settings.ireScale100 = ire100->isChecked();
                if (vsZoom)
                    m_settings.vectorscopeZoom = vsZoom->currentData().toInt();
                if (vsZones)
                    m_settings.vectorscopeLumaZones = vsZones->isChecked();
                if (zebraIre)
                    m_settings.zebraThresholdIre = static_cast<float>(zebraIre->value());
                if (monoTrace)
                    m_settings.waveformMonochrome = monoTrace->isChecked();
                if (tolLow)
                    m_settings.ireToleranceLow = static_cast<float>(tolLow->value());
                if (tolHigh)
                    m_settings.ireToleranceHigh = static_cast<float>(tolHigh->value());
                m_settings.snapshotOverlay = ghostSnap->isChecked();
                accept();
            });
    connect(box, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto *root = new QVBoxLayout(this);
    root->addLayout(form);
    root->addWidget(box);
}