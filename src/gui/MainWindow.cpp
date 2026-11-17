#include "MainWindow.h"

#include "AtemAdvancedSettingsDialog.h"
#include "AtemPathsSetupDialog.h"
#include "gui/scopes/ScopeComputeWorker.h"
#include "gui/docks/ScopeDockManager.h"
#include "gui/docks/ScopeInstrumentWidget.h"
#include "gui/scopes/ScopeSettingsDialog.h"
#include "PreviewOverlayWidget.h"

#include "capture/CaptureRoute.h"
#include "capture/MultiviewGeometry.h"
#include "color/ColorAnalysis.h"
#include "color/ColorTuning.h"
#include "atem/CameraControlProtocol.h"
#include "color/ColorCharts.h"
#include "color/MatchEngine.h"
#include "platform/CameraPresetStore.h"
#include "color/ReferenceFrameStore.h"
#include "platform/AtemPathValidation.h"
#include "platform/AtemSetup.h"

#ifdef HAVE_GSTREAMER
#include "capture/GstDeviceEnumerator.h"
#include "capture/GstLivePreview.h"
#endif

#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QCoreApplication>
#include <QDateTime>
#include <algorithm>
#include <QDialog>
#include <QColor>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QListWidget>
#include <QMenu>
#include <cmath>
#include "scopes/ScopeQc.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDockWidget>
#include <QInputDialog>
#include <QStatusBar>
#include <QToolBar>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QMetaObject>
#include <QThread>
#include <QHeaderView>
#include <QFontMetrics>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QPixmap>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QSplitter>
#include <QStringList>
#include <QTableWidget>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>

#include <QLoggingCategory>

#include <chrono>
#include <thread>

Q_LOGGING_CATEGORY(lcRoute, "atem.route")

namespace {
QString summarizeChange(const CameraCorrectionBundle &a, const CameraCorrectionBundle &b);
}

namespace {

bool isMappableInput(const AtemInputInfo &input)
{
    const QString port = input.portType.toLower();
    const QStringList blocked = {QStringLiteral("aux"),        QStringLiteral("multiview"),
                                 QStringLiteral("black"),      QStringLiteral("color"),
                                 QStringLiteral("media"),      QStringLiteral("super"),
                                 QStringLiteral("key"),        QStringLiteral("audio"),
                                 QStringLiteral("mixeffect"),  QStringLiteral("internal")};
    for (const QString &word : blocked) {
        if (port.contains(word))
            return false;
    }
    return true;
}

bool selectComboKey(QComboBox *box, const QString &key, bool allowFirst)
{
    if (!box || box->count() == 0)
        return true;
    if (!key.isEmpty()) {
        const int found = box->findData(key);
        if (found < 0)
            return false;
        box->setCurrentIndex(found);
        return true;
    }
    if (!allowFirst)
        return false;
    box->setCurrentIndex(0);
    return true;
}

QString formatChannel(const QString &name, float r, float g, float b, float y)
{
    return QStringLiteral("  %1  R %2  G %3  B %4  Y %5")
        .arg(name)
        .arg(r, 0, 'f', 3)
        .arg(g, 0, 'f', 3)
        .arg(b, 0, 'f', 3)
        .arg(y, 0, 'f', 3);
}

QString formatBundle(const QString &label, qint64 atemId, const CameraCorrectionBundle &bundle,
                     const CorrectionParameterMask *mask = nullptr)
{
    QStringList lines;
    lines << QStringLiteral("%1 (вход ATEM %2)").arg(label).arg(atemId);
    lines << formatChannel(QStringLiteral("Lift "), bundle.liftR, bundle.liftG, bundle.liftB, bundle.liftY);
    lines << formatChannel(QStringLiteral("Gamma"), bundle.gammaR, bundle.gammaG, bundle.gammaB, bundle.gammaY);
    lines << formatChannel(QStringLiteral("Gain "), bundle.gainR, bundle.gainG, bundle.gainB, bundle.gainY);
    lines << QStringLiteral("  Насыщенность %1   Оттенок %2")
                 .arg(bundle.saturation, 0, 'f', 3)
                 .arg(bundle.hue, 0, 'f', 3);
    lines << QStringLiteral("  Баланс белого %1 K, tint %2")
                 .arg(bundle.whiteBalanceKelvin)
                 .arg(bundle.whiteBalanceTint);
    lines << QStringLiteral("  Усиление %1 dB").arg(bundle.gainDb);
    if (bundle.irisFStopAv >= 0.0f)
        lines << QStringLiteral("  Iris %1").arg(bundle.irisFStopAv, 0, 'f', 2);
    if (mask)
        lines << QStringLiteral("  Отправка: %1").arg(describeMaskRu(*mask));
    return lines.join(QLatin1Char('\n'));
}

} // namespace

class DeviceScanThread final : public QThread
{
public:
    QVector<VideoDevice> devices;
    QString error;

protected:
    void run() override
    {
#ifdef HAVE_GSTREAMER
        devices = GstDevices::enumerate(&error);
#else
        error = QStringLiteral("GStreamer не собран");
#endif
    }
};

class CaptureJob final : public QThread
{
public:
    MainWindow *window = nullptr;
    CaptureRequest request;
    CaptureOutcome outcome;
    std::atomic<bool> cancel{false};

protected:
    void run() override
    {
        if (window)
            window->executeCapture(request, &cancel, &outcome);
    }
};

MainWindow::MainWindow(std::unique_ptr<AtemClient> client, const QString &atemIp, QWidget *parent)
    : QMainWindow(parent)
    , m_client(std::move(client))
{
    qRegisterMetaType<FrameColorAnalysis>();
    qRegisterMetaType<ScopeComputeRequest>();
    setWindowTitle(QStringLiteral("Atem Color AI"));
    resize(1480, 920);
    setDockNestingEnabled(true);
    setCorner(Qt::TopLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::BottomLeftCorner, Qt::LeftDockWidgetArea);
    setCorner(Qt::TopRightCorner, Qt::RightDockWidgetArea);
    setCorner(Qt::BottomRightCorner, Qt::RightDockWidgetArea);
    setStyleSheet(QStringLiteral(
        "QMainWindow{background:#1a1a1e;} QWidget{color:#dcdcdc;background:#1a1a1e;}"
        "QDockWidget::title{background:#2b2b32;padding:6px;} QToolBar{background:#242429;border:none;}"
        "QStatusBar{background:#121218;color:#bbb;} QComboBox,QLineEdit,QSpinBox,QTableWidget{background:#2a2a30;"
        "border:1px solid #3d3d48;}"));

    auto *settingsMenu = menuBar()->addMenu(QStringLiteral("Настройки"));
    settingsMenu->addAction(QStringLiteral("Пути ATEM…"), this, &MainWindow::openSettingsDialog);
    settingsMenu->addAction(QStringLiteral("Дополнительно…"), this, &MainWindow::openAdvancedSettingsDialog);
    settingsMenu->addSeparator();
    settingsMenu->addAction(QStringLiteral("О программе…"), this, [this] {
        QMessageBox box(this);
        box.setWindowTitle(QStringLiteral("О программе"));
        box.setIconPixmap(QIcon(QStringLiteral(":/icons/app-icon.png")).pixmap(QSize(96, 96)));
        box.setText(QStringLiteral("<b style=\"font-size:14pt\">Atem Color AI</b> %1").arg(QCoreApplication::applicationVersion()));
        box.setInformativeText(QStringLiteral(
            "Сведение цвета и экспозиции камер Blackmagic через пульт ATEM: захват через AUX, "
            "приборы, поправки Camera Control с откатом.<br>© Gafana"));
        box.exec();
    });
    settingsMenu->addAction(QStringLiteral("QC / допуски IRE…"), this, [this] {
        bool ok = false;
        const double lo = QInputDialog::getDouble(this, QStringLiteral("QC"),
                                                QStringLiteral("Нижний допуск IRE"),
                                                AtemAppSettings::qcIreLow(), -20, 100, 1, &ok);
        if (!ok)
            return;
        const double hi = QInputDialog::getDouble(this, QStringLiteral("QC"),
                                                  QStringLiteral("Верхний допуск IRE"),
                                                  AtemAppSettings::qcIreHigh(), 0, 120, 1, &ok);
        if (!ok)
            return;
        AtemAppSettings::setQcIreLow(static_cast<float>(lo));
        AtemAppSettings::setQcIreHigh(static_cast<float>(hi));
    });

    auto *scopesMenu = menuBar()->addMenu(QStringLiteral("Приборы"));
    for (ScopeKind kind : allScopeKinds()) {
        QAction *act = scopesMenu->addAction(scopeKindLabel(kind));
        connect(act, &QAction::triggered, this, [this, kind] {
            if (m_scopeDocks)
                m_scopeDocks->addInstrument(kind);
        });
    }
    m_presetMenu = menuBar()->addMenu(QStringLiteral("Пресеты камер"));
    auto *layoutMenu = menuBar()->addMenu(QStringLiteral("Рабочая область"));
    const auto addPreset = [&](const QString &id, const QString &title) {
        QAction *act = layoutMenu->addAction(title);
        connect(act, &QAction::triggered, this, [this, id] {
            if (m_scopeDocks)
                applyWorkspacePreset(id);
        });
    };
    addPreset(QStringLiteral("home"), QStringLiteral("Домашняя (превью, камеры, вектор, RGB)"));
    addPreset(QStringLiteral("camera_match"), QStringLiteral("Сведение камер"));
    addPreset(QStringLiteral("exposure"), QStringLiteral("Экспозиция"));
    addPreset(QStringLiteral("full"), QStringLiteral("Полный набор"));
    layoutMenu->addSeparator();
    layoutMenu->addAction(QStringLiteral("Сохранить раскладку…"), this, [this] {
        if (!m_scopeDocks)
            return;
        bool ok = false;
        const QString name = QInputDialog::getText(this, QStringLiteral("Раскладка"),
                                                   QStringLiteral("Имя:"), QLineEdit::Normal, QString(), &ok);
        if (ok && !name.trimmed().isEmpty())
            m_scopeDocks->saveCustomLayout(name.trimmed());
    });
    layoutMenu->addAction(QStringLiteral("Загрузить раскладку…"), this, [this] {
        if (!m_scopeDocks)
            return;
        const QStringList names = AtemAppSettings::dockLayoutPresetNames();
        if (names.isEmpty())
            return;
        bool ok = false;
        const QString picked =
            QInputDialog::getItem(this, QStringLiteral("Раскладка"), QStringLiteral("Имя:"), names, 0, false, &ok);
        if (ok)
            m_scopeDocks->loadCustomLayout(picked);
    });
    layoutMenu->addAction(QStringLiteral("Удалить раскладку…"), this, [this] {
        const QStringList names = AtemAppSettings::dockLayoutPresetNames();
        if (names.isEmpty())
            return;
        bool ok = false;
        const QString picked = QInputDialog::getItem(this, QStringLiteral("Удалить раскладку"), QStringLiteral("Имя:"),
                                                     names, 0, false, &ok);
        if (ok && QMessageBox::question(this, QStringLiteral("Удалить"), QStringLiteral("Удалить «%1»?").arg(picked))
                      == QMessageBox::Yes)
            AtemAppSettings::removeDockLayoutPreset(picked);
    });

    auto *toolbar = addToolBar(QStringLiteral("Управление"));
    toolbar->setMovable(false);
    toolbar->addAction(QStringLiteral("Источник"), this, [this] {
        if (m_sourceDock)
            m_sourceDock->setVisible(!m_sourceDock->isVisible());
    });
    toolbar->addAction(QStringLiteral("Настройки"), this, &MainWindow::openSettingsDialog);
    toolbar->addAction(QStringLiteral("Сведение камер"), this, [this] {
        if (m_scopeDocks)
            m_scopeDocks->applyLayoutPreset(QStringLiteral("camera_match"));
    });
    toolbar->addAction(QStringLiteral("Снимок (S)"), this, &MainWindow::onTakeSnapshot);
    toolbar->addAction(QStringLiteral("Наложение снимка"), this, [this] {
        if (m_scopeDocks)
            m_scopeDocks->enableSnapshotOverlay(!m_scopeDocks->snapshotOverlayEnabled());
    });

    m_statusStream = new QLabel(this);
    m_statusQc = new QLabel(this);
    m_statusFps = new QLabel(this);
    statusBar()->addWidget(m_statusStream, 1);
    statusBar()->addPermanentWidget(m_statusQc);
    statusBar()->addPermanentWidget(m_statusFps);

    m_sourcePanel = new QWidget(this);
    m_cameraPanel = new QWidget(this);
    auto *layout = new QVBoxLayout(m_sourcePanel);

    m_configBanner = new QLabel(m_sourcePanel);
    m_configBanner->setWordWrap(true);
    m_configBanner->setStyleSheet(
        QStringLiteral("QLabel { background: #fff8e6; border: 1px solid #e6c200; padding: 8px; }"));
    m_configBanner->setVisible(false);
    layout->addWidget(m_configBanner);

    auto *linkRow = new QHBoxLayout();
    linkRow->addWidget(new QLabel(QStringLiteral("IP пульта:"), m_sourcePanel));
    m_ipEdit = new QLineEdit(m_sourcePanel);
    m_ipEdit->setPlaceholderText(QStringLiteral("192.168.10.240"));
    m_ipEdit->setText(atemIp);
    m_ipEdit->setClearButtonEnabled(true);
    linkRow->addWidget(m_ipEdit, 1);
    m_btnConnect = new QPushButton(QStringLiteral("Подключить"), m_sourcePanel);
    linkRow->addWidget(m_btnConnect);
    m_linkStatus = new QLabel(QStringLiteral("Нет подключения"), m_sourcePanel);
    m_linkStatus->setWordWrap(true);
    linkRow->addWidget(m_linkStatus, 2);
    layout->addLayout(linkRow);

    auto *deviceRow = new QHBoxLayout();
    deviceRow->addWidget(new QLabel(QStringLiteral("Устройство:"), m_sourcePanel));
    m_deviceCombo = new QComboBox(m_sourcePanel);
    m_deviceCombo->setMinimumWidth(220);
    deviceRow->addWidget(m_deviceCombo, 2);
    deviceRow->addWidget(new QLabel(QStringLiteral("Вход:"), m_sourcePanel));
    m_inputCombo = new QComboBox(m_sourcePanel);
    m_inputCombo->setMinimumWidth(110);
    deviceRow->addWidget(m_inputCombo, 1);
    deviceRow->addWidget(new QLabel(QStringLiteral("Режим:"), m_sourcePanel));
    m_modeCombo = new QComboBox(m_sourcePanel);
    m_modeCombo->setMinimumWidth(170);
    deviceRow->addWidget(m_modeCombo, 1);
    auto *btnRefresh = new QPushButton(QStringLiteral("Обновить"), m_sourcePanel);
    deviceRow->addWidget(btnRefresh);
    layout->addLayout(deviceRow);

    auto *routeRow = new QHBoxLayout();
    routeRow->addWidget(new QLabel(QStringLiteral("На карту захвата:"), m_sourcePanel));
    m_routeCombo = new QComboBox(m_sourcePanel);
    m_routeCombo->addItem(QStringLiteral("Выход / AUX"), QStringLiteral("aux"));
    routeRow->addWidget(m_routeCombo);
    m_auxCombo = new QComboBox(m_sourcePanel);
    m_auxCombo->setMinimumWidth(160);
    routeRow->addWidget(m_auxCombo, 1);
    auto *btnAuxPgm = new QPushButton(QStringLiteral("AUX = Программа"), m_sourcePanel);
    btnAuxPgm->setToolTip(QStringLiteral("Вернуть на выбранный AUX программный выход (источник 10010)"));
    routeRow->addWidget(btnAuxPgm);
    connect(btnAuxPgm, &QPushButton::clicked, this, [this] {
        if (!m_client || !m_client->isConnected() || m_busy || currentAuxId() == 0)
            return;
        const QString err = m_client->setAuxSource(currentAuxId(), 10010);
        statusBar()->showMessage(err.isEmpty() ? QStringLiteral("AUX → Программа") : err, 5000);
    });
    m_layoutCombo = new QComboBox(m_sourcePanel);
    for (const QString &layoutId : multiviewLayoutIds())
        m_layoutCombo->addItem(multiviewLayoutLabel(layoutId), layoutId);
    routeRow->addWidget(m_layoutCombo, 1);
    // Capture always comes from one ATEM output (AUX) feeding the capture card.
    m_routeCombo->setVisible(false);
    m_layoutCombo->setVisible(false);
    layout->addLayout(routeRow);

    m_routeHelp = new QLabel(m_sourcePanel);
    m_routeHelp->setWordWrap(true);
    layout->addWidget(m_routeHelp);

    m_videoStatus = new QLabel(m_sourcePanel);
    m_videoStatus->setWordWrap(true);
    layout->addWidget(m_videoStatus);

    m_fpsLabel = new QLabel(QStringLiteral("Превью: нет кадров"), m_sourcePanel);
    layout->addWidget(m_fpsLabel);

    auto *cameraLayout = new QVBoxLayout(m_cameraPanel);
    cameraLayout->addWidget(new QLabel(QStringLiteral("Связка камер"), m_cameraPanel));
    m_mapTable = new QTableWidget(m_cameraPanel);
    m_mapTable->setColumnCount(4);
    m_mapTable->setHorizontalHeaderLabels({QStringLiteral("Камера"), QStringLiteral("Вход ATEM"),
                                           QStringLiteral("Окно MV"), QStringLiteral("В подстройке")});
    m_mapTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_mapTable->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_mapTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_mapTable->setMinimumHeight(120);
    m_mapTable->setMaximumHeight(180);
    cameraLayout->addWidget(m_mapTable);

    auto *mapButtons = new QHBoxLayout();
    auto *btnAdd = new QPushButton(QStringLiteral("Добавить камеру"), m_cameraPanel);
    auto *btnRemove = new QPushButton(QStringLiteral("Удалить"), m_cameraPanel);
    mapButtons->addWidget(btnAdd);
    mapButtons->addWidget(btnRemove);
    mapButtons->addStretch(1);
    cameraLayout->addLayout(mapButtons);

    auto *tuningRow = new QHBoxLayout();
    tuningRow->addWidget(new QLabel(QStringLiteral("Режим:"), m_cameraPanel));
    m_tuningModeCombo = new QComboBox(m_cameraPanel);
    for (ColorMatchMode mode : colorMatchModesForUi())
        m_tuningModeCombo->addItem(colorMatchModeLabel(mode), colorMatchModeId(mode));
    tuningRow->addWidget(m_tuningModeCombo, 3);
    m_referenceCombo = new QComboBox(m_cameraPanel);
    m_referenceCombo->setToolTip(QStringLiteral("Эталонная камера"));
    m_refLabel = new QLabel(QStringLiteral("Эталон:"), m_cameraPanel);
    tuningRow->addWidget(m_refLabel);
    tuningRow->addWidget(m_referenceCombo, 2);
    m_sampleTargetCombo = new QComboBox(m_cameraPanel);
    m_sampleTargetCombo->addItem(QStringLiteral("цель: эталонная камера"), QStringLiteral("reference"));
    m_sampleTargetCombo->addItem(QStringLiteral("цель: среднее камер"), QStringLiteral("average"));
    m_sampleTargetCombo->addItem(QStringLiteral("цель: нейтраль (белый/серый)"), QStringLiteral("neutral"));
    tuningRow->addWidget(m_sampleTargetCombo, 2);
    m_chartCombo = new QComboBox(m_cameraPanel);
    for (const ChartPreset &chart : ColorCharts::presets())
        m_chartCombo->addItem(chart.name, chart.id);
    m_chartCombo->setToolTip(QStringLiteral("Эталонные значения: опубликованные производителем CIELAB D50"));
    tuningRow->addWidget(m_chartCombo, 3);
    cameraLayout->addLayout(tuningRow);

    m_modeHint = new QLabel(m_cameraPanel);
    m_modeHint->setWordWrap(true);
    m_modeHint->setStyleSheet(QStringLiteral("color:#9aa0aa;"));
    cameraLayout->addWidget(m_modeHint);

    auto *markRow = new QHBoxLayout();
    m_markLabel = new QLabel(QStringLiteral("Разметка для:"), m_cameraPanel);
    markRow->addWidget(m_markLabel);
    m_markCameraCombo = new QComboBox(m_cameraPanel);
    m_markCameraCombo->setToolTip(QStringLiteral("Камера выводится на AUX, область рисуется мышкой на превью"));
    markRow->addWidget(m_markCameraCombo, 1);
    m_btnClearZone = new QPushButton(QStringLiteral("Сбросить область"), m_cameraPanel);
    m_btnClearZone->setToolTip(QStringLiteral("Убрать область этой камеры — замер по всему кадру "
                                              "(правый клик на превью делает то же)"));
    connect(m_btnClearZone, &QPushButton::clicked, this, [this] {
        if (!m_previewOverlay)
            return;
        if (m_tuningMode == ColorMatchMode::ColorChecker) {
            m_previewOverlay->setCheckerCorners({});
            onCheckerRoiEdited({});
        } else {
            m_previewOverlay->setGrayRoi({});
            onGrayRoiEdited({});
        }
    });
    markRow->addWidget(m_btnClearZone);
    m_btnSaveRef = new QPushButton(QStringLiteral("Сохранить эталон"), m_cameraPanel);
    m_btnLoadRef = new QPushButton(QStringLiteral("Загрузить эталон…"), m_cameraPanel);
    markRow->addWidget(m_btnSaveRef);
    markRow->addWidget(m_btnLoadRef);
    cameraLayout->addLayout(markRow);

    // What the engine may change and through which camera controls.
    QHBoxLayout *paramRow = new QHBoxLayout();
    m_chkBalance = new QCheckBox(QStringLiteral("Цвет"), m_cameraPanel);
    m_chkExposure = new QCheckBox(QStringLiteral("Уровень"), m_cameraPanel);
    m_chkFitLift = new QCheckBox(QStringLiteral("Лифт"), m_cameraPanel);
    m_chkFitGamma = new QCheckBox(QStringLiteral("Гамма"), m_cameraPanel);
    m_chkFitSat = new QCheckBox(QStringLiteral("Насыщ."), m_cameraPanel);
    m_chkAutoIris = new QCheckBox(QStringLiteral("+ авто-диафрагма"), m_cameraPanel);
    m_chkUnlimited = new QCheckBox(QStringLiteral("Без ограничений (тест)"), m_cameraPanel);
    m_chkUnlimited->setToolTip(QStringLiteral(
        "Тестовый режим: без ограничения шага за проход и без проверки суммарного ухода от исходных значений — "
        "поправка отправляется целиком (остаются только допустимые диапазоны камеры). Авто-подгонка применяет "
        "и предложения с предупреждениями. Снимите галочку для безопасного режима в эфире."));
    for (QCheckBox *box : {m_chkBalance, m_chkExposure, m_chkFitLift, m_chkFitGamma, m_chkFitSat, m_chkAutoIris})
        paramRow->addWidget(box);
    paramRow->addStretch(1);
    cameraLayout->addLayout(paramRow);
    paramRow = new QHBoxLayout();
    m_balanceViaCombo = new QComboBox(m_cameraPanel);
    m_balanceViaCombo->addItem(QStringLiteral("цвет: авто"), 3);
    m_balanceViaCombo->addItem(QStringLiteral("цвет через ББ (K/tint)"), 1);
    m_balanceViaCombo->addItem(QStringLiteral("цвет через CC Gain"), 0);
    m_balanceViaCombo->setItemData(0, QStringLiteral("Авто: основной оттенок — балансом белого камеры (K/tint), "
                                                     "остаток — CC Gain R/G/B"), Qt::ToolTipRole);
    paramRow->addWidget(m_balanceViaCombo);
    m_exposureViaCombo = new QComboBox(m_cameraPanel);
    m_exposureViaCombo->addItem(QStringLiteral("уровень: авто"), 3);
    m_exposureViaCombo->addItem(QStringLiteral("уровень через CC Gain"), 0);
    m_exposureViaCombo->addItem(QStringLiteral("уровень через усиление dB"), 1);
    m_exposureViaCombo->addItem(QStringLiteral("уровень через диафрагму"), 2);
    m_exposureViaCombo->setItemData(0, QStringLiteral("Авто: сначала свет — диафрагма (f/2…f/11), затем усиление dB "
                                                      "(как можно ниже), CC Gain Y только для точной подстройки. "
                                                      "При пересвете — прикрыть диафрагму / снизить усиление."),
                                    Qt::ToolTipRole);
    paramRow->addWidget(m_exposureViaCombo);
    m_targetIreSpin = new QDoubleSpinBox(m_cameraPanel);
    m_targetIreSpin->setRange(20.0, 70.0);
    m_targetIreSpin->setDecimals(0);
    m_targetIreSpin->setSuffix(QStringLiteral(" IRE"));
    m_targetIreSpin->setPrefix(QStringLiteral("серый "));
    m_targetIreSpin->setToolTip(QStringLiteral("Целевой уровень серого (18 % серый ≈ 41 IRE в Rec.709)"));
    paramRow->addWidget(m_targetIreSpin);
    m_strengthSpin = new QSpinBox(m_cameraPanel);
    m_strengthSpin->setRange(10, 100);
    m_strengthSpin->setSuffix(QStringLiteral(" %"));
    m_strengthSpin->setPrefix(QStringLiteral("сила "));
    m_strengthSpin->setToolTip(QStringLiteral("Какая доля рассчитанной поправки отправляется"));
    paramRow->addWidget(m_strengthSpin);
    paramRow->addWidget(m_chkUnlimited);
    paramRow->addStretch(1);
    cameraLayout->addLayout(paramRow);

    // legacy widgets kept for settings compatibility (hidden)
    m_grayTargetSpin = new QSpinBox(m_cameraPanel);
    m_grayTargetSpin->setRange(40, 200);
    m_grayTargetSpin->setVisible(false);
    m_chkWhiteBalance = new QCheckBox(m_cameraPanel);
    m_chkTint = new QCheckBox(m_cameraPanel);
    m_chkLift = new QCheckBox(m_cameraPanel);
    m_chkGamma = new QCheckBox(m_cameraPanel);
    m_chkGain = new QCheckBox(m_cameraPanel);
    m_chkLevel = new QCheckBox(m_cameraPanel);
    for (QCheckBox *box : {m_chkWhiteBalance, m_chkTint, m_chkLift, m_chkGamma, m_chkGain, m_chkLevel}) {
        box->setChecked(true);
        box->setVisible(false);
    }

    m_corrTable = new QTableWidget(m_cameraPanel);
    m_corrTable->setColumnCount(5);
    m_corrTable->setHorizontalHeaderLabels({QStringLiteral("Камера"), QStringLiteral("Ошибка, IRE"),
                                            QStringLiteral("ΔE2000"), QStringLiteral("Что изменится"),
                                            QStringLiteral("Внимание")});
    // Long texts: every column can be resized by the user, text wraps, the full text is in the tooltip.
    m_corrTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Interactive);
    m_corrTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
    m_corrTable->horizontalHeader()->setStretchLastSection(false);
    m_corrTable->horizontalHeader()->setMinimumSectionSize(50);
    m_corrTable->setColumnWidth(0, 60);
    m_corrTable->setColumnWidth(1, 80);
    m_corrTable->setColumnWidth(2, 130);
    m_corrTable->setColumnWidth(4, 170);
    m_corrTable->setWordWrap(true);
    m_corrTable->setTextElideMode(Qt::ElideNone);
    m_corrTable->verticalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_corrTable->verticalHeader()->setVisible(false);
    m_corrTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    connect(m_corrTable->horizontalHeader(), &QHeaderView::sectionResized, m_corrTable,
            [this] { m_corrTable->resizeRowsToContents(); });
    m_corrTable->setMinimumHeight(90);
    m_corrTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_corrTable->setSelectionMode(QAbstractItemView::SingleSelection);
    cameraLayout->addWidget(m_corrTable, 1);
    // Full text of the selected row (nothing is cut off, even in a narrow panel).
    m_corrDetails = new QLabel(m_cameraPanel);
    m_corrDetails->setWordWrap(true);
    m_corrDetails->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_corrDetails->setStyleSheet(QStringLiteral("QLabel { background: #23262b; border: 1px solid #3a3f46; padding: 4px; }"));
    m_corrDetails->setVisible(false);
    cameraLayout->addWidget(m_corrDetails);
    connect(m_corrTable, &QTableWidget::currentCellChanged, this, [this](int row, int, int, int) {
        if (!m_corrDetails)
            return;
        if (row < 0 || !m_corrTable->item(row, 0)) {
            m_corrDetails->setVisible(false);
            return;
        }
        QStringList lines;
        lines << QStringLiteral("<b>%1</b>").arg(m_corrTable->item(row, 0)->text().toHtmlEscaped());
        const QStringList heads = {QString(), QStringLiteral("Ошибка, IRE"), QStringLiteral("ΔE2000"),
                                   QStringLiteral("Что изменится"), QStringLiteral("Внимание")};
        for (int c = 1; c < 5; ++c) {
            const QTableWidgetItem *it = m_corrTable->item(row, c);
            if (it && !it->text().isEmpty() && it->text() != QStringLiteral("—"))
                lines << QStringLiteral("<b>%1:</b> %2").arg(heads.at(c), it->text().toHtmlEscaped().replace(
                                                                               QLatin1Char('\n'), QStringLiteral("<br>")));
        }
        m_corrDetails->setText(lines.join(QStringLiteral("<br>")));
        m_corrDetails->setVisible(true);
    });

    auto *buttons = new QHBoxLayout();
    m_btnCapture = new QPushButton(QStringLiteral("1. Захват и расчёт"), m_cameraPanel);
    m_btnApply = new QPushButton(QStringLiteral("2. Применить…"), m_cameraPanel);
    m_btnRollback = new QPushButton(QStringLiteral("Откат"), m_cameraPanel);
    m_btnCapture->setToolTip(QStringLiteral("AUX по очереди переключается на камеры, кадры усредняются ~0,75 с"));
    m_btnApply->setToolTip(QStringLiteral("Выбор камер, проверка значений и отправка на пульт"));
    m_btnLoop = new QPushButton(QStringLiteral("Авто-подгонка (3 прохода)"), m_cameraPanel);
    m_btnLoop->setToolTip(QStringLiteral(
        "Замкнутый цикл: захват → расчёт → применение → пауза ~0,8 с → повторный замер, до 3 применений. "
        "Каждый шаг ограничен, ΔE показывается по проходам. «Откат» вернёт значения до начала."));
    buttons->addWidget(m_btnCapture);
    buttons->addWidget(m_btnApply);
    buttons->addWidget(m_btnLoop);
    m_btnLoopUntil = new QPushButton(QStringLiteral("Авто до допуска"), m_cameraPanel);
    m_btnLoopUntil->setToolTip(QStringLiteral(
        "Проходы повторяются, пока ΔE сцены не станет не больше допуска. Останавливается сама: допуск достигнут, "
        "%1 проходов, %2 прохода без улучшения, тёмный кадр / нет сигнала; повторное нажатие — «Стоп».")
                                   .arg(m_loopUntilMax)
                                   .arg(m_loopStallPasses));
    m_loopTargetSpin = new QDoubleSpinBox(m_cameraPanel);
    m_loopTargetSpin->setRange(0.5, 10.0);
    m_loopTargetSpin->setSingleStep(0.1);
    m_loopTargetSpin->setDecimals(1);
    m_loopTargetSpin->setPrefix(QStringLiteral("ΔE ≤ "));
    m_loopTargetSpin->setValue(1.5);
    m_loopTargetSpin->setToolTip(QStringLiteral("Допуск для «Авто до допуска» (ΔE2000 сцены против эталона)"));
    buttons->addWidget(m_btnLoopUntil);
    buttons->addWidget(m_loopTargetSpin);
    buttons->addWidget(m_btnRollback);
    cameraLayout->addLayout(buttons);

    auto *central = new QWidget(this);
    central->setMinimumSize(0, 0);
    setCentralWidget(central);
    central->hide(); // placeholder only: the docks share the whole window

    m_previewOverlay = new PreviewOverlayWidget();
    m_scopeDocks = new ScopeDockManager(this, m_previewOverlay, this);
    m_scopeDocks->ensurePreviewDock();

    m_sourceDock = new QDockWidget(QStringLiteral("Источник"), this);
    m_sourceDock->setWidget(m_sourcePanel);
    m_sourceDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                              | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::TopDockWidgetArea, m_sourceDock);

    m_cameraDock = new QDockWidget(QStringLiteral("Камеры и подстройка"), this);
    m_cameraDock->setWidget(m_cameraPanel);
    m_cameraDock->setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable
                               | QDockWidget::DockWidgetClosable);
    addDockWidget(Qt::BottomDockWidgetArea, m_cameraDock);
    m_sourceDock->setObjectName(QStringLiteral("dock_source"));
    m_cameraDock->setObjectName(QStringLiteral("dock_cameras"));

    // Workspace: last session (instances, scope settings, dock geometry) or the selected preset.
    if (!m_scopeDocks->loadCustomLayout(ScopeDockManager::sessionLayoutName()))
        applyWorkspacePreset(AtemAppSettings::scopeLayoutPreset());

    connect(m_btnConnect, &QPushButton::clicked, this, &MainWindow::onConnectClicked);
    connect(m_ipEdit, &QLineEdit::textEdited, this, [this] { m_ipEdited = true; });
    connect(m_ipEdit, &QLineEdit::editingFinished, this, [this] {
        if (!m_ipEdited)
            return;
        AtemAppSettings::setIp(m_ipEdit->text());
    });
    connect(btnRefresh, &QPushButton::clicked, this, &MainWindow::onRefreshDevices);
    connect(m_deviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onDeviceChanged);
    connect(m_inputCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onVideoOptionChanged);
    connect(m_modeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onVideoOptionChanged);
    connect(m_routeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onRouteChanged);
    connect(m_auxCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onAuxChanged);
    connect(m_layoutCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onLayoutChanged);
    connect(btnAdd, &QPushButton::clicked, this, &MainWindow::onAddCamera);
    connect(btnRemove, &QPushButton::clicked, this, &MainWindow::onRemoveCamera);
    connect(m_mapTable, &QTableWidget::itemChanged, this, &MainWindow::onMappingEdited);
    connect(m_btnCapture, &QPushButton::clicked, this, &MainWindow::onCaptureAnalyze);
    connect(m_btnApply, &QPushButton::clicked, this, &MainWindow::onApply);
    connect(m_btnLoop, &QPushButton::clicked, this, [this] {
        m_loopUntilTol = false;
        onAutoMatchLoop();
    });
    connect(m_btnLoopUntil, &QPushButton::clicked, this, [this] {
        if (m_loopPass > 0) { // running: «Стоп»
            m_loopCancel = true;
            m_btnLoopUntil->setText(QStringLiteral("Останавливаю…"));
            return;
        }
        m_loopUntilTol = true;
        onAutoMatchLoop();
    });
    connect(m_btnRollback, &QPushButton::clicked, this, [this] {
        if (!m_client || m_busy)
            return;
        syncCamerasFromTable();
        for (CameraRow &camera : m_cameras) {
            if (!camera.hasRollback)
                continue;
            const QString err = m_client->rollbackCorrection(camera.binding.atemId, camera.rollback);
            qCInfo(lcRoute).noquote() << QStringLiteral("rollback %1 %2 %3")
                                             .arg(camera.binding.label, err.isEmpty() ? QStringLiteral("ok") : err,
                                                  QString::fromUtf8(QJsonDocument(camera.rollback.toJson())
                                                                        .toJson(QJsonDocument::Compact)));
            camera.hasRollback = false;
        }
        statusBar()->showMessage(QStringLiteral("Откат выполнен"), 8000);
        QMessageBox::information(this, QString(), QStringLiteral("Откат выполнен"));
    });
    connect(m_referenceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onReferenceChanged);
    connect(m_tuningModeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onTuningModeChanged);
    connect(m_markCameraCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onMarkCameraChanged);
    connect(m_btnSaveRef, &QPushButton::clicked, this, &MainWindow::onSaveReferenceFrame);
    connect(m_btnLoadRef, &QPushButton::clicked, this, &MainWindow::onLoadReferenceFrame);
    connect(m_previewOverlay, &PreviewOverlayWidget::grayRoiEdited, this, &MainWindow::onGrayRoiEdited);
    connect(m_previewOverlay, &PreviewOverlayWidget::checkerCornersEdited, this,
            &MainWindow::onCheckerRoiEdited);
    auto connectMask = [this](QCheckBox *box) {
        connect(box, &QCheckBox::toggled, this, &MainWindow::onParamMaskChanged);
    };
    connectMask(m_chkWhiteBalance);
    connectMask(m_chkTint);
    connectMask(m_chkLift);
    connectMask(m_chkGamma);
    connectMask(m_chkGain);
    connectMask(m_chkLevel);
    for (QCheckBox *box : {m_chkBalance, m_chkExposure, m_chkFitLift, m_chkFitGamma, m_chkFitSat, m_chkAutoIris,
                           m_chkUnlimited})
        connect(box, &QCheckBox::toggled, this, [this] { saveEngineSettings(); });
    for (QComboBox *combo : {m_balanceViaCombo, m_exposureViaCombo, m_chartCombo, m_sampleTargetCombo})
        connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, [this] {
            saveEngineSettings();
            updateTuningPanelVisibility();
        });
    connect(m_strengthSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this] { saveEngineSettings(); });
    // Double click on a camera row: show that camera on the capture AUX (quick visual check).
    const auto showOnAux = [this](qint64 atemId, const QString &label) {
        if (!m_client || !m_client->isConnected() || m_busy || currentAuxId() == 0 || atemId <= 0)
            return;
        const QString err = m_client->setAuxSource(currentAuxId(), atemId);
        statusBar()->showMessage(err.isEmpty() ? QStringLiteral("AUX → %1").arg(label)
                                               : QStringLiteral("AUX: %1").arg(err),
                                 4000);
    };
    connect(m_mapTable, &QTableWidget::cellDoubleClicked, this, [this, showOnAux](int row, int) {
        const CameraBinding b = bindingFromRow(row);
        showOnAux(b.atemId, b.label);
    });
    connect(m_corrTable, &QTableWidget::cellDoubleClicked, this, [this, showOnAux](int row, int) {
        QTableWidgetItem *item = m_corrTable->item(row, 0);
        if (!item)
            return;
        for (const CameraRow &c : m_cameras)
            if (c.binding.label == item->text())
                showOnAux(c.binding.atemId, c.binding.label);
    });
    connect(m_targetIreSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this,
            [this] { saveEngineSettings(); });
    connect(m_grayTargetSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [](int value) {
        AtemAppSettings::setGrayTargetLuma(value);
    });

    m_scopeThread = new QThread(this);
    m_scopeWorker = new ScopeComputeWorker();
    m_scopeWorker->moveToThread(m_scopeThread);
    connect(m_scopeThread, &QThread::finished, m_scopeWorker, &QObject::deleteLater);
    connect(m_scopeDocks, &ScopeDockManager::needCompute, this, &MainWindow::onScopeComputeRequested);
    connect(m_scopeWorker, &ScopeComputeWorker::instrumentResult, this, &MainWindow::onInstrumentResult,
            Qt::QueuedConnection);
    connect(m_scopeDocks, &ScopeDockManager::previewOverlay, this, &MainWindow::onScopePreviewOverlay);
    connect(m_scopeDocks, &ScopeDockManager::instrumentSettings, this, &MainWindow::onInstrumentSettings);
    connect(m_scopeDocks, &ScopeDockManager::scopeFpsUpdated, this, &MainWindow::onScopeFpsUpdated);
    m_scopeThread->start();

    qApp->installEventFilter(this);

#ifdef HAVE_GSTREAMER
    m_preview = new GstLivePreview(this);
    connect(m_preview, &GstLivePreview::frameReady, this, &MainWindow::onPreviewFrame);
    connect(m_preview, &GstLivePreview::analysisReady, this, &MainWindow::onPreviewAnalysis);
    connect(m_preview, &GstLivePreview::previewError, this, &MainWindow::onPreviewError);
#else
    m_videoStatus->setText(QStringLiteral("GStreamer не собран — список устройств и превью недоступны"));
#endif

    loadRouteControls();
    loadTuningUi();
    rebuildPresetMenu();
    loadMappingFromSettings();
    syncCamerasFromTable();
    loadCameraRois();
    rebuildAtemAvailabilityUi();

    for (QComboBox *combo : findChildren<QComboBox *>())
        fitComboPopup(combo);
    QTimer::singleShot(0, this, [this] { connectAtem(false); });
    // Link watchdog: the SDK reports "disconnected" asynchronously (cable / power loss).
    auto *linkTimer = new QTimer(this);
    linkTimer->setInterval(2000);
    connect(linkTimer, &QTimer::timeout, this, [this] {
        if (m_closing.load())
            return;
        const qint64 nowMs = QDateTime::currentMSecsSinceEpoch();
        // Capture watchdog: frames stopped without an error (signal gone, card hung).
        if (m_previewRunning && !m_previewStalled && m_lastPreviewFrameMs > 0
            && nowMs - m_lastPreviewFrameMs > 3000) {
            m_previewStalled = true;
            qCWarning(lcRoute) << "preview stalled: no frames for" << (nowMs - m_lastPreviewFrameMs) << "ms";
            if (m_fpsLabel)
                m_fpsLabel->setText(QStringLiteral("Превью: кадры не поступают"));
            statusBar()->showMessage(
                QStringLiteral("С карты захвата не поступают кадры — проверьте сигнал AUX и кабель"), 15000);
        }
        // Automatic restart after a capture error (not during a capture pass).
        if (!m_previewRunning && m_previewRetryAtMs > 0 && nowMs >= m_previewRetryAtMs && !m_busy) {
            m_previewRetryAtMs = 0;
            startPreview();
        }
        if (!m_atemWasConnected || !m_client || m_busy || m_closing.load())
            return;
        if (m_client->isConnected())
            return;
        m_atemWasConnected = false;
        qCWarning(lcRoute) << "ATEM connection lost";
        m_client->disconnect();
        if (m_linkStatus)
            m_linkStatus->setText(QStringLiteral("Связь с ATEM потеряна — проверьте сеть и нажмите «Подключить»"));
        statusBar()->showMessage(QStringLiteral("Связь с ATEM потеряна"), 15000);
        rebuildAtemAvailabilityUi();
    });
    linkTimer->start();
    QTimer::singleShot(0, this, [this] { onRefreshDevices(); });
}

MainWindow::~MainWindow()
{
    m_closing.store(true);
    if (m_captureJob) {
        m_captureJob->cancel.store(true);
        while (m_captureJob->isRunning())
            QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
    }
#ifdef HAVE_GSTREAMER
    if (m_preview) {
        disconnect(m_preview, nullptr, this, nullptr);
        m_preview->stopPreview();
    }
#endif
    if (m_deviceScan) {
        disconnect(m_deviceScan, nullptr, this, nullptr);
        m_deviceScan->wait(5000);
    }
    if (m_scopeThread) {
        m_scopeThread->quit();
        m_scopeThread->wait(3000);
    }
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
        m_btnApply->setEnabled(!m_busy && atemOk && m_client && m_client->isConnected());
        if (m_busy)
            m_btnApply->setToolTip(QStringLiteral("Идёт захват"));
        else if (!atemOk)
            m_btnApply->setToolTip(QStringLiteral(
                "Укажите корректный путь к ATEM Software Control в «Настройки → Пути ATEM…»"));
        else if (!m_client || !m_client->isConnected())
            m_btnApply->setToolTip(QStringLiteral("Нет подключения к ATEM"));
        else
            m_btnApply->setToolTip(QString());
    }
}

void MainWindow::openSettingsDialog()
{
    AtemPathsSetupDialog dlg(this);
    dlg.setSoftwareControlPath(AtemAppSettings::softwareControlPath());
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QString control =
        AtemPathValidation::normalizeSoftwareControlPath(dlg.softwareControlPath());
    AtemAppSettings::save(AtemAppSettings::sdkPath(), control, true);
    rebuildAtemAvailabilityUi();
}

void MainWindow::openAdvancedSettingsDialog()
{
    AtemAdvancedSettingsDialog dlg(this);
    dlg.setSdkPath(AtemAppSettings::sdkPath());
    if (dlg.exec() != QDialog::Accepted)
        return;
    AtemAppSettings::save(dlg.sdkPath(), AtemAppSettings::softwareControlPath(), true);
}

void MainWindow::onConnectClicked()
{
    m_ipEdited = true;
    connectAtem(true);
}

void MainWindow::connectAtem(bool persistIp)
{
    if (m_closing.load() || !m_client)
        return;
    const QString ip = m_ipEdit ? m_ipEdit->text().trimmed() : QString();
    if (persistIp)
        AtemAppSettings::setIp(ip);
    if (ip.isEmpty()) {
        if (m_linkStatus)
            m_linkStatus->setText(QStringLiteral("IP не задан"));
        QMessageBox::warning(
            this, QStringLiteral("IP пульта"),
            QStringLiteral("Введите IP-адрес пульта ATEM.\n\n"
                           "Адрес хранится в settings.ini, ключ atem/ip.\n"
                           "Ключ --atem-ip <адрес> переопределяет его на один запуск."));
        return;
    }

    const QString error = m_client->connectTo(ip);
    if (!error.isEmpty()) {
        qCWarning(lcRoute).noquote() << QStringLiteral("ATEM connect %1 failed: %2").arg(ip, error);
        m_inputs.clear();
        m_auxes.clear();
        refreshAuxList();
        if (m_linkStatus)
            m_linkStatus->setText(error);
        QMessageBox::critical(this, QStringLiteral("Не удалось подключиться к ATEM"),
                              QStringLiteral("Адрес: %1\n\n%2").arg(ip, error));
        rebuildAtemAvailabilityUi();
        return;
    }

    m_atemWasConnected = true;
    if (m_linkStatus) {
        m_linkStatus->setText(
            QStringLiteral("Подключено к %1 (%2)").arg(ip, m_client->backendName()));
    }
    m_inputs = m_client->listInputs();
    m_auxes = m_client->listAuxOutputs();
    refreshAuxList();
    reloadMappingCombos();
    if (m_mapTable && m_mapTable->rowCount() == 0)
        seedMappingFromInputs();
    syncCamerasFromTable();
    rebuildAtemAvailabilityUi();
}

void MainWindow::refreshAuxList()
{
    if (!m_auxCombo)
        return;
    const qint64 saved = AtemAppSettings::auxOutputId();
    QSignalBlocker blocker(m_auxCombo);
    m_auxCombo->clear();
    int found = -1;
    for (int i = 0; i < m_auxes.size(); ++i) {
        m_auxCombo->addItem(QStringLiteral("%1 (id %2)").arg(m_auxes.at(i).name).arg(m_auxes.at(i).id),
                            QVariant::fromValue(m_auxes.at(i).id));
        if (m_auxes.at(i).id == saved)
            found = i;
    }
    fitComboPopup(m_auxCombo);
    if (found >= 0)
        m_auxCombo->setCurrentIndex(found);
    else
        m_auxCombo->setCurrentIndex(-1);
    if (found < 0 && saved != 0 && m_client && m_client->isConnected() && m_videoStatus) {
        m_videoStatus->setText(
            QStringLiteral("Сохранённый выход AUX id %1 не найден. Другой выход не выбран.")
                .arg(saved));
    }
}

void MainWindow::loadRouteControls()
{
    m_loading = true;
    // Only the AUX route is supported (PGM / Multiview were removed).
    const int routeIndex = m_routeCombo->findData(QStringLiteral("aux"));
    if (AtemAppSettings::captureRoute() != QLatin1String("aux"))
        AtemAppSettings::setCaptureRoute(QStringLiteral("aux"));
    m_routeCombo->setCurrentIndex(routeIndex);

    const QString layout = AtemAppSettings::multiviewLayout();
    int layoutIndex = m_layoutCombo->findData(layout);
    if (layout.isEmpty()) {
        layoutIndex = m_layoutCombo->findData(QStringLiteral("2x2"));
        if (layoutIndex >= 0)
            AtemAppSettings::setMultiviewLayout(QStringLiteral("2x2"));
    }
    m_layoutCombo->setCurrentIndex(layoutIndex);
    m_loading = false;
    updateRouteWidgets();
    updateWindowSpinRange();
}

void MainWindow::updateRouteWidgets()
{
    const QString route = currentRoute();
    if (m_auxCombo)
        m_auxCombo->setEnabled(route == QLatin1String("aux") && !m_busy);
    if (m_layoutCombo)
        m_layoutCombo->setEnabled(route == QLatin1String("multiview") && !m_busy);
    if (m_mapTable)
        m_mapTable->setColumnHidden(2, route != QLatin1String("multiview"));
    if (!m_routeHelp)
        return;
    if (route == QLatin1String("aux")) {
        m_routeHelp->setText(QStringLiteral(
            "На карту должен приходить выбранный выход AUX. Перед кадром он переключается на камеру "
            "и затем возвращается на прежний источник. Другие выходы и эфир не трогаются."));
    } else if (route == QLatin1String("multiview")) {
        m_routeHelp->setText(QStringLiteral(
            "На карту должен приходить мультивью в выбранной раскладке. Программа только вырезает "
            "окно камеры и ничего не переключает на пульте."));
    } else if (route == QLatin1String("pgm")) {
        m_routeHelp->setText(QStringLiteral(
            "На карту должен приходить программный выход. Камеры по очереди выводятся в эфир. "
            "Без подтверждения в диалоге переключения не будет."));
    } else {
        m_routeHelp->setText(QStringLiteral("Выберите, что именно приходит на карту захвата."));
    }
}

void MainWindow::updateWindowSpinRange()
{
    const int count = multiviewWindowCount(currentLayout());
    const int maxWindow = qMax(0, (count > 0 ? count : 16) - 1);
    if (!m_mapTable)
        return;
    for (int row = 0; row < m_mapTable->rowCount(); ++row) {
        auto *spin = qobject_cast<QSpinBox *>(m_mapTable->cellWidget(row, 2));
        if (!spin)
            continue;
        QSignalBlocker blocker(spin);
        const int value = spin->value();
        spin->setRange(0, maxWindow);
        spin->setValue(qMin(value, maxWindow));
    }
    if (!m_loading)
        saveMapping();
}

QString MainWindow::currentRoute() const
{
    if (!m_routeCombo || m_routeCombo->currentIndex() < 0)
        return {};
    return m_routeCombo->currentData().toString();
}

qint64 MainWindow::currentAuxId() const
{
    if (!m_auxCombo || m_auxCombo->currentIndex() < 0)
        return 0;
    return m_auxCombo->currentData().toLongLong();
}

QString MainWindow::currentLayout() const
{
    if (!m_layoutCombo || m_layoutCombo->currentIndex() < 0)
        return {};
    return m_layoutCombo->currentData().toString();
}

void MainWindow::onRouteChanged()
{
    if (m_loading)
        return;
    const QString route = currentRoute();
    if (!route.isEmpty())
        AtemAppSettings::setCaptureRoute(route);
    updateRouteWidgets();
}

void MainWindow::onAuxChanged(int index)
{
    if (m_loading || index < 0)
        return;
    AtemAppSettings::setAuxOutputId(currentAuxId());
}

void MainWindow::onLayoutChanged(int index)
{
    if (m_loading || index < 0)
        return;
    const QString layout = currentLayout();
    if (!layout.isEmpty())
        AtemAppSettings::setMultiviewLayout(layout);
    updateWindowSpinRange();
}

void MainWindow::fillInputCombo(QComboBox *combo, qint64 selectedId) const
{
    if (!combo)
        return;
    QSignalBlocker blocker(combo);
    combo->clear();
    bool found = false;
    for (const AtemInputInfo &input : m_inputs) {
        if (!isMappableInput(input))
            continue;
        const QString name = input.shortName.isEmpty() ? input.longName : input.shortName;
        QString label = QStringLiteral("%1 (%2)").arg(name).arg(input.id);
        if (!input.cameraControlSupported)
            label += QStringLiteral(" — без Camera Control");
        combo->addItem(label, QVariant::fromValue(input.id));
        if (input.id == selectedId)
            found = true;
    }
    if (!found && selectedId != 0)
        combo->addItem(QStringLiteral("id %1 (нет на пульте)").arg(selectedId),
                       QVariant::fromValue(selectedId));
    const int index = combo->findData(QVariant::fromValue(selectedId));
    if (index >= 0)
        combo->setCurrentIndex(index);
}

void MainWindow::addMapRow(const CameraBinding &binding)
{
    if (!m_mapTable)
        return;
    const int row = m_mapTable->rowCount();
    m_loading = true;
    m_mapTable->insertRow(row);
    m_mapTable->setItem(row, 0, new QTableWidgetItem(binding.label));
    m_loading = false;

    auto *combo = new QComboBox(m_mapTable);
    fillInputCombo(combo, binding.atemId);
    fitComboPopup(combo);
    m_mapTable->setCellWidget(row, 1, combo);
    connect(combo, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            &MainWindow::onMappingEdited);

    auto *spin = new QSpinBox(m_mapTable);
    const int count = multiviewWindowCount(currentLayout());
    spin->setRange(0, qMax(0, (count > 0 ? count : 16) - 1));
    spin->setValue(qBound(spin->minimum(), binding.multiviewWindow, spin->maximum()));
    m_mapTable->setCellWidget(row, 2, spin);
    connect(spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MainWindow::onMappingEdited);

    auto *participate = new QTableWidgetItem();
    participate->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
    participate->setCheckState(binding.includeInTuning ? Qt::Checked : Qt::Unchecked);
    participate->setToolTip(QStringLiteral("Только отмеченные камеры захватываются, рассчитываются и получают поправки"));
    m_loading = true;
    m_mapTable->setItem(row, 3, participate);
    m_loading = false;
}

CameraBinding MainWindow::bindingFromRow(int row) const
{
    CameraBinding binding;
    if (!m_mapTable || row < 0 || row >= m_mapTable->rowCount())
        return binding;
    if (const QTableWidgetItem *item = m_mapTable->item(row, 0))
        binding.label = item->text().trimmed();
    if (binding.label.isEmpty())
        binding.label = QStringLiteral("Камера %1").arg(row + 1);
    if (auto *combo = qobject_cast<QComboBox *>(m_mapTable->cellWidget(row, 1)))
        binding.atemId = combo->currentData().toLongLong();
    if (auto *spin = qobject_cast<QSpinBox *>(m_mapTable->cellWidget(row, 2)))
        binding.multiviewWindow = spin->value();
    if (const QTableWidgetItem *tune = m_mapTable->item(row, 3))
        binding.includeInTuning = tune->checkState() == Qt::Checked;
    return binding;
}

void MainWindow::saveMapping()
{
    // Before the saved mapping is loaded the table is empty: saving then would wipe the camera list
    // (and the «В подстройке» marks) on every start.
    if (m_loading || !m_mapTable || !m_mappingLoaded)
        return;
    QVector<CameraBinding> rows;
    rows.reserve(m_mapTable->rowCount());
    for (int row = 0; row < m_mapTable->rowCount(); ++row)
        rows.push_back(bindingFromRow(row));
    AtemAppSettings::setCameraMap(rows);
}

void MainWindow::loadMappingFromSettings()
{
    const QVector<CameraBinding> rows = AtemAppSettings::cameraMap();
    m_loading = true;
    if (m_mapTable)
        m_mapTable->setRowCount(0);
    m_loading = false;
    for (const CameraBinding &row : rows)
        addMapRow(row);
    m_mappingLoaded = true;
}

void MainWindow::seedMappingFromInputs()
{
    for (const AtemInputInfo &input : m_inputs) {
        if (!isMappableInput(input))
            continue;
        CameraBinding binding;
        binding.label = input.shortName.isEmpty() ? input.longName : input.shortName;
        if (binding.label.isEmpty())
            binding.label = QStringLiteral("Input %1").arg(input.id);
        binding.atemId = input.id;
        binding.multiviewWindow = 0;
        addMapRow(binding);
    }
    saveMapping();
}

void MainWindow::reloadMappingCombos()
{
    if (!m_mapTable)
        return;
    for (int row = 0; row < m_mapTable->rowCount(); ++row) {
        auto *combo = qobject_cast<QComboBox *>(m_mapTable->cellWidget(row, 1));
        if (!combo)
            continue;
        const qint64 selected = combo->currentData().toLongLong();
        fillInputCombo(combo, selected);
    }
}

void MainWindow::onAddCamera()
{
    CameraBinding binding;
    binding.label = QStringLiteral("Камера %1").arg(m_mapTable ? m_mapTable->rowCount() + 1 : 1);
    if (!m_inputs.isEmpty()) {
        for (const AtemInputInfo &input : m_inputs) {
            if (isMappableInput(input)) {
                binding.atemId = input.id;
                break;
            }
        }
    }
    addMapRow(binding);
    saveMapping();
    syncCamerasFromTable();
}

void MainWindow::onRemoveCamera()
{
    if (!m_mapTable)
        return;
    const int row = m_mapTable->currentRow();
    if (row < 0)
        return;
    m_loading = true;
    m_mapTable->removeRow(row);
    m_loading = false;
    saveMapping();
    syncCamerasFromTable();
}

void MainWindow::onMappingEdited()
{
    if (m_loading)
        return;
    saveMapping();
    syncCamerasFromTable();
}

void MainWindow::syncCamerasFromTable()
{
    const QVector<CameraRow> previous = m_cameras;
    const qint64 previousRef = (m_referenceIndex >= 0 && m_referenceIndex < previous.size())
                                   ? previous.at(m_referenceIndex).binding.atemId
                                   : 0;
    m_cameras.clear();
    const int rows = m_mapTable ? m_mapTable->rowCount() : 0;
    for (int row = 0; row < rows; ++row) {
        CameraRow camera;
        camera.binding = bindingFromRow(row);
        QTableWidgetItem *participate = m_mapTable->item(row, 3);
        if (participate)
            camera.includeInTuning = participate->checkState() == Qt::Checked;
        for (const CameraRow &old : previous) {
            if (old.binding.atemId == camera.binding.atemId) {
                const CameraBinding binding = camera.binding;
                const bool include = participate ? camera.includeInTuning : old.includeInTuning;
                camera = old;
                camera.binding = binding;
                camera.includeInTuning = include;
                break;
            }
        }
        m_cameras.push_back(camera);
    }
    // Rows get their real ATEM ids only after the switcher's inputs are known; the saved
    // sample regions / chart corners (keyed by ATEM id) are re-applied on every sync.
    loadCameraRois();
    applyOverlayForMarkCamera();

    if (!m_referenceCombo)
        return;
    QSignalBlocker blocker(m_referenceCombo);
    m_referenceCombo->clear();
    int nextRef = 0;
    for (int i = 0; i < m_cameras.size(); ++i) {
        m_referenceCombo->addItem(m_cameras.at(i).binding.label, m_cameras.at(i).binding.atemId);
        if (previousRef != 0 && m_cameras.at(i).binding.atemId == previousRef)
            nextRef = i;
    }
    if (m_cameras.isEmpty()) {
        m_referenceIndex = 0;
        return;
    }
    m_referenceCombo->setCurrentIndex(nextRef);
    m_referenceIndex = nextRef;

    if (m_markCameraCombo) {
        QSignalBlocker markBlock(m_markCameraCombo);
        const int markIndex = m_markCameraCombo->currentIndex();
        m_markCameraCombo->clear();
        for (const CameraRow &camera : m_cameras)
            m_markCameraCombo->addItem(camera.binding.label, camera.binding.atemId);
        if (markIndex >= 0 && markIndex < m_markCameraCombo->count())
            m_markCameraCombo->setCurrentIndex(markIndex);
        else if (m_markCameraCombo->count() > 0)
            m_markCameraCombo->setCurrentIndex(0);
        applyOverlayForMarkCamera();
    }
}

void MainWindow::onReferenceChanged(int index)
{
    m_referenceIndex = qBound(0, index, qMax(0, m_cameras.size() - 1));
}

void MainWindow::onRefreshDevices()
{
#ifndef HAVE_GSTREAMER
    if (m_videoStatus)
        m_videoStatus->setText(QStringLiteral("GStreamer не собран"));
    return;
#else
    if (m_closing.load() || (m_deviceScan && m_deviceScan->isRunning()))
        return;
    if (m_deviceScan) {
        m_deviceScan->deleteLater();
        m_deviceScan = nullptr;
    }
    m_deviceScan = new DeviceScanThread();
    connect(m_deviceScan, &QThread::finished, this, &MainWindow::onDevicesReady);
    if (m_videoStatus)
        m_videoStatus->setText(QStringLiteral("Поиск видеоустройств…"));
    m_deviceScan->start();
#endif
}

void MainWindow::onDevicesReady()
{
#ifdef HAVE_GSTREAMER
    if (m_closing.load() || !m_deviceScan)
        return;
    m_devices = m_deviceScan->devices;
    const QString error = m_deviceScan->error;
    if (!error.isEmpty() && m_videoStatus)
        m_videoStatus->setText(error);
    else if (m_devices.isEmpty() && m_videoStatus)
        m_videoStatus->setText(QStringLiteral("Видеоустройства не найдены"));
    applyDeviceList();
#endif
}

void MainWindow::applyDeviceList()
{
    if (!m_deviceCombo)
        return;
    QSignalBlocker blocker(m_deviceCombo);
    m_deviceCombo->clear();
    for (const VideoDevice &device : m_devices)
        m_deviceCombo->addItem(device.displayName, device.id);
    fitComboPopup(m_deviceCombo);

    const QString saved = AtemAppSettings::videoDeviceId();
    const int found = saved.isEmpty() ? -1 : m_deviceCombo->findData(saved);
    m_deviceCombo->setCurrentIndex(found);
    blocker.unblock();
    if (found < 0) {
        if (m_inputCombo)
            m_inputCombo->clear();
        if (m_modeCombo)
            m_modeCombo->clear();
        if (!saved.isEmpty() && m_videoStatus) {
            m_videoStatus->setText(QStringLiteral(
                "Сохранённое устройство не найдено. Другое устройство не выбрано."));
        } else if (m_videoStatus && !m_devices.isEmpty()) {
            m_videoStatus->setText(QStringLiteral("Выберите устройство захвата"));
        }
#ifdef HAVE_GSTREAMER
        if (m_preview)
            m_preview->stopPreview();
#endif
        m_previewRunning = false;
        return;
    }
    populateDeviceOptions();
}

void MainWindow::onDeviceChanged(int index)
{
    if (m_loading || index < 0)
        return;
    populateDeviceOptions();
}

void MainWindow::populateDeviceOptions()
{
    if (!m_deviceCombo)
        return;
    const int index = m_deviceCombo->currentIndex();
    if (index < 0 || index >= m_devices.size())
        return;
    const VideoDevice &device = m_devices.at(index);
    const bool sameDevice = device.id == AtemAppSettings::videoDeviceId();
    const QString savedInput = sameDevice ? AtemAppSettings::videoInputKey() : QString();
    const QString savedMode = sameDevice ? AtemAppSettings::videoModeKey() : QString();

    if (m_inputCombo) {
        QSignalBlocker blocker(m_inputCombo);
        m_inputCombo->clear();
        for (const VideoInputOption &input : device.inputs)
            m_inputCombo->addItem(input.label, input.key);
        m_inputCombo->setEnabled(!device.inputs.isEmpty());
        fitComboPopup(m_inputCombo);
        if (!selectComboKey(m_inputCombo, savedInput, !sameDevice || savedInput.isEmpty())) {
            if (m_videoStatus) {
                m_videoStatus->setText(QStringLiteral(
                    "Сохранённый вход устройства недоступен. Другой вход не выбран."));
            }
#ifdef HAVE_GSTREAMER
            if (m_preview)
                m_preview->stopPreview();
#endif
            return;
        }
    }
    if (m_modeCombo) {
        QSignalBlocker blocker(m_modeCombo);
        m_modeCombo->clear();
        for (const VideoModeOption &mode : device.modes)
            m_modeCombo->addItem(mode.label, mode.key);
        m_modeCombo->setEnabled(!device.modes.isEmpty());
        fitComboPopup(m_modeCombo);
        if (!selectComboKey(m_modeCombo, savedMode, !sameDevice || savedMode.isEmpty())) {
            if (m_videoStatus) {
                m_videoStatus->setText(QStringLiteral(
                    "Сохранённый режим недоступен. Другой режим не выбран."));
            }
#ifdef HAVE_GSTREAMER
            if (m_preview)
                m_preview->stopPreview();
#endif
            return;
        }
    }
    startPreview();
}

void MainWindow::onVideoOptionChanged()
{
    if (m_loading)
        return;
    startPreview();
}

void MainWindow::startPreview()
{
#ifdef HAVE_GSTREAMER
    if (!m_preview || !m_deviceCombo)
        return;
    const int index = m_deviceCombo->currentIndex();
    if (index < 0 || index >= m_devices.size()) {
        m_preview->stopPreview();
        m_previewRunning = false;
        return;
    }
    const VideoDevice &device = m_devices.at(index);
    const QString inputKey = m_inputCombo ? m_inputCombo->currentData().toString() : QString();
    const QString modeKey = m_modeCombo ? m_modeCombo->currentData().toString() : QString();
    if (!device.inputs.isEmpty() && m_inputCombo && m_inputCombo->currentIndex() < 0) {
        m_preview->stopPreview();
        m_previewRunning = false;
        if (m_videoStatus)
            m_videoStatus->setText(QStringLiteral("Выберите вход устройства"));
        return;
    }
    if (!device.modes.isEmpty() && m_modeCombo && m_modeCombo->currentIndex() < 0) {
        m_preview->stopPreview();
        m_previewRunning = false;
        if (m_videoStatus)
            m_videoStatus->setText(QStringLiteral("Выберите режим устройства"));
        return;
    }
    AtemAppSettings::setVideoDeviceId(device.id);
    AtemAppSettings::setVideoInputKey(inputKey);
    AtemAppSettings::setVideoModeKey(modeKey);
    if (m_videoStatus)
        m_videoStatus->setText(QStringLiteral("Запуск %1…").arg(device.displayName));
    m_fpsStamp = 0;
    m_previewFrames = 0;
    m_preview->startDevice(device.id, inputKey, modeKey);
#else
    if (m_videoStatus)
        m_videoStatus->setText(QStringLiteral("GStreamer не собран"));
#endif
}

void MainWindow::onPreviewFrame(const QImage &image)
{
    if (m_closing.load() || !m_previewOverlay)
        return;
    m_previewRunning = true;
    m_lastPreviewFrameMs = QDateTime::currentMSecsSinceEpoch();
    m_previewRetry = 0;
    m_previewRetryAtMs = 0;
    if (m_previewStalled) {
        m_previewStalled = false;
        statusBar()->showMessage(QStringLiteral("Сигнал на карте захвата восстановлен"), 5000);
    }
    m_lastPreviewFrame = image;
    m_previewOverlay->setPreviewImage(image);
    if (m_scopeDocks) {
        QImage compare = m_referenceFrameImage;
        if (compare.isNull() && m_cameras.size() > 1 && m_referenceIndex >= 0
            && m_referenceIndex < m_cameras.size())
            compare = m_cameras.at(m_referenceIndex).lastFrame;
        m_scopeDocks->submitFrame(image, compare);
    }
    const ScopeQcResult qc = ScopeQc::analyzeFrame(image, AtemAppSettings::qcIreLow(),
                                                   AtemAppSettings::qcIreHigh());
    if (m_statusQc) {
        m_statusQc->setTextFormat(Qt::RichText);
        const QString qcText =
            QStringLiteral("QC: %1–%2 IRE | вне допуска %3% | пересвет %4")
                .arg(qc.minIre, 0, 'f', 1)
                .arg(qc.maxIre, 0, 'f', 1)
                .arg(qc.outOfTolerancePercent(), 0, 'f', 2)
                .arg(qc.highlightClipped);
        m_statusQc->setText(
            qc.hasIssues(AtemAppSettings::qcIreLow(), AtemAppSettings::qcIreHigh())
                ? QStringLiteral("<span style='color:#ff7070'>%1</span>").arg(qcText)
                : qcText);
    }
    updateStreamStatusLabel();

    ++m_previewFrames;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_fpsStamp == 0)
        m_fpsStamp = now;
    if (now - m_fpsStamp >= 1000 && m_fpsLabel) {
        const double fps = m_previewFrames * 1000.0 / static_cast<double>(now - m_fpsStamp);
        m_fpsLabel->setText(QStringLiteral("Превью %1 кадр/с").arg(fps, 0, 'f', 1));
        m_previewFrames = 0;
        m_fpsStamp = now;
    }
}

void MainWindow::onPreviewAnalysis(const FrameColorAnalysis &analysis)
{
    Q_UNUSED(analysis);
}

void MainWindow::onPreviewError(const QString &message)
{
    m_previewRunning = false;
    m_previewStalled = false;
    if (m_previewOverlay)
        m_previewOverlay->setPreviewImage(QImage());
    // Automatic restart (card busy in another app, cable re-plugged, signal back): 2, 4 … 10 s.
    const int delayMs = qMin(10000, 2000 * ++m_previewRetry);
    m_previewRetryAtMs = m_closing.load() ? 0 : QDateTime::currentMSecsSinceEpoch() + delayMs;
    qCWarning(lcRoute).noquote() << "preview error:" << message << "- retry in" << delayMs << "ms";
    QString hint;
    const QString device = m_deviceCombo ? m_deviceCombo->currentText() : QString();
    if (device.contains(QLatin1String("decklink"), Qt::CaseInsensitive)
        && (message.contains(QLatin1String("Internal data stream error"), Qt::CaseInsensitive)
            || message.contains(QLatin1String("negotiat"), Qt::CaseInsensitive))) {
        // DeckLink does not adapt to the input: frames arrive only in the exact input format.
        hint = QStringLiteral("\nВыбранный режим «%1» не совпадает с сигналом на входе карты. "
                              "Выберите режим как у источника (у ATEM обычно 1920×1080 @ 25 или 50 кадр/с).")
                   .arg(m_modeCombo ? m_modeCombo->currentText() : QString());
    }
    if (m_videoStatus)
        m_videoStatus->setText(QStringLiteral("%1 — повторный запуск через %2 с%3").arg(message).arg(delayMs / 1000).arg(hint));
    if (m_fpsLabel)
        m_fpsLabel->setText(QStringLiteral("Превью: нет кадров"));
}

void MainWindow::setBusy(bool busy)
{
    m_busy = busy;
    if (m_btnCapture)
        m_btnCapture->setEnabled(!busy);
    if (m_btnRollback)
        m_btnRollback->setEnabled(!busy);
    if (m_btnLoop)
        m_btnLoop->setEnabled(!busy && m_loopPass <= 0);
    if (m_btnLoopUntil)
        m_btnLoopUntil->setEnabled(!busy || m_loopPass > 0); // stays clickable as «Стоп»
    if (m_btnConnect)
        m_btnConnect->setEnabled(!busy);
    if (m_mapTable)
        m_mapTable->setEnabled(!busy);
    if (m_deviceCombo)
        m_deviceCombo->setEnabled(!busy);
    if (m_routeCombo)
        m_routeCombo->setEnabled(!busy);
    updateRouteWidgets();
    rebuildAtemAvailabilityUi();
}

void MainWindow::setCaptureStatus(const QString &text)
{
    if (m_closing.load())
        return;
    if (m_videoStatus)
        m_videoStatus->setText(text);
}

QString MainWindow::callOnGuiThread(const std::function<QString()> &fn)
{
    if (QThread::currentThread() == thread())
        return fn();
    QString result;
    const bool invoked = QMetaObject::invokeMethod(
        this, [&]() { result = fn(); }, Qt::BlockingQueuedConnection);
    if (!invoked)
        return QStringLiteral("Не удалось выполнить команду ATEM в потоке интерфейса");
    return result;
}

bool MainWindow::confirmProgramSwitch()
{
    QMessageBox box(this);
    box.setIcon(QMessageBox::Warning);
    box.setWindowTitle(QStringLiteral("Переключение в эфире"));
    box.setText(QStringLiteral(
        "Режим PGM меняет программный выход пульта. Каждая камера на короткое время появится "
        "в эфире, затем вернётся прежний источник."));
    box.setInformativeText(QStringLiteral("Без подтверждения ничего не переключается."));
    auto *go = box.addButton(QStringLiteral("Переключить в эфире"), QMessageBox::AcceptRole);
    auto *cancel = box.addButton(QStringLiteral("Отмена"), QMessageBox::RejectRole);
    box.setDefaultButton(cancel);
    box.exec();
    return box.clickedButton() == go;
}

void MainWindow::onCaptureAnalyze()
{
    if (m_busy)
        return;
    if (!m_client || !m_client->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("Захват"),
                             QStringLiteral("Нет подключения к ATEM. Проверьте IP пульта и нажмите «Подключить»."));
        return;
    }
    syncCamerasFromTable();
    if (m_cameras.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Захват"),
                             QStringLiteral("Добавьте хотя бы одну камеру в таблицу связки."));
        return;
    }
    const CaptureRouteKind kind = captureRouteKind(currentRoute());
    if (kind == CaptureRouteKind::Invalid) {
        QMessageBox::warning(this, QStringLiteral("Захват"),
                             QStringLiteral("Выберите, что приходит на карту захвата."));
        return;
    }
    if (kind == CaptureRouteKind::Aux && currentAuxId() == 0) {
        QMessageBox::warning(this, QStringLiteral("Захват"),
                             QStringLiteral("Выберите выход AUX. Другой выход не будет выбран сам."));
        return;
    }
    if (kind == CaptureRouteKind::Multiview && currentLayout().isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Захват"),
                             QStringLiteral("Выберите раскладку мультивью."));
        return;
    }

    bool pgmAllowed = false;
    if (kind == CaptureRouteKind::Program) {
        if (!confirmProgramSwitch()) {
            qCInfo(lcRoute) << "PGM switch cancelled; program bus was not changed";
            return;
        }
        pgmAllowed = true;
    }

#ifndef HAVE_GSTREAMER
    QMessageBox::warning(this, QStringLiteral("Захват"), QStringLiteral("GStreamer не собран"));
    return;
#else
    if (!m_preview || !m_preview->isRunning()) {
        QMessageBox::warning(this, QStringLiteral("Захват"),
                             QStringLiteral("Нет живого сигнала с выбранного устройства."));
        return;
    }

    auto *job = new CaptureJob();
    job->window = this;
    job->request.route = currentRoute();
    job->request.auxId = currentAuxId();
    job->request.layout = currentLayout();
    job->request.pgmAllowed = pgmAllowed;
    job->request.referenceIndex = m_referenceIndex;
    for (const CameraRow &camera : m_cameras)
        job->request.cameras.push_back(camera.binding);
    job->request.active = captureActiveMask();
    if (!job->request.active.contains(true)) {
        delete job;
        QMessageBox::warning(this, QStringLiteral("Захват"),
                             QStringLiteral("Отметьте камеры в столбце «В подстройке»."));
        return;
    }
    m_captureJob = job;
    connect(job, &QThread::finished, this, [this, job] { finishCapture(job); });
    setBusy(true);
    setCaptureStatus(QStringLiteral("Захват…"));
    job->start();
#endif
}

void MainWindow::executeCapture(const CaptureRequest &request, const std::atomic<bool> *cancel,
                                CaptureOutcome *outcome)
{
    if (!outcome)
        return;
    outcome->analysis.resize(request.cameras.size());
    outcome->errors.resize(request.cameras.size());

    const CaptureRouteKind kind = captureRouteKind(request.route);
    const auto cancelled = [&] {
        return (cancel && cancel->load()) || m_closing.load();
    };
    const auto status = [&](const QString &text) {
        QMetaObject::invokeMethod(this, [this, text] { setCaptureStatus(text); }, Qt::QueuedConnection);
    };

#ifndef HAVE_GSTREAMER
    Q_UNUSED(request);
    Q_UNUSED(cancel);
    outcome->fatal = QStringLiteral("GStreamer не собран");
    return;
#else
    GstLivePreview *preview = m_preview;
    if (!preview || !preview->isRunning()) {
        outcome->fatal = QStringLiteral("Нет живого сигнала с выбранного устройства.");
        return;
    }

    if (kind == CaptureRouteKind::Program && !maySwitchProgram(kind, request.pgmAllowed)) {
        outcome->fatal = QStringLiteral(
            "Режим PGM: без подтверждения программный выход не переключается.");
        qCInfo(lcRoute) << "PGM capture refused without confirmation";
        return;
    }
    if (kind == CaptureRouteKind::Invalid) {
        outcome->fatal = QStringLiteral("Режим захвата не выбран.");
        return;
    }

    const auto isActive = [&](int i) { return request.active.isEmpty() || request.active.value(i, false); };
    const int activeTotal = request.active.isEmpty() ? static_cast<int>(request.cameras.size())
                                                     : static_cast<int>(request.active.count(true));
    int activeDone = 0;
    qCInfo(lcRoute).noquote() << QStringLiteral("capture route=%1 cameras=%2 aux=%3 layout=%4")
                                     .arg(request.route)
                                     .arg(activeTotal)
                                     .arg(request.auxId)
                                     .arg(request.layout);

    const auto grabAfterRouteChange = [&](QImage &image) -> QString {
        const quint64 base = preview->frameGeneration();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        if (cancelled())
            return QStringLiteral("Захват прерван");
        if (!preview->waitForNewerFrame(base + 3, 1500, image, nullptr, cancel)) {
            return QStringLiteral(
                "Нет нового кадра с карты захвата после переключения. Другой режим не используется.");
        }
        return {};
    };

    if (kind == CaptureRouteKind::Multiview) {
        QImage frame;
        if (!preview->copyLatest(frame)) {
            if (!preview->waitForNewerFrame(0, 1500, frame, nullptr, cancel)) {
                outcome->fatal = QStringLiteral("Нет кадра мультивью с карты захвата.");
                return;
            }
        }
        outcome->analysis.resize(request.cameras.size());
        outcome->images.resize(request.cameras.size());
        outcome->errors.resize(request.cameras.size());
        for (int i = 0; i < request.cameras.size(); ++i) {
            if (cancelled()) {
                outcome->fatal = QStringLiteral("Захват прерван");
                return;
            }
            if (!isActive(i))
                continue;
            const CameraBinding &camera = request.cameras.at(i);
            status(QStringLiteral("Multiview: %1, окно %2")
                       .arg(camera.label)
                       .arg(camera.multiviewWindow));
            const QRect rect = multiviewPixelRect(request.layout, camera.multiviewWindow, frame.width(),
                                                  frame.height());
            if (rect.isEmpty()) {
                outcome->errors[i] = QStringLiteral("%1: окно %2 не входит в раскладку %3")
                                         .arg(camera.label)
                                         .arg(camera.multiviewWindow)
                                         .arg(request.layout);
                continue;
            }
            outcome->analysis[i] = averageLiveAnalysis(rect, cancel);
            outcome->images[i] = frame.copy(rect);
        }
        return;
    }

    if (maySwitchAux(kind)) {
        outcome->images.resize(request.cameras.size());
        // Read the AUX source ONCE and restore it ONCE at the end: the SDK may still report the
        // previous camera right after a switch, so per-camera read/restore drifts (seen live).
        qint64 previous = 0;
        const QString readError = callOnGuiThread([&] {
            return m_client ? m_client->auxSource(request.auxId, &previous) : QStringLiteral("Нет клиента ATEM");
        });
        if (!readError.isEmpty()) {
            outcome->fatal = QStringLiteral("Не удалось прочитать источник AUX: %1").arg(readError);
            return;
        }
        bool armed = false;
        const auto restore = [&] {
            if (!armed)
                return;
            armed = false;
            const QString restoreError = callOnGuiThread([&] {
                return m_client ? m_client->setAuxSource(request.auxId, previous) : QStringLiteral("Нет клиента ATEM");
            });
            if (restoreError.isEmpty())
                qCInfo(lcRoute).noquote() << QStringLiteral("AUX %1 restored to %2").arg(request.auxId).arg(previous);
            else
                qCWarning(lcRoute).noquote()
                    << QStringLiteral("AUX %1 restore failed: %2").arg(request.auxId).arg(restoreError);
        };
        struct ScopeExit
        {
            const decltype(restore) &fn;
            ~ScopeExit() { fn(); }
        } guard{restore};

        for (int i = 0; i < request.cameras.size(); ++i) {
            if (cancelled()) {
                outcome->fatal = QStringLiteral("Захват прерван");
                return;
            }
            if (!isActive(i))
                continue; // not «В подстройке»: the AUX is not switched to it at all
            const CameraBinding &camera = request.cameras.at(i);
            status(QStringLiteral("AUX: %1 (%2/%3)").arg(camera.label).arg(++activeDone).arg(activeTotal));
            const QString setError = callOnGuiThread([&] {
                return m_client ? m_client->setAuxSource(request.auxId, camera.atemId)
                                : QStringLiteral("Нет клиента ATEM");
            });
            if (!setError.isEmpty()) {
                outcome->errors[i] = QStringLiteral("%1: %2").arg(camera.label, setError);
                continue;
            }
            armed = true;
            qCInfo(lcRoute).noquote() << QStringLiteral("AUX %1 -> %2 (%3)")
                                             .arg(request.auxId)
                                             .arg(camera.atemId)
                                             .arg(camera.label);
            QImage image;
            const QString grabError = grabAfterRouteChange(image);
            if (!grabError.isEmpty())
                outcome->errors[i] = QStringLiteral("%1: %2").arg(camera.label, grabError);
            else {
                outcome->analysis[i] = averageLiveAnalysis(QRect(), cancel);
                if (preview->copyLatest(image))
                    outcome->images[i] = image;
            }
        }
        return;
    }

    if (maySwitchProgram(kind, request.pgmAllowed)) {
        outcome->images.resize(request.cameras.size());
        for (int i = 0; i < request.cameras.size(); ++i) {
            if (cancelled()) {
                outcome->fatal = QStringLiteral("Захват прерван");
                return;
            }
            if (!isActive(i))
                continue;
            const CameraBinding &camera = request.cameras.at(i);
            status(QStringLiteral("PGM: %1").arg(camera.label));
            qint64 previous = 0;
            const QString readError = callOnGuiThread([&] {
                return m_client ? m_client->programInput(&previous) : QStringLiteral("Нет клиента ATEM");
            });
            if (!readError.isEmpty()) {
                outcome->errors[i] = QStringLiteral("%1: %2").arg(camera.label, readError);
                continue;
            }

            bool armed = false;
            const auto restore = [&] {
                if (!armed)
                    return;
                armed = false;
                const QString restoreError = callOnGuiThread([&] {
                    return m_client ? m_client->setProgramInput(previous) : QStringLiteral("Нет клиента ATEM");
                });
                if (restoreError.isEmpty())
                    qCInfo(lcRoute).noquote() << QStringLiteral("PGM restored to %1").arg(previous);
                else
                    qCWarning(lcRoute).noquote() << QStringLiteral("PGM restore failed: %1").arg(restoreError);
            };
            struct ScopeExit
            {
                const decltype(restore) &fn;
                ~ScopeExit() { fn(); }
            } guard{restore};

            const QString setError = callOnGuiThread([&] {
                return m_client ? m_client->setProgramInput(camera.atemId) : QStringLiteral("Нет клиента ATEM");
            });
            if (!setError.isEmpty()) {
                outcome->errors[i] = QStringLiteral("%1: %2").arg(camera.label, setError);
                continue;
            }
            armed = true;
            qCInfo(lcRoute).noquote()
                << QStringLiteral("PGM: %1 -> %2").arg(previous).arg(camera.atemId);

            QImage image;
            const QString grabError = grabAfterRouteChange(image);
            if (!grabError.isEmpty())
                outcome->errors[i] = QStringLiteral("%1: %2").arg(camera.label, grabError);
            else {
                outcome->analysis[i] = averageLiveAnalysis(QRect(), cancel);
                if (preview->copyLatest(image))
                    outcome->images[i] = image;
            }
        }
        return;
    }

    outcome->fatal = QStringLiteral("Этот режим захвата не выполняет переключений другого типа.");
#endif
}

void MainWindow::finishCapture(CaptureJob *job)
{
    if (m_captureJob == job)
        m_captureJob = nullptr;
    CaptureOutcome outcome;
    if (job)
        outcome = job->outcome;
    if (m_closing.load()) {
        delete job;
        return;
    }
    if (job)
        job->deleteLater();

    setBusy(false);
    if (!outcome.fatal.isEmpty()) {
        if (m_loopPass > 0)
            finishAutoMatchLoop(outcome.fatal);
        QMessageBox::warning(this, QStringLiteral("Захват"), outcome.fatal);
        return;
    }

    const QVector<bool> active = job ? job->request.active : QVector<bool>();
    for (int i = 0; i < m_cameras.size(); ++i) {
        if (!active.isEmpty() && !active.value(i, false)) {
            // not captured this time: stale frames must not produce proposals
            m_cameras[i].analysis = FrameColorAnalysis();
            m_cameras[i].lastFrame = QImage();
        }
    }
    for (int i = 0; i < outcome.analysis.size() && i < m_cameras.size(); ++i) {
        if (!outcome.errors.value(i).isEmpty())
            continue;
        if (outcome.analysis.at(i).sampleCount == 0)
            continue;
        m_cameras[i].analysis = outcome.analysis.at(i);
        if (i < outcome.images.size() && !outcome.images.at(i).isNull())
            m_cameras[i].lastFrame = outcome.images.at(i);
    }

    QStringList problems;
    for (const QString &error : outcome.errors) {
        if (!error.isEmpty())
            problems << error;
    }

    readCurrentCameraValues();
    runMatchingAfterCapture(problems);
}

void MainWindow::onApply()
{
    if (m_busy)
        return;
    if (!AtemSetup::isLiveAtemControlAvailable()) {
        QMessageBox::warning(this, QString(), QStringLiteral("Настройте путь к ATEM Software Control"));
        return;
    }
    if (!m_client || !m_client->isConnected()) {
        QMessageBox::warning(this, QString(), QStringLiteral("Нет подключения к ATEM"));
        return;
    }
    syncCamerasFromTable();
    const bool camAuto = m_tuningMode == ColorMatchMode::CameraAuto;

    QVector<int> candidates;
    for (int i = 0; i < m_cameras.size(); ++i)
        if ((camAuto && m_cameras.at(i).includeInTuning) || (!camAuto && m_cameras.at(i).hasProposal))
            candidates << i;
    if (candidates.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Поправки"),
                                 QStringLiteral("Нет рассчитанных поправок. Сначала «Захват и расчёт»."));
        return;
    }

    // Compact confirmation with camera selection; suspicious values are highlighted.
    QDialog dlg(this);
    dlg.setWindowTitle(camAuto ? QStringLiteral("Автобаланс камер") : QStringLiteral("Применить поправки"));
    auto *lay = new QVBoxLayout(&dlg);
    auto *list = new QListWidget(&dlg);
    bool anyWarning = false;
    for (int i : candidates) {
        const CameraRow &c = m_cameras.at(i);
        QString text = c.binding.label;
        if (!camAuto) {
            text += QStringLiteral(":  ") + summarizeChange(c.current, c.proposed).replace(QLatin1Char('\n'), QStringLiteral(", "));
            if (!c.warnings.isEmpty()) {
                text += QStringLiteral("\n   ⚠ ") + c.warnings.join(QStringLiteral("; "));
                anyWarning = true;
            }
        } else {
            text += m_chkAutoIris && m_chkAutoIris->isChecked() ? QStringLiteral(": авто ББ + авто диафрагма")
                                                                : QStringLiteral(": авто ББ");
        }
        auto *item = new QListWidgetItem(text, list);
        item->setFlags(item->flags() | Qt::ItemIsUserCheckable);
        item->setCheckState(Qt::Checked);
        item->setData(Qt::UserRole, i);
        if (!camAuto && !c.warnings.isEmpty())
            item->setForeground(QColor(255, 140, 110));
    }
    lay->addWidget(new QLabel(anyWarning ? QStringLiteral("⚠ Некоторые значения выглядят слишком большими — проверьте "
                                                          "кадр/разметку. Снимите галочки, чтобы не применять.")
                                         : (camAuto ? QStringLiteral("Отмеченные камеры сами подберут баланс белого. "
                                                                     "«Откат» вернёт прежние значения.")
                                                    : QStringLiteral("Отмеченные камеры получат новые значения (откат доступен).")),
                              &dlg));
    lay->addWidget(list, 1);
    auto *btns = new QDialogButtonBox(&dlg);
    QPushButton *go = btns->addButton(QStringLiteral("Применить"), QDialogButtonBox::AcceptRole);
    btns->addButton(QStringLiteral("Отмена"), QDialogButtonBox::RejectRole);
    go->setDefault(!anyWarning);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    lay->addWidget(btns);
    dlg.resize(760, 120 + 44 * candidates.size());
    if (dlg.exec() != QDialog::Accepted) {
        qCInfo(lcRoute) << "apply cancelled, nothing sent";
        return;
    }
    QVector<int> chosen;
    bool chosenWarn = false;
    for (int r = 0; r < list->count(); ++r) {
        if (list->item(r)->checkState() != Qt::Checked)
            continue;
        const int idx = list->item(r)->data(Qt::UserRole).toInt();
        chosen << idx;
        if (!camAuto && !m_cameras.at(idx).warnings.isEmpty())
            chosenWarn = true;
    }
    if (chosen.isEmpty())
        return;
    if (chosenWarn
        && QMessageBox::warning(this, QStringLiteral("Подозрительные значения"),
                                QStringLiteral("Среди выбранных камер есть очень большие поправки. Всё равно применить?"),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
               != QMessageBox::Yes)
        return;

    QString errors;
    const int sent = sendProposals(chosen, &errors);
    refreshCorrectionsTable();
    if (!errors.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Частичные ошибки"), errors);
    setCaptureStatus(QStringLiteral("Отправлено на %1 камер(ы). Для уточнения повторите «Захват и расчёт».").arg(sent));
    statusBar()->showMessage(QStringLiteral("Поправки отправлены: %1").arg(sent), 8000);
}

int MainWindow::sendProposals(const QVector<int> &chosen, QString *errorsOut)
{
    const bool camAuto = m_tuningMode == ColorMatchMode::CameraAuto;
    QString errors;
    int sent = 0;
    for (int index : chosen) {
        if (index < 0 || index >= m_cameras.size())
            continue;
        CameraRow &camera = m_cameras[index];
        if (camAuto) {
            // Keep the values before auto WB so "Откат" can restore them.
            CameraCorrectionBundle before;
            if (!camera.hasRollback && m_client->readCorrectionBundle(camera.binding.atemId, before).isEmpty()) {
                camera.rollback = before;
                camera.hasRollback = true;
            }
            QString e = m_client->triggerCameraAction(camera.binding.atemId, BmdCameraProtocol::kCategoryVideo,
                                                      BmdCameraProtocol::kVideoAutoWhiteBalance);
            if (e.isEmpty() && m_chkAutoIris && m_chkAutoIris->isChecked())
                e = m_client->triggerCameraAction(camera.binding.atemId, BmdCameraProtocol::kCategoryLens,
                                                  BmdCameraProtocol::kLensAutoAperture);
            qCInfo(lcRoute).noquote() << QStringLiteral("camera auto dest=%1 %2")
                                             .arg(camera.binding.atemId)
                                             .arg(e.isEmpty() ? QStringLiteral("ok") : e);
            if (!e.isEmpty())
                errors += QStringLiteral("%1: %2\n").arg(camera.binding.label, e);
            else
                ++sent;
            continue;
        }
        CameraCorrectionBundle previous;
        const QString error = m_client->applyCorrectionBundle(camera.binding.atemId, camera.proposed, &previous,
                                                              nullptr, colorMatchModeLogName(m_tuningMode));
        if (!error.isEmpty()) {
            errors += QStringLiteral("%1: %2\n").arg(camera.binding.label, error);
        } else {
            // «Откат» returns to the values before the FIRST apply (not just the last pass).
            if (!camera.hasRollback) {
                camera.rollback = previous;
                camera.hasRollback = true;
                qCInfo(lcRoute).noquote() << QStringLiteral("rollback point %1 %2")
                                                 .arg(camera.binding.label,
                                                      QString::fromUtf8(QJsonDocument(previous.toJson())
                                                                            .toJson(QJsonDocument::Compact)));
            }
            if (camera.proposed.irisFStopAv >= 0.0f && camera.current.irisFStopAv >= 0.0f
                && std::fabs(camera.proposed.irisFStopAv - camera.current.irisFStopAv) >= 0.25f
                && camera.lastMeanLuma > 0.0) {
                camera.irisProbePending = true;
                camera.irisProbeExpectStops = camera.current.irisFStopAv - camera.proposed.irisFStopAv;
                camera.irisProbeMeanBefore = camera.lastMeanLuma;
                camera.irisProbeAvBefore = camera.current.irisFStopAv;
                camera.irisProbeDbBefore = camera.current.gainDb;
                camera.irisProbeGainYBefore = std::max(1e-3f, camera.current.gainY);
            }
            camera.hasProposal = false; // applied: a new capture is needed for the next pass
            ++sent;
        }
    }
    if (errorsOut)
        *errorsOut = errors;
    return sent;
}

QVector<bool> MainWindow::captureActiveMask() const
{
    QVector<bool> mask(m_cameras.size(), false);
    for (int i = 0; i < m_cameras.size(); ++i)
        mask[i] = m_cameras.at(i).includeInTuning;
    if (modeNeedsReferenceCamera() && m_referenceIndex >= 0 && m_referenceIndex < mask.size())
        mask[m_referenceIndex] = true;
    return mask;
}

bool MainWindow::unlimitedMode() const
{
    return m_chkUnlimited && m_chkUnlimited->isChecked();
}

bool MainWindow::modeNeedsReferenceCamera() const
{
    if (m_tuningMode == ColorMatchMode::ReferenceCamera)
        return true;
    if (m_tuningMode == ColorMatchMode::CommonSample)
        return !m_sampleTargetCombo || m_sampleTargetCombo->currentData().toString() == QLatin1String("reference");
    return false;
}

void MainWindow::fitComboPopup(QComboBox *combo)
{
    if (!combo)
        return;
    // The popup list must show full item names (e.g. «1920×1080 @ 25.00 кадр/с (UYVY)»), even when
    // the combo itself is narrow.
    const QFontMetrics fm(combo->view() ? combo->view()->font() : combo->font());
    int widest = 0;
    for (int i = 0; i < combo->count(); ++i)
        widest = std::max(widest, fm.horizontalAdvance(combo->itemText(i)));
    if (combo->view())
        combo->view()->setMinimumWidth(widest + 48);
    for (int i = 0; i < combo->count(); ++i)
        if (combo->itemData(i, Qt::ToolTipRole).toString().isEmpty())
            combo->setItemData(i, combo->itemText(i), Qt::ToolTipRole);
    combo->setToolTip(combo->currentText());
    combo->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    combo->setMinimumContentsLength(std::max(combo->minimumContentsLength(), 8));
    QObject::connect(combo, &QComboBox::currentTextChanged, combo, &QComboBox::setToolTip, Qt::UniqueConnection);
}

void MainWindow::onAutoMatchLoop()
{
    if (m_busy || m_loopPass > 0)
        return;
    if (m_tuningMode == ColorMatchMode::CameraAuto) {
        QMessageBox::information(this, QStringLiteral("Авто-подгонка"),
                                 QStringLiteral("В режиме встроенного автобаланса используйте «Применить»."));
        return;
    }
    if (!AtemSetup::isLiveAtemControlAvailable() || !m_client || !m_client->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("Авто-подгонка"), QStringLiteral("Нет подключения к ATEM"));
        return;
    }
    {
        QMessageBox box(this);
        box.setIcon(QMessageBox::Question);
        box.setWindowTitle(QStringLiteral("Авто-подгонка"));
        box.setText((m_loopUntilTol
                         ? QStringLiteral("Пока ΔE сцены > %1 (не больше %2 проходов, стоп после %3 проходов без "
                                          "улучшения): захват → расчёт → применение к камерам «В подстройке» → "
                                          "повторный замер.")
                               .arg(m_loopTargetSpin ? m_loopTargetSpin->value() : 1.5, 0, 'f', 1)
                               .arg(m_loopUntilMax)
                               .arg(m_loopStallPasses)
                         : QStringLiteral("До %1 раз: захват → расчёт → применение к камерам «В подстройке» → "
                                          "повторный замер.")
                               .arg(m_loopMaxApplies))
                    + QStringLiteral(" %1 «Откат» вернёт значения до начала.")
                        .arg(unlimitedMode()
                                 ? QStringLiteral("Режим «Без ограничений»: поправки применяются целиком, "
                                                  "в том числе с предупреждениями.")
                                 : QStringLiteral("Шаги ограничены, камеры с подозрительными значениями "
                                                  "пропускаются.")));
        QPushButton *go = box.addButton(QStringLiteral("Начать"), QMessageBox::AcceptRole);
        box.addButton(QStringLiteral("Отмена"), QMessageBox::RejectRole);
        box.setDefaultButton(go);
        box.exec();
        if (box.clickedButton() != go)
            return;
    }
    m_loopHistory.clear();
    m_loopCancel = false;
    if (m_loopUntilTol && m_btnLoopUntil)
        m_btnLoopUntil->setText(QStringLiteral("Стоп"));
    for (CameraRow &c : m_cameras)
        c.irisUnresponsive = false; // lens may have been switched to remote / replaced: probe again
    m_loopPass = 1;
    qCInfo(lcRoute).noquote() << QStringLiteral("auto loop start mode=%1 until=%2 target=%3")
                                     .arg(colorMatchModeLogName(m_tuningMode))
                                     .arg(m_loopUntilTol)
                                     .arg(m_loopTargetSpin ? m_loopTargetSpin->value() : 1.5, 0, 'f', 1);
    onCaptureAnalyze();
    if (!m_busy) // capture refused (message already shown)
        finishAutoMatchLoop(QStringLiteral("захват не запущен"));
}

void MainWindow::continueAutoMatchLoop()
{
    // Called after a measurement pass (proposals are fresh, ΔE before = measured mismatch now).
    const double target = m_loopUntilTol && m_loopTargetSpin ? m_loopTargetSpin->value() : 1.0;
    const int maxApplies = m_loopUntilTol ? m_loopUntilMax : m_loopMaxApplies;
    const int applied = m_loopPass - 1;
    if (m_loopCancel) {
        finishAutoMatchLoop(QStringLiteral("остановлено кнопкой «Стоп»"));
        return;
    }
    QVector<int> toApply;
    double worst = 0.0;
    QStringList progress, stalled;
    for (int i = 0; i < m_cameras.size(); ++i) {
        const CameraRow &c = m_cameras.at(i);
        if (!c.hasProposal)
            continue;
        // dark: the first passes may be raising the level - stop only if it is still dark after two
        if (m_loopUntilTol && (c.lastMeanLuma < 0.0 || (c.lastLowLight && applied >= 2))) {
            finishAutoMatchLoop(QStringLiteral("%1: %2 — проверьте камеру и свет")
                                    .arg(c.binding.label,
                                         c.lastMeanLuma < 0.0 ? QStringLiteral("нет сигнала") : QStringLiteral("тёмный кадр")));
            return;
        }
        worst = std::max(worst, c.deBefore);
        progress << QStringLiteral("%1 ΔE %2").arg(c.binding.label).arg(c.deBefore, 0, 'f', 2);
        // no improvement over the last N passes (best of the last N not better than the best before)
        const QVector<double> h = m_loopHistory.value(c.binding.atemId);
        if (m_loopUntilTol && h.size() > m_loopStallPasses) {
            const double bestBefore = *std::min_element(h.cbegin(), h.cend() - m_loopStallPasses);
            const double bestRecent = *std::min_element(h.cend() - m_loopStallPasses, h.cend());
            if (bestRecent > bestBefore * 0.97) {
                stalled << c.binding.label;
                continue;
            }
        }
        // test mode: apply even with sanity warnings, only unread camera values block it
        const bool allowed = unlimitedMode() ? c.currentOk : c.warnings.isEmpty();
        if (allowed && c.deBefore > target)
            toApply << i;
    }
    if (m_loopUntilTol)
        setCaptureStatus(QStringLiteral("Авто до допуска: проход %1, %2 (цель ΔE ≤ %3)")
                             .arg(m_loopPass)
                             .arg(progress.join(QStringLiteral(", ")))
                             .arg(target, 0, 'f', 1));
    if (toApply.isEmpty()) {
        if (worst <= target)
            finishAutoMatchLoop(QStringLiteral("допуск достигнут (ΔE ≤ %1)").arg(target, 0, 'f', 1));
        else if (!stalled.isEmpty())
            finishAutoMatchLoop(QStringLiteral("нет улучшения %1 прохода подряд (%2) — ΔE %3, допуск %4 не достигнут")
                                    .arg(m_loopStallPasses)
                                    .arg(stalled.join(QStringLiteral(", ")))
                                    .arg(worst, 0, 'f', 1)
                                    .arg(target, 0, 'f', 1));
        else
            finishAutoMatchLoop(QStringLiteral("нечего применять"));
        return;
    }
    if (applied >= maxApplies) {
        finishAutoMatchLoop(m_loopUntilTol ? QStringLiteral("достигнут предел %1 проходов — ΔE %2, допуск %3 не достигнут")
                                                 .arg(applied)
                                                 .arg(worst, 0, 'f', 1)
                                                 .arg(target, 0, 'f', 1)
                                           : QStringLiteral("выполнено %1 прохода").arg(applied));
        return;
    }
    // Sensor gain / iris / white balance need longer to show up in the captured picture than CC values
    // (live test: +25 dB was not yet visible after 0.8 s and the next pass over-corrected).
    bool lightChanged = false;
    for (int i : toApply) {
        const CameraRow &c = m_cameras.at(i);
        lightChanged = lightChanged || c.proposed.gainDb != c.current.gainDb
                       || std::fabs(c.proposed.irisFStopAv - c.current.irisFStopAv) > 0.01f
                       || c.proposed.whiteBalanceKelvin != c.current.whiteBalanceKelvin
                       || c.proposed.whiteBalanceTint != c.current.whiteBalanceTint;
    }
    const int settleMs = lightChanged ? 3000 : 800;
    QString errors;
    const int sent = sendProposals(toApply, &errors);
    qCInfo(lcRoute).noquote() << QStringLiteral("auto loop pass %1 applied to %2 camera(s) %3")
                                     .arg(m_loopPass)
                                     .arg(sent)
                                     .arg(errors.trimmed());
    if (sent == 0) {
        finishAutoMatchLoop(QStringLiteral("ошибка отправки: %1").arg(errors.trimmed()));
        return;
    }
    ++m_loopPass;
    setCaptureStatus(QStringLiteral("Авто-подгонка: проход %1 — ждём, пока камеры применят значения…").arg(m_loopPass));
    // Camera needs a moment to apply; then measure again.
    QTimer::singleShot(settleMs, this, [this] {
        if (m_loopPass <= 0 || m_closing.load())
            return;
        if (m_loopCancel) {
            finishAutoMatchLoop(QStringLiteral("остановлено кнопкой «Стоп»"));
            return;
        }
        onCaptureAnalyze();
        if (!m_busy)
            finishAutoMatchLoop(QStringLiteral("захват не запущен"));
    });
}

void MainWindow::finishAutoMatchLoop(const QString &why)
{
    if (m_loopPass <= 0)
        return;
    m_loopPass = 0;
    m_loopCancel = false;
    if (m_btnLoopUntil) {
        m_btnLoopUntil->setText(QStringLiteral("Авто до допуска"));
        m_btnLoopUntil->setEnabled(!m_busy);
    }
    if (m_btnLoop)
        m_btnLoop->setEnabled(!m_busy);
    QStringList parts;
    for (const CameraRow &c : m_cameras) {
        const QVector<double> h = m_loopHistory.value(c.binding.atemId);
        if (h.isEmpty())
            continue;
        QStringList v;
        for (double d : h)
            v << QString::number(d, 'f', 1);
        parts << QStringLiteral("%1: ΔE %2").arg(c.binding.label, v.join(QStringLiteral(" → ")));
    }
    const QString text = QStringLiteral("Авто-подгонка завершена (%1). %2").arg(why, parts.join(QStringLiteral("; ")));
    qCInfo(lcRoute).noquote() << text;
    setCaptureStatus(text);
    statusBar()->showMessage(text, 30000);
    refreshCorrectionsTable();
}

void MainWindow::refreshCorrectionsTable()
{
    if (!m_corrTable)
        return;
    QVector<int> rows;
    for (int i = 0; i < m_cameras.size(); ++i) {
        const CameraRow &c = m_cameras.at(i);
        if (c.hasProposal || !c.notes.isEmpty())
            rows << i;
    }
    m_corrTable->setRowCount(rows.size());
    for (int r = 0; r < rows.size(); ++r) {
        const CameraRow &c = m_cameras.at(rows.at(r));
        auto *name = new QTableWidgetItem(c.binding.label);
        m_corrTable->setItem(r, 0, name);
        QStringList history;
        for (double d : m_loopHistory.value(c.binding.atemId))
            history << QString::number(d, 'f', 1);
        if (!c.hasProposal) {
            m_corrTable->setItem(r, 1, new QTableWidgetItem(QStringLiteral("—")));
            auto *deItem = new QTableWidgetItem(history.isEmpty() ? QStringLiteral("—")
                                                                  : QStringLiteral("по проходам: ") + history.join(QStringLiteral(" → ")));
            deItem->setToolTip(deItem->text());
            m_corrTable->setItem(r, 2, deItem);
            auto *noteItem = new QTableWidgetItem(c.notes.join(QStringLiteral("; ")));
            noteItem->setToolTip(noteItem->text());
            m_corrTable->setItem(r, 3, noteItem);
            m_corrTable->setItem(r, 4, new QTableWidgetItem());
            continue;
        }
        m_corrTable->setItem(r, 1, new QTableWidgetItem(QStringLiteral("%1 → %2")
                                                            .arg(c.errBefore, 0, 'f', 1)
                                                            .arg(c.errAfter, 0, 'f', 1)));
        QString deText = c.deBefore >= 0 ? QStringLiteral("%1 → %2 (прогноз)")
                                               .arg(c.deBefore, 0, 'f', 1)
                                               .arg(c.deAfter, 0, 'f', 1)
                                         : QStringLiteral("—");
        if (history.size() > 1)
            deText = QStringLiteral("по проходам: ") + history.join(QStringLiteral(" → ")) + QLatin1Char('\n') + deText;
        auto *deItem = new QTableWidgetItem(deText);
        deItem->setToolTip(QStringLiteral("Первое число — измеренная разница с целью сейчас, второе — прогноз после "
                                          "применения. «По проходам» — измерения авто-подгонки.\n")
                           + deText);
        m_corrTable->setItem(r, 2, deItem);
        QString change = summarizeChange(c.current, c.proposed);
        if (!c.notes.isEmpty())
            change += QStringLiteral("\n— ") + c.notes.join(QStringLiteral("\n— "));
        auto *changeItem = new QTableWidgetItem(change);
        changeItem->setToolTip(change);
        m_corrTable->setItem(r, 3, changeItem);
        auto *warn = new QTableWidgetItem(c.warnings.isEmpty() ? QStringLiteral("ок") : c.warnings.join(QStringLiteral("; ")));
        warn->setForeground(c.warnings.isEmpty() ? QColor(120, 210, 120) : QColor(255, 120, 100));
        warn->setToolTip(warn->text());
        m_corrTable->setItem(r, 4, warn);
        m_corrTable->item(r, 1)->setToolTip(QStringLiteral("Ошибка уровня, IRE: сейчас → прогноз"));
    }
    m_corrTable->resizeRowsToContents();
    if (m_corrTable->rowCount() > 0) {
        m_corrTable->setCurrentCell(-1, -1);
        m_corrTable->setCurrentCell(0, 0);
    } else if (m_corrDetails) {
        m_corrDetails->setVisible(false);
    }
}

void MainWindow::loadTuningUi()
{
    m_tuningMode = colorMatchModeFromId(AtemAppSettings::tuningMode());
    if (m_tuningModeCombo) {
        QSignalBlocker block(m_tuningModeCombo);
        const int index = m_tuningModeCombo->findData(colorMatchModeId(m_tuningMode));
        if (index >= 0)
            m_tuningModeCombo->setCurrentIndex(index);
    }
    m_paramMask = CorrectionParameterMask::allEnabled();
    loadEngineSettings();
    m_referenceFramePath = AtemAppSettings::referenceFramePath();
    if (!m_referenceFramePath.isEmpty()) {
        ReferenceFrameStore::loadReferenceImage(m_referenceFramePath, &m_referenceFrameImage,
                                                &m_referenceFrameAnalysis);
    }
    updateTuningPanelVisibility();
}

void MainWindow::updateTuningPanelVisibility()
{
    const ColorMatchMode m = m_tuningMode;
    const bool roiMode = m == ColorMatchMode::GrayCard || m == ColorMatchMode::CommonSample;
    const bool checker = m == ColorMatchMode::ColorChecker;
    const bool refFrame = m == ColorMatchMode::ReferenceFrame;
    const bool refCam = m == ColorMatchMode::ReferenceCamera
                        || (m == ColorMatchMode::CommonSample && m_sampleTargetCombo
                            && m_sampleTargetCombo->currentData().toString() == QLatin1String("reference"));
    const bool camAuto = m == ColorMatchMode::CameraAuto;
    if (m_btnSaveRef)
        m_btnSaveRef->setVisible(refFrame);
    if (m_btnLoadRef)
        m_btnLoadRef->setVisible(refFrame);
    const bool marking = roiMode || checker || m == ColorMatchMode::ReferenceCamera;
    if (m_markCameraCombo)
        m_markCameraCombo->setVisible(marking);
    if (m_markLabel)
        m_markLabel->setVisible(marking);
    if (m_referenceCombo)
        m_referenceCombo->setVisible(refCam);
    if (m_refLabel)
        m_refLabel->setVisible(refCam);
    if (m_sampleTargetCombo)
        m_sampleTargetCombo->setVisible(m == ColorMatchMode::CommonSample);
    if (m_chartCombo)
        m_chartCombo->setVisible(checker);
    for (QWidget *w : std::initializer_list<QWidget *>{m_chkBalance, m_chkExposure, m_chkFitLift, m_chkFitGamma,
                                                        m_chkFitSat, m_balanceViaCombo, m_exposureViaCombo,
                                                        m_strengthSpin, m_chkUnlimited})
        if (w)
            w->setVisible(!camAuto);
    if (m_chkAutoIris)
        m_chkAutoIris->setVisible(camAuto);
    if (m_targetIreSpin)
        m_targetIreSpin->setVisible(m == ColorMatchMode::NeutralBalance || m == ColorMatchMode::GrayCard
                                    || (m == ColorMatchMode::CommonSample && m_sampleTargetCombo
                                        && m_sampleTargetCombo->currentData().toString() == QLatin1String("neutral")));
    if (m_btnCapture)
        m_btnCapture->setEnabled(!camAuto && !m_busy);
    if (m_modeHint)
        m_modeHint->setText(colorMatchModeHint(m));
    if (m_previewOverlay) {
        // whole-frame modes: no zone on the preview and no accidental drawing with the mouse
        m_previewOverlay->setTool(checker   ? PreviewOverlayWidget::Tool::ColorCheckerQuad
                                  : marking ? PreviewOverlayWidget::Tool::GrayRect
                                            : PreviewOverlayWidget::Tool::None);
    }
    if (m_btnClearZone)
        m_btnClearZone->setVisible(marking);
    applyOverlayForMarkCamera();
}

void MainWindow::onTuningModeChanged(int index)
{
    if (!m_tuningModeCombo || index < 0)
        return;
    m_tuningMode = colorMatchModeFromId(m_tuningModeCombo->itemData(index).toString());
    AtemAppSettings::setTuningMode(colorMatchModeId(m_tuningMode));
    updateTuningPanelVisibility();
    refreshCorrectionsTable();
}

void MainWindow::onParamMaskChanged()
{
    m_paramMask = CorrectionParameterMask::allEnabled();
    saveEngineSettings();
}

void MainWindow::onMarkCameraChanged(int)
{
    applyOverlayForMarkCamera();
}

void MainWindow::applyOverlayForMarkCamera()
{
    if (!m_previewOverlay || !m_markCameraCombo)
        return;
    const qint64 id = m_markCameraCombo->currentData().toLongLong();
    for (const CameraRow &camera : m_cameras) {
        if (camera.binding.atemId != id)
            continue;
        m_previewOverlay->setGrayRoi(camera.grayRoi);
        m_previewOverlay->setCheckerCorners(camera.checkerCorners);
        return;
    }
    m_previewOverlay->setGrayRoi({});
    m_previewOverlay->setCheckerCorners({});
}

void MainWindow::onGrayRoiEdited(const QRectF &roi)
{
    if (!m_markCameraCombo)
        return;
    const qint64 id = m_markCameraCombo->currentData().toLongLong();
    for (CameraRow &camera : m_cameras) {
        if (camera.binding.atemId == id) {
            camera.grayRoi = roi;
            saveCameraRois();
            break;
        }
    }
}

void MainWindow::onCheckerRoiEdited(const QVector<QPointF> &corners)
{
    if (!m_markCameraCombo)
        return;
    const qint64 id = m_markCameraCombo->currentData().toLongLong();
    for (CameraRow &camera : m_cameras) {
        if (camera.binding.atemId == id) {
            camera.checkerCorners = corners;
            saveCameraRois();
            break;
        }
    }
}

void MainWindow::saveCameraRois()
{
    QJsonArray array;
    for (const CameraRow &camera : m_cameras) {
        QJsonObject obj;
        obj.insert(QStringLiteral("atemId"), static_cast<double>(camera.binding.atemId));
        if (!camera.grayRoi.isEmpty()) {
            QJsonArray gray;
            gray.append(camera.grayRoi.x());
            gray.append(camera.grayRoi.y());
            gray.append(camera.grayRoi.width());
            gray.append(camera.grayRoi.height());
            obj.insert(QStringLiteral("gray"), gray);
        }
        if (camera.checkerCorners.size() == 4) {
            QJsonArray quad;
            for (const QPointF &point : camera.checkerCorners) {
                QJsonArray pair;
                pair.append(point.x());
                pair.append(point.y());
                quad.append(pair);
            }
            obj.insert(QStringLiteral("checker"), quad);
        }
        array.append(obj);
    }
    AtemAppSettings::setCameraRoiJson(QString::fromUtf8(QJsonDocument(array).toJson(QJsonDocument::Compact)));
}

void MainWindow::loadCameraRois()
{
    const QJsonDocument doc = QJsonDocument::fromJson(AtemAppSettings::cameraRoiJson().toUtf8());
    if (!doc.isArray())
        return;
    for (const QJsonValue &value : doc.array()) {
        const QJsonObject obj = value.toObject();
        const qint64 id = static_cast<qint64>(obj.value(QStringLiteral("atemId")).toDouble());
        for (CameraRow &camera : m_cameras) {
            if (camera.binding.atemId != id)
                continue;
            const QJsonArray gray = obj.value(QStringLiteral("gray")).toArray();
            if (gray.size() == 4) {
                camera.grayRoi = QRectF(gray.at(0).toDouble(), gray.at(1).toDouble(), gray.at(2).toDouble(),
                                        gray.at(3).toDouble());
            }
            const QJsonArray quad = obj.value(QStringLiteral("checker")).toArray();
            camera.checkerCorners.clear();
            for (const QJsonValue &pointValue : quad) {
                const QJsonArray pair = pointValue.toArray();
                if (pair.size() == 2)
                    camera.checkerCorners.append(QPointF(pair.at(0).toDouble(), pair.at(1).toDouble()));
            }
            break;
        }
    }
}

void MainWindow::onSaveReferenceFrame()
{
    const int index = m_referenceIndex;
    if (index < 0 || index >= m_cameras.size()) {
        QMessageBox::warning(this, QStringLiteral("Эталон"), QStringLiteral("Выберите камеру-эталон."));
        return;
    }
    const CameraRow &camera = m_cameras.at(index);
    QImage image = camera.lastFrame;
    FrameColorAnalysis analysis = camera.analysis;
    if (image.isNull() && !m_lastPreviewFrame.isNull()) {
        image = m_lastPreviewFrame;
        analysis = ColorAnalysis::analyze(image);
    }
    if (image.isNull() || analysis.sampleCount == 0) {
        QMessageBox::warning(this, QStringLiteral("Эталон"),
                             QStringLiteral("Нет кадра. Сначала выполните захват / анализ."));
        return;
    }
    SavedReferenceFrame saved;
    const QString error =
        ReferenceFrameStore::saveReference(image, analysis, camera.binding.label, &saved);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Эталон"), error);
        return;
    }
    m_referenceFramePath = saved.imagePath;
    m_referenceFrameAnalysis = analysis;
    m_referenceFrameImage = image;
    AtemAppSettings::setReferenceFramePath(m_referenceFramePath);
    QMessageBox::information(this, QStringLiteral("Эталон"),
                             QStringLiteral("Сохранено:\n%1").arg(saved.imagePath));
}

void MainWindow::onLoadReferenceFrame()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("Эталонный кадр"), ReferenceFrameStore::referencesDirectory(),
        QStringLiteral("Изображения (*.png *.jpg *.jpeg)"));
    if (path.isEmpty())
        return;
    const QString error =
        ReferenceFrameStore::loadReferenceImage(path, &m_referenceFrameImage, &m_referenceFrameAnalysis);
    if (!error.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Эталон"), error);
        return;
    }
    m_referenceFramePath = path;
    AtemAppSettings::setReferenceFramePath(path);
}

FrameColorAnalysis MainWindow::averageLiveAnalysis(const QRect &crop,
                                                   const std::atomic<bool> *cancel) const
{
#ifndef HAVE_GSTREAMER
    Q_UNUSED(crop);
    Q_UNUSED(cancel);
    return {};
#else
    constexpr int kAverageMs = 750;
    if (!m_preview)
        return {};
    QElapsedTimer timer;
    timer.start();
    FrameColorAnalysis acc;
    while (timer.elapsed() < kAverageMs) {
        if (cancel && cancel->load())
            break;
        QImage image;
        if (!m_preview->copyLatest(image) || image.isNull()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(40));
            continue;
        }
        const QImage work = crop.isEmpty() ? image : image.copy(crop.intersected(image.rect()));
        if (work.isNull())
            continue;
        acc = ColorAnalysis::mergeAccumulate(acc, ColorAnalysis::analyze(work));
        std::this_thread::sleep_for(std::chrono::milliseconds(66));
    }
    ColorAnalysis::finalizeMerged(acc);
    return acc;
#endif
}

void MainWindow::runMatchingAfterCapture(const QStringList &problems)
{
    const MatchEngine::Settings st = engineSettings();
    const double targetIre = m_targetIreSpin ? m_targetIreSpin->value() : 41.0;
    for (CameraRow &c : m_cameras) {
        c.hasProposal = false;
        c.notes.clear();
        c.warnings.clear();
    }
    // A marked zone is used only by the modes that let you mark one; the others measure the whole frame
    // (a zone left over from another mode must not silently crop the picture).
    const bool zoneMode = m_tuningMode == ColorMatchMode::ReferenceCamera || m_tuningMode == ColorMatchMode::GrayCard
                          || m_tuningMode == ColorMatchMode::CommonSample;
    const auto roiAnalysis = [&](const CameraRow &cam) -> FrameColorAnalysis {
        if (zoneMode && !cam.grayRoi.isEmpty() && !cam.lastFrame.isNull())
            return analyzeNormalizedRoi(cam.lastFrame, cam.grayRoi);
        return cam.analysis;
    };
    const auto roiRect = [](const QImage &img, const QRectF &n) {
        if (n.isEmpty())
            return QRect();
        return QRect(qRound(n.x() * img.width()), qRound(n.y() * img.height()), qRound(n.width() * img.width()),
                     qRound(n.height() * img.height()));
    };
    const auto finishDelta = [&](CameraRow &cam, const MatchEngine::Delta &d, const MatchEngine::Settings &s,
                                 int pairCount, double sceneDe = -1.0) {
        Q_UNUSED(pairCount);
        if (!d.valid) {
            cam.notes << d.notes;
            return;
        }
        QStringList notes = d.notes;
        MatchEngine::Settings sc = s;
        sc.irisUsable = !cam.irisUnresponsive;
        cam.proposed = MatchEngine::compose(cam.current, d, sc, &notes);
        cam.notes << notes;
        cam.errBefore = d.rmsBeforeIre;
        cam.errAfter = d.rmsAfterIre;
        cam.deBefore = d.deBefore;
        cam.deAfter = d.deAfter;
        if (sceneDe >= 0.0 && d.deBefore >= 0.0) {
            // reference mode: the grey pairs alone say "matched" while shadows / contrast / saturation are
            // still off - the loop goes on the whole-picture difference
            cam.deBefore = std::max(d.deBefore, sceneDe);
            cam.deAfter = std::max(d.deAfter, 0.0);
        }
        cam.warnings = MatchEngine::sanityWarnings(cam.current, cam.proposed);
        if (cam.hasRollback && !unlimitedMode()) {
            // Several small passes must not add up to a wild change: check the total drift from the
            // values before the first apply as well (the auto loop skips cameras with warnings).
            for (const QString &w : MatchEngine::sanityWarnings(cam.rollback, cam.proposed))
                cam.warnings << QStringLiteral("от исходных значений: %1").arg(w);
        }
        if (!cam.currentOk)
            cam.warnings << QStringLiteral("текущие значения камеры не прочитаны (нет Camera Control?) — "
                                           "поправка отсчитана от заводских");
        cam.hasProposal = true;
        cam.match.label = cam.binding.label;
        cam.match.suggested = cam.proposed;
        cam.match.damped = cam.proposed;
        qCInfo(lcRoute).noquote() << QStringLiteral("match %1 mode=%2 pairs=%3 err %4->%5 IRE dE %6->%7 %8")
                                         .arg(cam.binding.label, colorMatchModeLogName(m_tuningMode))
                                         .arg(d.pairCount)
                                         .arg(d.rmsBeforeIre, 0, 'f', 2)
                                         .arg(d.rmsAfterIre, 0, 'f', 2)
                                         .arg(d.deBefore, 0, 'f', 2)
                                         .arg(d.deAfter, 0, 'f', 2)
                                         .arg(summarizeChange(cam.current, cam.proposed));
        if (m_loopPass > 0 && d.deBefore >= 0)
            m_loopHistory[cam.binding.atemId] << cam.deBefore;
    };
    const auto finish = [&](CameraRow &cam, const QVector<MatchEngine::Pair> &pairs, MatchEngine::Settings s) {
        if (pairs.isEmpty()) {
            cam.notes << QStringLiteral("нет данных для расчёта");
            return;
        }
        finishDelta(cam, MatchEngine::solve(pairs, s), s, pairs.size());
    };
    const auto neutralPair = [&](const MatchEngine::Rgb &m, bool toTargetLevel) {
        MatchEngine::Pair p;
        p.measured = m;
        const double y = ColorScience::luma(m);
        const double t = toTargetLevel ? targetIre / 100.0 : y;
        p.target = {t, t, t};
        p.neutral = true;
        return p;
    };
    QStringList failures;

    // No signal on an input must never produce a correction. "No signal" = essentially constant frame
    // (no detail, no noise); a dark but real scene is processed with a «мало света» note.
    const QVector<bool> activeMask = captureActiveMask();
    const auto participates = [&](int i) { return i >= 0 && i < m_cameras.size() && m_cameras.at(i).includeInTuning; };
    QVector<MatchEngine::SignalCheck> signal(m_cameras.size());
    for (int i = 0; i < m_cameras.size(); ++i) {
        CameraRow &cam = m_cameras[i];
        if (!activeMask.value(i, false)) {
            cam.lastFrame = QImage();
            cam.analysis = FrameColorAnalysis();
            continue;
        }
        if (cam.lastFrame.isNull())
            continue;
        signal[i] = MatchEngine::checkSignal(cam.lastFrame);
        qCInfo(lcRoute).noquote() << QStringLiteral("signal %1 mean=%2 p99=%3 std=%4 detail=%5 %6")
                                         .arg(cam.binding.label)
                                         .arg(signal[i].meanLuma * 255, 0, 'f', 1)
                                         .arg(signal[i].p99Luma * 255, 0, 'f', 0)
                                         .arg(signal[i].stdLuma * 255, 0, 'f', 2)
                                         .arg(signal[i].detail * 255, 0, 'f', 2)
                                         .arg(signal[i].noSignal ? QStringLiteral("NO-SIGNAL")
                                                                 : (signal[i].lowLight ? QStringLiteral("low-light")
                                                                                       : QStringLiteral("ok")));
        if (cam.irisProbePending && !signal[i].noSignal) {
            cam.irisProbePending = false;
            const bool accepted = cam.current.irisFStopAv >= 0.0f
                                  && std::fabs(cam.current.irisFStopAv - cam.irisProbeAvBefore) > 0.1;
            const double otherStops =
                (cam.current.gainDb - cam.irisProbeDbBefore) / 6.02
                + std::log2(std::max(1e-3, static_cast<double>(cam.current.gainY) / cam.irisProbeGainYBefore)) / 0.45;
            const bool followed = accepted
                                  && MatchEngine::irisResponded(cam.irisProbeMeanBefore, signal[i].meanLuma,
                                                                cam.irisProbeExpectStops, otherStops);
            qCInfo(lcRoute).noquote() << QStringLiteral("iris probe %1 expect %2 st accepted=%3 followed=%4")
                                             .arg(cam.binding.label)
                                             .arg(cam.irisProbeExpectStops, 0, 'f', 2)
                                             .arg(accepted)
                                             .arg(followed);
            if (!followed) {
                cam.irisUnresponsive = true;
                cam.notes << QStringLiteral("диафрагма не реагирует (объектив без управления?) — дальше уровень "
                                            "через усиление");
            }
        }
        cam.lastMeanLuma = signal[i].noSignal ? -1.0 : signal[i].meanLuma;
        cam.lastLowLight = !signal[i].noSignal && signal[i].lowLight;
        if (signal[i].noSignal) {
            cam.notes << signal[i].reason;
            cam.lastFrame = QImage();
            cam.analysis.sampleCount = 0;
        } else if (signal[i].lowLight) {
            cam.notes << signal[i].reason;
        }
    }

    switch (m_tuningMode) {
    case ColorMatchMode::CameraAuto:
        setCaptureStatus(QStringLiteral("Режим встроенного автобаланса: нажмите «Применить»"));
        return;
    case ColorMatchMode::ReferenceCamera:
    case ColorMatchMode::ReferenceFrame:
    case ColorMatchMode::CameraAverage: {
        FrameColorAnalysis target;
        int refIndex = -1;
        if (m_tuningMode == ColorMatchMode::ReferenceCamera) {
            refIndex = m_referenceIndex;
            if (refIndex < 0 || refIndex >= m_cameras.size() || m_cameras.at(refIndex).analysis.sampleCount == 0) {
                failures << QStringLiteral("Нет кадра эталонной камеры.");
                break;
            }
            target = roiAnalysis(m_cameras.at(refIndex));
        } else if (m_tuningMode == ColorMatchMode::ReferenceFrame) {
            if (m_referenceFrameAnalysis.sampleCount == 0) {
                failures << QStringLiteral("Загрузите или сохраните эталонный кадр.");
                break;
            }
            target = m_referenceFrameAnalysis;
        } else {
            QVector<FrameColorAnalysis> parts;
            for (const CameraRow &c : m_cameras)
                if (c.includeInTuning && c.analysis.sampleCount > 0)
                    parts << roiAnalysis(c);
            if (parts.size() < 2) {
                failures << QStringLiteral("Для среднего нужны минимум две отмеченные камеры с кадрами.");
                break;
            }
            target = ColorTuning::averageAnalyses(parts);
        }
        // Reference camera / frame: robust scene statistics (light colour, median level, percentiles)
        // instead of comparing whole-frame histograms of different scenes.
        const auto roiImage = [&](const CameraRow &c) {
            const QRect r = zoneMode ? roiRect(c.lastFrame, c.grayRoi) : QRect();
            return r.isEmpty() ? c.lastFrame : c.lastFrame.copy(r);
        };
        MatchEngine::SceneStats refStats, refFull;
        if (m_tuningMode == ColorMatchMode::ReferenceCamera && !m_cameras.at(refIndex).lastFrame.isNull()) {
            QStringList refNotes;
            refStats = MatchEngine::sceneStats(roiImage(m_cameras.at(refIndex)), st.transfer, &refNotes);
            refFull = MatchEngine::sceneStats(m_cameras.at(refIndex).lastFrame, st.transfer);
            if (!refStats.valid)
                failures << QStringLiteral("Кадр эталонной камеры непригоден: %1").arg(refNotes.join(QStringLiteral("; ")));
        } else if (m_tuningMode == ColorMatchMode::ReferenceFrame && !m_referenceFrameImage.isNull()) {
            refStats = MatchEngine::sceneStats(m_referenceFrameImage, st.transfer);
        }
        for (int i = 0; i < m_cameras.size(); ++i) {
            CameraRow &cam = m_cameras[i];
            if (i == refIndex || cam.analysis.sampleCount == 0 || !participates(i))
                continue;
            if (refStats.valid && !cam.lastFrame.isNull()) {
                QStringList pre;
                const MatchEngine::SceneStats cs = MatchEngine::sceneStats(roiImage(cam), st.transfer, &pre);
                if (!cs.valid) {
                    cam.notes << pre;
                    continue;
                }
                MatchEngine::Settings sm = st;
                const bool commonRegion = m_tuningMode == ColorMatchMode::ReferenceCamera && !cam.grayRoi.isEmpty()
                                          && !m_cameras.at(refIndex).grayRoi.isEmpty();
                // Tone (lift / gamma), per-zone tint and saturation: from the whole frames. A marked zone (live:
                // a white sheet, 5 IRE of contrast, no shadows) sets only the light colour and the level; with a
                // zone on one camera only it is ignored. Both cameras are expected to see a similar scene.
                MatchEngine::SceneStats camFull;
                const bool zoneUsed = commonRegion && refFull.valid;
                if (zoneUsed)
                    camFull = MatchEngine::sceneStats(cam.lastFrame, st.transfer);
                if (m_tuningMode == ColorMatchMode::ReferenceCamera && !commonRegion
                    && (!cam.grayRoi.isEmpty() || !m_cameras.at(refIndex).grayRoi.isEmpty()))
                    pre << QStringLiteral("область отмечена только на одной камере — замер по всему кадру");
                if (zoneUsed && (sm.lift || sm.gamma || sm.saturation))
                    pre << QStringLiteral("область — для баланса и уровня; лифт/гамма/насыщенность — по всему кадру");
                // Exposure first: when the frames differ by more than 2 stops (or the camera frame is
                // dark), colour / tone statistics are unreliable - this pass corrects the level only.
                const double expRatio = cs.p50Linear > 1e-5 ? refStats.p50Linear / cs.p50Linear : 1.0;
                const bool refLow = refIndex >= 0 && signal[refIndex].lowLight;
                if (sm.exposure && (expRatio > 4.0 || expRatio < 0.25 || (signal[i].lowLight && !refLow))
                    && (sm.balance || sm.lift || sm.gamma || sm.saturation)) {
                    sm.balance = sm.lift = sm.gamma = sm.saturation = false;
                    pre << QStringLiteral("сначала уровень: кадр %1 — цвет и тон на следующем проходе")
                               .arg(signal[i].lowLight && !refLow ? QStringLiteral("тёмный")
                                                       : QStringLiteral("отличается от эталона более чем на 2 ступени"));
                }
                const MatchEngine::ReferenceMatch rm = MatchEngine::referenceMatch(
                    cs, refStats, sm, &pre, zoneUsed && camFull.valid ? &camFull : nullptr,
                    zoneUsed && camFull.valid ? &refFull : nullptr);
                MatchEngine::Settings s2 = sm;
                s2.saturation = false; // saturation comes from the median-chroma ratio, not from pairs
                MatchEngine::Delta d = MatchEngine::solve(rm.pairs, s2);
                if (sm.saturation)
                    d.saturation = rm.saturation;
                cam.notes << pre;
                MatchEngine::SceneDiff sd;
                if (m_tuningMode == ColorMatchMode::ReferenceCamera || m_tuningMode == ColorMatchMode::ReferenceFrame)
                    sd = MatchEngine::compareScenes(zoneUsed && camFull.valid ? camFull : cs,
                                                    zoneUsed && camFull.valid ? refFull : refStats, st.transfer);
                if (sd.valid && zoneUsed && camFull.valid) {
                    // the loop must see both: the zone (light colour) and the whole picture (tone)
                    const MatchEngine::SceneDiff zd = MatchEngine::compareScenes(cs, refStats, st.transfer);
                    if (zd.valid)
                        sd.sceneDe = std::max(sd.sceneDe, zd.zoneDeMean);
                }
                if (sd.valid) {
                    cam.notes << QStringLiteral("кадр против эталона: ΔE зон %1 (тени %2, середина %3, света %4), "
                                                "чёрное %5/%6 IRE, контраст %7/%8 IRE, насыщенность C* %9/%10")
                                     .arg(sd.zoneDeMean, 0, 'f', 1)
                                     .arg(sd.zoneDe[0], 0, 'f', 1)
                                     .arg(sd.zoneDe[2], 0, 'f', 1)
                                     .arg(sd.zoneDe[4], 0, 'f', 1)
                                     .arg(sd.shadowIre[0], 0, 'f', 1)
                                     .arg(sd.shadowIre[1], 0, 'f', 1)
                                     .arg(sd.contrastIre[0], 0, 'f', 1)
                                     .arg(sd.contrastIre[1], 0, 'f', 1)
                                     .arg(sd.chroma75[0], 0, 'f', 1)
                                     .arg(sd.chroma75[1], 0, 'f', 1);
                    qCInfo(lcRoute).noquote()
                        << QStringLiteral("scene %1 dE %2 zones %3/%4/%5/%6/%7 black %8/%9 contrast %10/%11 C75 %12/%13 "
                                          "C90 %14/%15 sat %16")
                               .arg(cam.binding.label)
                               .arg(sd.sceneDe, 0, 'f', 2)
                               .arg(sd.zoneDe[0], 0, 'f', 1).arg(sd.zoneDe[1], 0, 'f', 1).arg(sd.zoneDe[2], 0, 'f', 1)
                               .arg(sd.zoneDe[3], 0, 'f', 1).arg(sd.zoneDe[4], 0, 'f', 1)
                               .arg(sd.shadowIre[0], 0, 'f', 1).arg(sd.shadowIre[1], 0, 'f', 1)
                               .arg(sd.contrastIre[0], 0, 'f', 1).arg(sd.contrastIre[1], 0, 'f', 1)
                               .arg(cs.chromaP75, 0, 'f', 1).arg(refStats.chromaP75, 0, 'f', 1)
                               .arg(cs.chromaP90, 0, 'f', 1).arg(refStats.chromaP90, 0, 'f', 1)
                               .arg(rm.saturation, 0, 'f', 3);
                }
                finishDelta(cam, d, sm, rm.pairs.size(), sd.valid ? sd.sceneDe : -1.0);
                qCInfo(lcRoute).noquote()
                    << QStringLiteral("refmatch %1 ill %2/%3/%4 -> %5/%6/%7 p50 %8 -> %9 neutral %10%")
                           .arg(cam.binding.label)
                           .arg(cs.illuminant.r, 0, 'f', 3).arg(cs.illuminant.g, 0, 'f', 3).arg(cs.illuminant.b, 0, 'f', 3)
                           .arg(refStats.illuminant.r, 0, 'f', 3).arg(refStats.illuminant.g, 0, 'f', 3)
                           .arg(refStats.illuminant.b, 0, 'f', 3)
                           .arg(cs.p50Linear, 0, 'f', 4).arg(refStats.p50Linear, 0, 'f', 4)
                           .arg(cs.neutralFraction * 100, 0, 'f', 1);
                continue;
            }
            finish(cam, MatchEngine::quantilePairs(roiAnalysis(cam), target), st);
        }
        break;
    }
    case ColorMatchMode::CommonSample:
    case ColorMatchMode::GrayCard: {
        const QString targetKind = m_tuningMode == ColorMatchMode::GrayCard
                                       ? QStringLiteral("neutral")
                                       : (m_sampleTargetCombo ? m_sampleTargetCombo->currentData().toString()
                                                              : QStringLiteral("reference"));
        // Region mean per camera (trimmed); cameras without a marked region use the whole frame.
        QVector<MatchEngine::Rgb> means(m_cameras.size());
        QVector<bool> ok(m_cameras.size(), false);
        for (int i = 0; i < m_cameras.size(); ++i) {
            const CameraRow &c = m_cameras.at(i);
            if (c.lastFrame.isNull())
                continue;
            means[i] = MatchEngine::regionMean(c.lastFrame, roiRect(c.lastFrame, c.grayRoi));
            // A sample that is nearly black or clipped carries no usable colour information
            // and would demand an extreme gain — skip that camera.
            const double y = ColorScience::luma(means[i]);
            const double mx = std::max({means[i][0], means[i][1], means[i][2]});
            if (y < 0.03 || mx > 0.97) {
                m_cameras[i].notes << (y < 0.03 ? QStringLiteral("образец слишком тёмный — поправка не считается")
                                                : QStringLiteral("образец пересвечен — поправка не считается"));
                continue;
            }
            ok[i] = true;
        }
        int refIndex = -1;
        MatchEngine::Rgb target;
        if (targetKind == QLatin1String("reference")) {
            refIndex = m_referenceIndex;
            if (refIndex < 0 || refIndex >= m_cameras.size() || !ok.value(refIndex)) {
                failures << QStringLiteral("Нет кадра эталонной камеры.");
                break;
            }
            target = means[refIndex];
        } else if (targetKind == QLatin1String("average")) {
            int n = 0;
            for (int i = 0; i < m_cameras.size(); ++i)
                if (ok[i] && m_cameras.at(i).includeInTuning) {
                    for (int ch = 0; ch < 3; ++ch)
                        target[ch] += means[i][ch];
                    ++n;
                }
            if (n < 2) {
                failures << QStringLiteral("Для среднего нужны минимум две камеры.");
                break;
            }
            for (int ch = 0; ch < 3; ++ch)
                target[ch] /= n;
        }
        for (int i = 0; i < m_cameras.size(); ++i) {
            CameraRow &cam = m_cameras[i];
            if (!ok[i] || i == refIndex || !participates(i))
                continue;
            if (cam.grayRoi.isEmpty())
                cam.notes << QStringLiteral("область не выделена — весь кадр");
            QVector<MatchEngine::Pair> pairs;
            if (targetKind == QLatin1String("neutral")) {
                pairs << neutralPair(means[i], st.exposure);
            } else {
                MatchEngine::Pair p;
                p.measured = means[i];
                p.target = target;
                pairs << p;
            }
            finish(cam, pairs, st);
        }
        break;
    }
    case ColorMatchMode::ColorChecker: {
        const ChartPreset chart = ColorCharts::byId(m_chartCombo ? m_chartCombo->currentData().toString()
                                                                 : QStringLiteral("cc24"));
        for (CameraRow &cam : m_cameras) {
            if (cam.lastFrame.isNull() || !cam.includeInTuning)
                continue;
            if (cam.checkerCorners.size() != 4) {
                cam.notes << QStringLiteral("не отмечены 4 угла таблицы");
                continue;
            }
            QVector<QPointF> corners;
            for (const QPointF &pt : cam.checkerCorners)
                corners << QPointF(pt.x() * cam.lastFrame.width(), pt.y() * cam.lastFrame.height());
            int rot = 0;
            const QVector<ChartPatchSample> patches =
                ColorCharts::sample(cam.lastFrame, corners, chart, st.transfer, &rot);
            QVector<MatchEngine::Pair> pairs;
            for (const ChartPatchSample &s : patches) {
                MatchEngine::Pair p;
                p.measured = s.measured;
                p.target = s.reference;
                p.neutral = s.neutral;
                pairs << p;
            }
            MatchEngine::Settings s = st;
            s.liveLimits = false; // exact chart reference: full step
            finish(cam, pairs, s);
            if (rot != 0)
                cam.notes << QStringLiteral("таблица повёрнута на %1°").arg(rot * 90);
        }
        break;
    }
    case ColorMatchMode::NeutralBalance: {
        for (CameraRow &cam : m_cameras) {
            if (cam.lastFrame.isNull() || !cam.includeInTuning)
                continue;
            QStringList notes;
            const QVector<MatchEngine::Pair> pairs = MatchEngine::autoPairs(cam.lastFrame, st, targetIre, nullptr, &notes);
            cam.notes << notes;
            if (st.lift || st.gamma || st.saturation)
                cam.notes << QStringLiteral("«Нейтральный баланс» ставит только баланс белого и уровень: лифт, гамма "
                                            "и насыщенность подбираются в режиме «Эталонная камера» (по общей области)");
            MatchEngine::Settings sn = st;
            sn.lift = sn.gamma = sn.saturation = false; // one grey level: nothing to fit them to
            finish(cam, pairs, sn);
        }
        break;
    }
    }

    refreshCorrectionsTable();
    QStringList all = problems + failures;
    if (m_loopPass > 0) {
        if (!failures.isEmpty()) {
            finishAutoMatchLoop(failures.join(QStringLiteral("; ")));
            QMessageBox::warning(this, QStringLiteral("Расчёт"), all.join(QLatin1Char('\n')));
            return;
        }
        continueAutoMatchLoop();
        return;
    }
    if (!all.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Расчёт"), all.join(QLatin1Char('\n')));
    int ready = 0;
    for (const CameraRow &c : m_cameras)
        if (c.hasProposal)
            ++ready;
    setCaptureStatus(QStringLiteral("Расчёт готов: %1 камер(ы). Проверьте таблицу и нажмите «Применить».").arg(ready));
}

bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    Q_UNUSED(watched);
    if (event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<const QKeyEvent *>(event);
        if (key->key() == Qt::Key_S && !key->modifiers()) {
            onTakeSnapshot();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (m_scopeDocks) {
        AtemAppSettings::setMainWindowDockState(m_scopeDocks->saveWindowState());
        m_scopeDocks->saveCustomLayout(ScopeDockManager::sessionLayoutName());
    }
    QMainWindow::closeEvent(event);
}

void MainWindow::onScopeComputeRequested()
{
    if (!m_scopeWorker || !m_scopeDocks || m_closing.load())
        return;
    if (!m_scopeDocks->takePendingCompute())
        return;
    if (!m_scopeWorker->tryAcquire())
        return; // previous frame still being computed: drop this one, never build a queue
    const auto pending = m_scopeDocks->pendingRequest();
    ScopeComputeRequest req;
    req.frameA = pending.frameA;
    req.frameB = pending.frameB;
    req.plotWidth = pending.plotWidth;
    req.plotHeight = pending.plotHeight;
    req.compareWipe = pending.compareWipe;
    for (const ScopeDockInstance &inst : pending.instruments) {
        ScopeInstrumentJob job;
        job.instanceId = inst.id;
        job.kind = inst.kind;
        job.settings = inst.settings;
        job.plotSize = inst.plotSize;
        req.instruments.push_back(job);
    }
    QMetaObject::invokeMethod(m_scopeWorker, "compute", Qt::QueuedConnection, Q_ARG(ScopeComputeRequest, req));
}

void MainWindow::onInstrumentResult(const QString &instanceId, const QImage &image, ScopeKind kind)
{
    if (m_scopeDocks)
        m_scopeDocks->onInstrumentResult(instanceId, image, kind);
}

void MainWindow::onScopePreviewOverlay(const QImage &overlay, bool enabled)
{
    if (m_previewOverlay)
        m_previewOverlay->setScopeOverlay(overlay, enabled);
}

void MainWindow::onTakeSnapshot()
{
    if (!m_scopeDocks || m_lastPreviewFrame.isNull())
        return;
    const QString path = m_scopeDocks->takeSnapshot(m_lastPreviewFrame, QString());
    QMessageBox::information(this, QStringLiteral("Снимок"),
                           QStringLiteral("Сохранено в:\n%1").arg(path));
}

void MainWindow::onInstrumentSettings(const QString &instanceId)
{
    if (!m_scopeDocks)
        return;
    for (const ScopeDockInstance &inst : m_scopeDocks->visibleInstruments()) {
        if (inst.id != instanceId || !inst.widget)
            continue;
        ScopeSettingsDialog dlg(inst.kind, inst.widget->settings(), this);
        if (dlg.exec() == QDialog::Accepted)
            m_scopeDocks->setInstrumentSettings(instanceId, dlg.result());
        break;
    }
}

void MainWindow::updateStreamStatusLabel()
{
    if (!m_statusStream)
        return;
    const QString device = m_deviceCombo ? m_deviceCombo->currentText() : QStringLiteral("—");
    const QString mode = m_modeCombo ? m_modeCombo->currentText() : QStringLiteral("—");
    const QString fps = m_fpsLabel ? m_fpsLabel->text() : QString();
    m_statusStream->setText(QStringLiteral("%1 | %2 | RGB 8 bpc | BT.709 | %3 | приборы %4 к/с")
                                .arg(device, mode, fps)
                                .arg(m_lastScopeFps, 0, 'f', 1));
}

void MainWindow::onScopeFpsUpdated(double fps)
{
    m_lastScopeFps = fps;
    updateStreamStatusLabel();
}

// ---------------------------------------------------------------------------------------------
// Matching engine settings / presets

namespace {

QString summarizeChange(const CameraCorrectionBundle &a, const CameraCorrectionBundle &b)
{
    QStringList parts;
    const auto f3 = [](float x, float y, float z) {
        return QStringLiteral("%1/%2/%3").arg(x, 0, 'f', 2).arg(y, 0, 'f', 2).arg(z, 0, 'f', 2);
    };
    const auto changed = [](float x, float y, float eps) { return std::abs(x - y) > eps; };
    if (changed(a.gainR, b.gainR, 0.005f) || changed(a.gainG, b.gainG, 0.005f) || changed(a.gainB, b.gainB, 0.005f))
        parts << QStringLiteral("Gain RGB %1→%2").arg(f3(a.gainR, a.gainG, a.gainB), f3(b.gainR, b.gainG, b.gainB));
    if (changed(a.gainY, b.gainY, 0.005f))
        parts << QStringLiteral("Уровень (Gain Y) %1→%2").arg(a.gainY, 0, 'f', 2).arg(b.gainY, 0, 'f', 2);
    if (changed(a.liftY, b.liftY, 0.003f))
        parts << QStringLiteral("Lift Y %1→%2").arg(a.liftY, 0, 'f', 3).arg(b.liftY, 0, 'f', 3);
    if (changed(a.gammaY, b.gammaY, 0.003f))
        parts << QStringLiteral("Gamma Y %1→%2").arg(a.gammaY, 0, 'f', 3).arg(b.gammaY, 0, 'f', 3);
    if (changed(a.liftR, b.liftR, 0.003f) || changed(a.liftG, b.liftG, 0.003f) || changed(a.liftB, b.liftB, 0.003f))
        parts << QStringLiteral("Lift %1→%2").arg(f3(a.liftR, a.liftG, a.liftB), f3(b.liftR, b.liftG, b.liftB));
    if (changed(a.gammaR, b.gammaR, 0.003f) || changed(a.gammaG, b.gammaG, 0.003f) || changed(a.gammaB, b.gammaB, 0.003f))
        parts << QStringLiteral("Gamma %1→%2").arg(f3(a.gammaR, a.gammaG, a.gammaB), f3(b.gammaR, b.gammaG, b.gammaB));
    if (changed(a.saturation, b.saturation, 0.005f))
        parts << QStringLiteral("Sat %1→%2").arg(a.saturation, 0, 'f', 2).arg(b.saturation, 0, 'f', 2);
    if (a.whiteBalanceKelvin != b.whiteBalanceKelvin || a.whiteBalanceTint != b.whiteBalanceTint)
        parts << QStringLiteral("ББ %1K/%2→%3K/%4")
                     .arg(a.whiteBalanceKelvin)
                     .arg(a.whiteBalanceTint)
                     .arg(b.whiteBalanceKelvin)
                     .arg(b.whiteBalanceTint);
    if (a.gainDb != b.gainDb)
        parts << QStringLiteral("Усиление %1→%2 dB").arg(a.gainDb).arg(b.gainDb);
    if (b.irisFStopAv >= 0 && changed(a.irisFStopAv, b.irisFStopAv, 0.02f))
        parts << QStringLiteral("Диафрагма f/%1→f/%2")
                     .arg(a.irisFStopAv >= 0 ? std::sqrt(std::pow(2.0, a.irisFStopAv)) : 0.0, 0, 'f', 1)
                     .arg(std::sqrt(std::pow(2.0, b.irisFStopAv)), 0, 'f', 1);
    return parts.isEmpty() ? QStringLiteral("без изменений") : parts.join(QStringLiteral("\n"));
}

} // namespace

MatchEngine::Settings MainWindow::engineSettings() const
{
    MatchEngine::Settings s;
    s.balance = m_chkBalance ? m_chkBalance->isChecked() : true;
    s.exposure = m_chkExposure ? m_chkExposure->isChecked() : true;
    s.lift = m_chkFitLift && m_chkFitLift->isChecked();
    s.gamma = m_chkFitGamma && m_chkFitGamma->isChecked();
    s.saturation = m_chkFitSat && m_chkFitSat->isChecked();
    const int bv = m_balanceViaCombo ? m_balanceViaCombo->currentData().toInt() : 3;
    s.balanceVia = bv == 1 ? MatchEngine::BalanceVia::WhiteBalance
                           : (bv == 0 ? MatchEngine::BalanceVia::CcGain : MatchEngine::BalanceVia::Auto);
    const int ev = m_exposureViaCombo ? m_exposureViaCombo->currentData().toInt() : 3;
    s.exposureVia = ev == 1   ? MatchEngine::ExposureVia::SensorGain
                    : ev == 2 ? MatchEngine::ExposureVia::Iris
                    : ev == 0 ? MatchEngine::ExposureVia::CcGain
                              : MatchEngine::ExposureVia::Auto;
    s.strength = (m_strengthSpin ? m_strengthSpin->value() : 100) / 100.0;
    // per-pass limits (the closed loop converges); off in the test mode «Без ограничений»
    s.liveLimits = !unlimitedMode();
    return s;
}

void MainWindow::loadEngineSettings()
{
    const auto v = [](const char *key, const QVariant &def) {
        return AtemAppSettings::value(QStringLiteral("match/") + QLatin1String(key), def);
    };
    const auto setBox = [](QCheckBox *b, bool on) {
        if (b) {
            QSignalBlocker blk(b);
            b->setChecked(on);
        }
    };
    setBox(m_chkBalance, v("balance", true).toBool());
    setBox(m_chkExposure, v("exposure", true).toBool());
    setBox(m_chkFitLift, v("lift", false).toBool());
    setBox(m_chkFitGamma, v("gamma", false).toBool());
    setBox(m_chkFitSat, v("saturation", false).toBool());
    setBox(m_chkAutoIris, v("autoIris", false).toBool());
    setBox(m_chkUnlimited, v("unlimited", true).toBool()); // test period: on by default
    const auto setCombo = [](QComboBox *c, const QVariant &data) {
        if (!c)
            return;
        QSignalBlocker blk(c);
        const int i = c->findData(data);
        if (i >= 0)
            c->setCurrentIndex(i);
    };
    // «Авто» (3) is the default; settings saved before it existed are switched to it once.
    const bool autoMigrated = v("autoMethodsV1", false).toBool();
    setCombo(m_balanceViaCombo, autoMigrated ? v("balanceVia", 3).toInt() : 3);
    setCombo(m_exposureViaCombo, autoMigrated ? v("exposureVia", 3).toInt() : 3);
    if (!autoMigrated) {
        AtemAppSettings::setValue(QStringLiteral("match/autoMethodsV1"), true);
        AtemAppSettings::setValue(QStringLiteral("match/balanceVia"), 3);
        AtemAppSettings::setValue(QStringLiteral("match/exposureVia"), 3);
    }
    setCombo(m_chartCombo, v("chart", QStringLiteral("cc24")).toString());
    setCombo(m_sampleTargetCombo, v("sampleTarget", QStringLiteral("reference")).toString());
    if (m_strengthSpin) {
        QSignalBlocker blk(m_strengthSpin);
        m_strengthSpin->setValue(v("strength", 100).toInt());
    }
    if (m_targetIreSpin) {
        QSignalBlocker blk(m_targetIreSpin);
        m_targetIreSpin->setValue(v("targetIre", 41.0).toDouble());
    }
}

void MainWindow::saveEngineSettings()
{
    const auto put = [](const char *key, const QVariant &val) {
        AtemAppSettings::setValue(QStringLiteral("match/") + QLatin1String(key), val);
    };
    if (m_chkBalance)
        put("balance", m_chkBalance->isChecked());
    if (m_chkExposure)
        put("exposure", m_chkExposure->isChecked());
    if (m_chkFitLift)
        put("lift", m_chkFitLift->isChecked());
    if (m_chkFitGamma)
        put("gamma", m_chkFitGamma->isChecked());
    if (m_chkFitSat)
        put("saturation", m_chkFitSat->isChecked());
    if (m_chkAutoIris)
        put("autoIris", m_chkAutoIris->isChecked());
    if (m_chkUnlimited)
        put("unlimited", m_chkUnlimited->isChecked());
    if (m_balanceViaCombo)
        put("balanceVia", m_balanceViaCombo->currentData());
    if (m_exposureViaCombo)
        put("exposureVia", m_exposureViaCombo->currentData());
    if (m_chartCombo)
        put("chart", m_chartCombo->currentData());
    if (m_sampleTargetCombo)
        put("sampleTarget", m_sampleTargetCombo->currentData());
    if (m_strengthSpin)
        put("strength", m_strengthSpin->value());
    if (m_targetIreSpin)
        put("targetIre", m_targetIreSpin->value());
}

void MainWindow::readCurrentCameraValues()
{
    const QVector<bool> active = captureActiveMask();
    for (int i = 0; i < m_cameras.size(); ++i) {
        CameraRow &cam = m_cameras[i];
        cam.current = CameraCorrectionBundle();
        cam.currentOk = false;
        if (!m_client || !m_client->isConnected() || !active.value(i, false))
            continue;
        CameraCorrectionBundle cur;
        const QString err = m_client->readCorrectionBundle(cam.binding.atemId, cur);
        if (err.isEmpty()) {
            cam.current = cur;
            cam.currentOk = true;
            qCInfo(lcRoute).noquote() << QStringLiteral("camera %1 now %2")
                                             .arg(cam.binding.label,
                                                  QString::fromUtf8(QJsonDocument(cur.toJson())
                                                                        .toJson(QJsonDocument::Compact)));
        } else {
            qCWarning(lcRoute).noquote() << QStringLiteral("read camera %1 failed: %2").arg(cam.binding.atemId).arg(err);
        }
    }
}

void MainWindow::onSaveCameraPreset()
{
    if (!m_client || !m_client->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("Пресеты"), QStringLiteral("Нет подключения к ATEM"));
        return;
    }
    syncCamerasFromTable();
    if (m_cameras.isEmpty())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Сохранить пресет камер"),
                                               QStringLiteral("Имя (например «День», «Вечер»):"),
                                               QLineEdit::Normal, QString(), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    if (CameraPresetStore::find(name, nullptr)
        && QMessageBox::question(this, QStringLiteral("Пресеты"),
                                 QStringLiteral("Пресет «%1» уже есть. Перезаписать?").arg(name))
               != QMessageBox::Yes)
        return;
    CameraPreset preset;
    preset.name = name;
    preset.saved = QDateTime::currentDateTime();
    QStringList failed;
    for (const CameraRow &cam : m_cameras) {
        CameraCorrectionBundle cur;
        const QString err = m_client->readCorrectionBundle(cam.binding.atemId, cur);
        if (!err.isEmpty()) {
            failed << cam.binding.label;
            continue;
        }
        preset.cameras.push_back({cam.binding.atemId, cam.binding.label, cur});
    }
    if (preset.cameras.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Пресеты"), QStringLiteral("Не удалось прочитать ни одну камеру."));
        return;
    }
    const QString err = CameraPresetStore::save(preset);
    if (!err.isEmpty()) {
        QMessageBox::warning(this, QStringLiteral("Пресеты"), err);
        return;
    }
    rebuildPresetMenu();
    statusBar()->showMessage(QStringLiteral("Пресет «%1» сохранён (%2 камер)%3")
                                 .arg(name)
                                 .arg(preset.cameras.size())
                                 .arg(failed.isEmpty() ? QString()
                                                       : QStringLiteral(", не прочитаны: ") + failed.join(", ")),
                             8000);
}

void MainWindow::applyCameraPreset(const QString &name)
{
    CameraPreset preset;
    if (!CameraPresetStore::find(name, &preset))
        return;
    if (!m_client || !m_client->isConnected()) {
        QMessageBox::warning(this, QStringLiteral("Пресеты"), QStringLiteral("Нет подключения к ATEM"));
        return;
    }
    syncCamerasFromTable();
    QStringList warnings;
    QVector<CameraCorrectionBundle> currents(preset.cameras.size());
    for (int i = 0; i < preset.cameras.size(); ++i) {
        const CameraPresetEntry &e = preset.cameras.at(i);
        if (m_client->readCorrectionBundle(e.atemId, currents[i]).isEmpty()) {
            const QStringList w = MatchEngine::sanityWarnings(currents[i], e.bundle);
            if (!w.isEmpty())
                warnings << QStringLiteral("%1: %2").arg(e.label, w.join(QStringLiteral("; ")));
        }
    }
    if (!warnings.isEmpty()
        && QMessageBox::warning(this, QStringLiteral("Пресет «%1»").arg(name),
                                QStringLiteral("Пресет сильно отличается от текущих настроек:\n%1\n\nПрименить?")
                                    .arg(warnings.join(QLatin1Char('\n'))),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No)
               != QMessageBox::Yes)
        return;
    QStringList errors;
    int sent = 0;
    for (const CameraPresetEntry &e : preset.cameras) {
        CameraCorrectionBundle previous;
        const QString err = m_client->applyCorrectionBundle(e.atemId, e.bundle, &previous, nullptr,
                                                            QStringLiteral("preset"));
        if (!err.isEmpty()) {
            errors << QStringLiteral("%1: %2").arg(e.label, err);
            continue;
        }
        ++sent;
        for (CameraRow &cam : m_cameras)
            if (cam.binding.atemId == e.atemId && !cam.hasRollback) {
                cam.rollback = previous;
                cam.hasRollback = true;
            }
    }
    AtemAppSettings::setValue(QStringLiteral("presets/last"), name);
    qCInfo(lcRoute).noquote() << QStringLiteral("preset '%1' applied to %2 camera(s), errors: %3")
                                     .arg(name)
                                     .arg(sent)
                                     .arg(errors.size());
    if (!errors.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Пресеты"), errors.join(QLatin1Char('\n')));
    statusBar()->showMessage(QStringLiteral("Пресет «%1» применён к %2 камерам (откат доступен)").arg(name).arg(sent), 8000);
}

void MainWindow::manageCameraPresets()
{
    const QStringList names = CameraPresetStore::names();
    if (names.isEmpty()) {
        QMessageBox::information(this, QStringLiteral("Пресеты"), QStringLiteral("Пресетов пока нет."));
        return;
    }
    bool ok = false;
    const QString picked = QInputDialog::getItem(this, QStringLiteral("Пресеты камер"), QStringLiteral("Пресет:"),
                                                 names, 0, false, &ok);
    if (!ok)
        return;
    QMessageBox box(this);
    box.setWindowTitle(QStringLiteral("Пресет «%1»").arg(picked));
    box.setText(QStringLiteral("Что сделать с пресетом?"));
    QPushButton *ren = box.addButton(QStringLiteral("Переименовать"), QMessageBox::ActionRole);
    QPushButton *del = box.addButton(QStringLiteral("Удалить"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.exec();
    QString err;
    if (box.clickedButton() == ren) {
        const QString to = QInputDialog::getText(this, QStringLiteral("Переименовать"), QStringLiteral("Новое имя:"),
                                                 QLineEdit::Normal, picked, &ok)
                               .trimmed();
        if (ok && !to.isEmpty() && to != picked)
            err = CameraPresetStore::rename(picked, to);
    } else if (box.clickedButton() == del) {
        if (QMessageBox::question(this, QStringLiteral("Удалить"), QStringLiteral("Удалить пресет «%1»?").arg(picked))
            == QMessageBox::Yes)
            err = CameraPresetStore::remove(picked);
    }
    if (!err.isEmpty())
        QMessageBox::warning(this, QStringLiteral("Пресеты"), err);
    rebuildPresetMenu();
}

void MainWindow::rebuildPresetMenu()
{
    if (!m_presetMenu)
        return;
    m_presetMenu->clear();
    const QStringList names = CameraPresetStore::names();
    for (const QString &name : names)
        m_presetMenu->addAction(QStringLiteral("Применить «%1»").arg(name), this,
                                [this, name] { applyCameraPreset(name); });
    if (!names.isEmpty())
        m_presetMenu->addSeparator();
    m_presetMenu->addAction(QStringLiteral("Сохранить текущие настройки камер…"), this,
                            &MainWindow::onSaveCameraPreset);
    m_presetMenu->addAction(QStringLiteral("Переименовать / удалить…"), this, &MainWindow::manageCameraPresets);
    if (m_presetCombo) {
        QSignalBlocker blk(m_presetCombo);
        m_presetCombo->clear();
        m_presetCombo->addItem(QStringLiteral("— пресет камер —"), QString());
        for (const QString &name : names)
            m_presetCombo->addItem(name, name);
    }
}

void MainWindow::applyWorkspacePreset(const QString &id)
{
    if (!m_scopeDocks)
        return;
    m_scopeDocks->applyLayoutPreset(id);
    if (m_cameraDock) {
        m_cameraDock->setFloating(false);
        addDockWidget(Qt::BottomDockWidgetArea, m_cameraDock);
        m_cameraDock->show();
    }
    if (m_sourceDock)
        m_sourceDock->setVisible(id != QLatin1String("home"));
    // Give the space to the picture and the scopes instead of the (empty) central widget.
    QTimer::singleShot(400, this, [this] {
        if (!m_scopeDocks)
            return;
        const int w = width();
        const int h = height();
        QList<QDockWidget *> scopes;
        for (QDockWidget *d : findChildren<QDockWidget *>())
            if (d->objectName().startsWith(QLatin1String("scope_")) && d->isVisible() && !d->isFloating())
                scopes << d;
        if (QDockWidget *pv = m_scopeDocks->previewDock())
            resizeDocks({pv}, {w * 42 / 100}, Qt::Horizontal);
        if (!scopes.isEmpty()) {
            resizeDocks({scopes.first()}, {w * 26 / 100}, Qt::Horizontal);
            QList<int> heights;
            for (int i = 0; i < scopes.size(); ++i)
                heights << h / scopes.size();
            resizeDocks(scopes, heights, Qt::Vertical);
        }
        if (m_cameraDock && m_cameraDock->isVisible())
            resizeDocks({m_cameraDock}, {h * 70 / 100}, Qt::Vertical);
    });
}
