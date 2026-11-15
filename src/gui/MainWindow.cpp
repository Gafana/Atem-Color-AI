#include "MainWindow.h"
#include "AtemPathsSetupDialog.h"
#include "HistogramWidget.h"
#include "VectorscopeWidget.h"

#include "color/ColorAnalysis.h"
#include "platform/AtemAppSettings.h"
#include "platform/AtemPathValidation.h"
#include "platform/AtemSetup.h"

#ifdef HAVE_GSTREAMER
#include "capture/GstFrameCapture.h"
#endif

#include <QComboBox>
#include <QDialog>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QSplitter>
#include <QTableWidget>
#include <QVBoxLayout>

MainWindow::MainWindow(std::unique_ptr<AtemClient> client, QWidget *parent)
    : QMainWindow(parent)
    , m_client(std::move(client))
{
    setWindowTitle(QStringLiteral("Atem Color AI"));
    resize(1100, 720);

    auto *settingsMenu = menuBar()->addMenu(QStringLiteral("Настройки"));
    settingsMenu->addAction(QStringLiteral("Пути ATEM…"), this, &MainWindow::openSettingsDialog);

    if (m_client && m_client->isConnected()) {
        m_inputs = m_client->listInputs();
        for (const AtemInputInfo &in : m_inputs) {
            if (!in.cameraControlSupported)
                continue;
            CameraRow row;
            row.atemId = in.id;
            row.label = in.shortName;
            row.sourceUri = QStringLiteral("videotestsrc");
            m_cameras.push_back(row);
        }
    }

    auto *central = new QWidget(this);
    auto *layout = new QVBoxLayout(central);

    m_configBanner = new QLabel(central);
    m_configBanner->setWordWrap(true);
    m_configBanner->setStyleSheet(
        QStringLiteral("QLabel { background: #fff8e6; border: 1px solid #e6c200; padding: 8px; }"));
    m_configBanner->setVisible(false);
    layout->addWidget(m_configBanner);

    auto *topRow = new QHBoxLayout();
    topRow->addWidget(new QLabel(QStringLiteral("Эталон:")));
    m_referenceCombo = new QComboBox(central);
    for (const CameraRow &c : m_cameras)
        m_referenceCombo->addItem(c.label, QVariant::fromValue(c.atemId));
    connect(m_referenceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onReferenceChanged);
    topRow->addWidget(m_referenceCombo, 1);
    layout->addLayout(topRow);

    m_sourceTable = new QTableWidget(central);
    m_sourceTable->setColumnCount(3);
    m_sourceTable->setHorizontalHeaderLabels(
        {QStringLiteral("Камера"), QStringLiteral("Источник (URI/PNG)"), QStringLiteral("ATEM id")});
    m_sourceTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_sourceTable->setRowCount(m_cameras.size());
    for (int i = 0; i < m_cameras.size(); ++i) {
        m_sourceTable->setItem(i, 0, new QTableWidgetItem(m_cameras[i].label));
        auto *uri = new QTableWidgetItem(m_cameras[i].sourceUri);
        m_sourceTable->setItem(i, 1, uri);
        m_sourceTable->setItem(i, 2, new QTableWidgetItem(QString::number(m_cameras[i].atemId)));
    }
    layout->addWidget(m_sourceTable);

    m_corrTable = new QTableWidget(central);
    m_corrTable->setColumnCount(6);
    m_corrTable->setHorizontalHeaderLabels({QStringLiteral("Камера"), QStringLiteral("ΔY"),
                                            QStringLiteral("L1 RGB"), QStringLiteral("Gain Y"),
                                            QStringLiteral("WB K"), QStringLiteral("Gain dB")});
    m_corrTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    layout->addWidget(m_corrTable);

    auto *split = new QSplitter(Qt::Horizontal, central);
    m_hist = new HistogramWidget(split);
    m_vectorscope = new VectorscopeWidget(split);
    split->addWidget(m_hist);
    split->addWidget(m_vectorscope);
    layout->addWidget(split, 1);

    auto *buttons = new QHBoxLayout();
    auto *btnCapture = new QPushButton(QStringLiteral("Захватить / Анализировать"), central);
    m_btnApply = new QPushButton(QStringLiteral("Применить поправки"), central);
    auto *btnRollback = new QPushButton(QStringLiteral("Откат"), central);
    connect(btnCapture, &QPushButton::clicked, this, &MainWindow::onCaptureAnalyze);
    connect(m_btnApply, &QPushButton::clicked, this, &MainWindow::onApply);
    connect(btnRollback, &QPushButton::clicked, this, [this]() {
        if (!m_client)
            return;
        for (CameraRow &c : m_cameras) {
            if (!c.hasRollback)
                continue;
            m_client->rollbackCorrection(c.atemId, c.rollback);
            c.hasRollback = false;
        }
        QMessageBox::information(this, QString(), QStringLiteral("Откат выполнен"));
    });
    buttons->addWidget(btnCapture);
    buttons->addWidget(m_btnApply);
    buttons->addWidget(btnRollback);
    layout->addLayout(buttons);

    setCentralWidget(central);
    onReferenceChanged(0);
    rebuildAtemAvailabilityUi();
}

void MainWindow::rebuildAtemAvailabilityUi()
{
    const QString banner = AtemSetup::configurationBannerText();
    if (m_configBanner) {
        m_configBanner->setText(banner);
        m_configBanner->setVisible(!banner.isEmpty());
    }

    const bool atemOk = AtemSetup::isLiveAtemControlAvailable();
    if (m_btnApply) {
        m_btnApply->setEnabled(atemOk && m_client && m_client->isConnected());
        if (!atemOk) {
            m_btnApply->setToolTip(QStringLiteral(
                "Укажите корректный путь к ATEM Software Control в «Настройки → Пути ATEM…»"));
        } else if (!m_client || !m_client->isConnected()) {
            m_btnApply->setToolTip(QStringLiteral("Нет подключения к ATEM"));
        } else {
            m_btnApply->setToolTip(QString());
        }
    }
}

void MainWindow::openSettingsDialog()
{
    AtemPathsSetupDialog dlg(this);
    dlg.setSdkPath(AtemAppSettings::sdkPath());
    dlg.setSoftwareControlPath(AtemAppSettings::softwareControlPath());
    if (dlg.exec() != QDialog::Accepted)
        return;

    const QString control = AtemPathValidation::normalizeSoftwareControlPath(
        dlg.softwareControlPath());
    AtemAppSettings::save(dlg.sdkPath(), control, true);
    rebuildAtemAvailabilityUi();
}

bool MainWindow::loadFrameForCamera(int index, QImage &image)
{
    if (index < 0 || index >= m_cameras.size())
        return false;
    CameraRow &row = m_cameras[index];
    if (m_sourceTable->item(index, 1))
        row.sourceUri = m_sourceTable->item(index, 1)->text().trimmed();

    if (row.sourceUri.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive)
        || row.sourceUri.endsWith(QStringLiteral(".jpg"), Qt::CaseInsensitive)) {
        return ColorAnalysis::loadImage(row.sourceUri, image).isEmpty();
    }
#ifdef HAVE_GSTREAMER
    return GstFrameCapture::captureFrame(row.sourceUri, image).isEmpty();
#else
    Q_UNUSED(image);
    return false;
#endif
}

void MainWindow::onCaptureAnalyze()
{
    if (m_cameras.isEmpty()) {
        QMessageBox::warning(this, QString(), QStringLiteral("Нет камер с Camera Control"));
        return;
    }

    ColorMatchOptions options;
    options.damping = 0.35f;

    QVector<QPair<QString, FrameColorAnalysis>> others;
    FrameColorAnalysis referenceAnalysis;

    for (int i = 0; i < m_cameras.size(); ++i) {
        QImage frame;
        if (!loadFrameForCamera(i, frame)) {
            QMessageBox::warning(this, QString(),
                                 QStringLiteral("Не удалось получить кадр для %1").arg(m_cameras[i].label));
            return;
        }
        m_cameras[i].analysis = ColorAnalysis::analyze(frame);
        if (i == m_referenceIndex)
            referenceAnalysis = m_cameras[i].analysis;
        else
            others.push_back({m_cameras[i].label, m_cameras[i].analysis});
    }

    const auto matches = ColorMatching::matchAll(referenceAnalysis, others, options);
    int matchIdx = 0;
    for (int i = 0; i < m_cameras.size(); ++i) {
        if (i == m_referenceIndex)
            continue;
        if (matchIdx < matches.size())
            m_cameras[i].match = matches[matchIdx++];
    }
    refreshCorrectionsTable();
    updatePlots();
}

void MainWindow::onApply()
{
    if (!AtemSetup::isLiveAtemControlAvailable()) {
        QMessageBox::warning(this, QString(),
                             QStringLiteral("Настройте путь к ATEM Software Control"));
        return;
    }
    if (!m_client || !m_client->isConnected()) {
        QMessageBox::warning(this, QString(), QStringLiteral("Нет подключения к ATEM"));
        return;
    }

    const auto reply = QMessageBox::question(
        this, QStringLiteral("Подтверждение"),
        QStringLiteral("Применить демпфированные поправки ко всем камерам (кроме эталона)?"));
    if (reply != QMessageBox::Yes)
        return;

    QString errors;
    for (int i = 0; i < m_cameras.size(); ++i) {
        if (i == m_referenceIndex)
            continue;
        CameraCorrectionBundle prev;
        const QString err = m_client->applyCorrectionBundle(m_cameras[i].atemId,
                                                            m_cameras[i].match.damped, &prev);
        if (!err.isEmpty())
            errors += QStringLiteral("%1: %2\n").arg(m_cameras[i].label, err);
        else {
            m_cameras[i].rollback = prev;
            m_cameras[i].hasRollback = true;
        }
    }

    if (!errors.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Частичные ошибки"), errors);
    else
        QMessageBox::information(this, QString(), QStringLiteral("Поправки отправлены"));
}

void MainWindow::onReferenceChanged(int index)
{
    m_referenceIndex = qBound(0, index, qMax(0, m_cameras.size() - 1));
    updatePlots();
    refreshCorrectionsTable();
}

void MainWindow::refreshCorrectionsTable()
{
    int rows = 0;
    for (int i = 0; i < m_cameras.size(); ++i) {
        if (i == m_referenceIndex)
            continue;
        ++rows;
    }
    m_corrTable->setRowCount(rows);
    int r = 0;
    for (int i = 0; i < m_cameras.size(); ++i) {
        if (i == m_referenceIndex)
            continue;
        const CameraMatchResult &m = m_cameras[i].match;
        m_corrTable->setItem(r, 0, new QTableWidgetItem(m_cameras[i].label));
        m_corrTable->setItem(r, 1,
                             new QTableWidgetItem(QString::number(m.metrics.meanDeltaLuma, 'f', 2)));
        m_corrTable->setItem(r, 2,
                             new QTableWidgetItem(QString::number(m.metrics.histogramL1Rgb, 'f', 4)));
        m_corrTable->setItem(r, 3, new QTableWidgetItem(QString::number(m.damped.gainY, 'f', 3)));
        m_corrTable->setItem(r, 4,
                             new QTableWidgetItem(QString::number(m.damped.whiteBalanceKelvin)));
        m_corrTable->setItem(r, 5, new QTableWidgetItem(QString::number(m.damped.gainDb)));
        ++r;
    }
}

void MainWindow::updatePlots()
{
    if (m_cameras.isEmpty())
        return;
    const FrameColorAnalysis &a = m_cameras[m_referenceIndex].analysis;
    m_hist->setAnalysis(a);
    m_vectorscope->setAnalysis(a);
}