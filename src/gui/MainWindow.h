#pragma once

#include "atem/AtemClient.h"
#include "capture/VideoDevice.h"
#include "color/ColorCheckerData.h"
#include "color/ColorMatchMode.h"
#include "color/ColorMatching.h"
#include "color/CorrectionParameterMask.h"
#include "color/MatchEngine.h"
#include "platform/AtemAppSettings.h"
#include "scopes/ScopeTypes.h"

#include <QMainWindow>

#include <atomic>
#include <functional>
#include <memory>

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QSpinBox;
class QTableWidget;
class ScopeDockManager;
class QDockWidget;
class ScopeComputeWorker;
class QThread;
class CaptureJob;
class DeviceScanThread;
class GstLivePreview;

struct CaptureRequest
{
    QString route;
    qint64 auxId = 0;
    QString layout;
    bool pgmAllowed = false;
    QVector<CameraBinding> cameras;
    int referenceIndex = 0;
};

struct CaptureOutcome
{
    QString fatal;
    QVector<FrameColorAnalysis> analysis;
    QVector<QImage> images;
    QVector<QString> errors;
};

class MainWindow final : public QMainWindow
{
    Q_OBJECT
    friend class CaptureJob;

public:
    explicit MainWindow(std::unique_ptr<AtemClient> client, const QString &atemIp,
                        QWidget *parent = nullptr);
    ~MainWindow() override;

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private Q_SLOTS:
    void onScopeComputeRequested();
    void onInstrumentResult(const QString &instanceId, const QImage &image, ScopeKind kind);
    void onScopePreviewOverlay(const QImage &overlay, bool enabled);
    void onTakeSnapshot();
    void onInstrumentSettings(const QString &instanceId);
    void onScopeFpsUpdated(double fps);
    void onCaptureAnalyze();
    void onApply();
    void onReferenceChanged(int index);
    void onConnectClicked();
    void onDeviceChanged(int index);
    void onVideoOptionChanged();
    void onRouteChanged();
    void onAuxChanged(int index);
    void onLayoutChanged(int index);
    void onRefreshDevices();
    void onDevicesReady();
    void onPreviewFrame(const QImage &image);
    void onPreviewAnalysis(const FrameColorAnalysis &analysis);
    void onPreviewError(const QString &message);
    void onAddCamera();
    void onRemoveCamera();
    void onMappingEdited();
    void onTuningModeChanged(int index);
    void onSaveReferenceFrame();
    void onLoadReferenceFrame();
    void onMarkCameraChanged(int index);
    void onGrayRoiEdited(const QRectF &roi);
    void onCheckerRoiEdited(const QVector<QPointF> &corners);
    void onParamMaskChanged();

private:
    struct CameraRow
    {
        CameraBinding binding;
        FrameColorAnalysis analysis;
        CameraMatchResult match;
        CameraCorrectionBundle rollback;
        bool hasRollback = false;
        bool includeInTuning = true;
        QImage lastFrame;
        QRectF grayRoi;
        QVector<QPointF> checkerCorners;
        QVector<ColorCheckerPatch> checkerPatches;
        // MatchEngine result (relative to the values read from the camera before the calculation)
        CameraCorrectionBundle current;
        bool currentOk = false;
        CameraCorrectionBundle proposed;
        bool hasProposal = false;
        QStringList notes;
        QStringList warnings;
        double errBefore = 0, errAfter = 0, deBefore = -1, deAfter = -1;
    };

    void rebuildAtemAvailabilityUi();
    void openSettingsDialog();
    void openAdvancedSettingsDialog();
    void connectAtem(bool persistIp);
    void refreshAuxList();
    void loadRouteControls();
    void updateRouteWidgets();
    void updateWindowSpinRange();
    void loadMappingFromSettings();
    void seedMappingFromInputs();
    void reloadMappingCombos();
    void addMapRow(const CameraBinding &binding);
    CameraBinding bindingFromRow(int row) const;
    void saveMapping();
    void syncCamerasFromTable();
    void fillInputCombo(QComboBox *combo, qint64 selectedId) const;
    void applyDeviceList();
    void populateDeviceOptions();
    void startPreview();
    void setBusy(bool busy);
    void refreshCorrectionsTable();
    void finishCapture(CaptureJob *job);
    void runMatchingAfterCapture(const QStringList &problems);
    void loadTuningUi();
    void saveCameraRois();
    void loadCameraRois();
    void applyOverlayForMarkCamera();
    void updateTuningPanelVisibility();
    MatchEngine::Settings engineSettings() const;
    void loadEngineSettings();
    void saveEngineSettings();
    void readCurrentCameraValues();
    void onSaveCameraPreset();
    void applyCameraPreset(const QString &name);
    void manageCameraPresets();
    void rebuildPresetMenu();
    void applyWorkspacePreset(const QString &id);
    FrameColorAnalysis averageLiveAnalysis(const QRect &crop, const std::atomic<bool> *cancel) const;
    bool confirmProgramSwitch();
    QString currentRoute() const;
    qint64 currentAuxId() const;
    QString currentLayout() const;
    QString callOnGuiThread(const std::function<QString()> &fn);
    void setCaptureStatus(const QString &text);
    void executeCapture(const CaptureRequest &request, const std::atomic<bool> *cancel,
                        CaptureOutcome *outcome);
    void updateStreamStatusLabel();

    std::unique_ptr<AtemClient> m_client;
    QVector<AtemInputInfo> m_inputs;
    QVector<AtemAuxOutput> m_auxes;
    QVector<VideoDevice> m_devices;
    QVector<CameraRow> m_cameras;
    int m_referenceIndex = 0;
    bool m_loading = false;
    bool m_busy = false;
    bool m_ipEdited = false;
    bool m_previewRunning = false;
    qint64 m_lastPreviewFrameMs = 0;   // wall clock of the last preview frame (stall detection)
    bool m_previewStalled = false;
    int m_previewRetry = 0;            // consecutive automatic restarts after a capture error
    qint64 m_previewRetryAtMs = 0;     // when the next automatic restart is due (0 = none)
    std::atomic<bool> m_closing{false};

    QLineEdit *m_ipEdit = nullptr;
    QLabel *m_linkStatus = nullptr;
    QLabel *m_videoStatus = nullptr;
    QLabel *m_fpsLabel = nullptr;
    QLabel *m_routeHelp = nullptr;
    QLabel *m_configBanner = nullptr;
    QComboBox *m_deviceCombo = nullptr;
    QComboBox *m_inputCombo = nullptr;
    QComboBox *m_modeCombo = nullptr;
    QComboBox *m_routeCombo = nullptr;
    QComboBox *m_auxCombo = nullptr;
    QComboBox *m_layoutCombo = nullptr;
    QComboBox *m_referenceCombo = nullptr;
    QComboBox *m_tuningModeCombo = nullptr;
    QComboBox *m_markCameraCombo = nullptr;
    QSpinBox *m_grayTargetSpin = nullptr;
    QTableWidget *m_mapTable = nullptr;
    QTableWidget *m_corrTable = nullptr;
    QWidget *m_sourcePanel = nullptr;
    QWidget *m_cameraPanel = nullptr;
    QDockWidget *m_sourceDock = nullptr;
    QDockWidget *m_cameraDock = nullptr;
    ScopeDockManager *m_scopeDocks = nullptr;
    QLabel *m_statusStream = nullptr;
    QLabel *m_statusQc = nullptr;
    QLabel *m_statusFps = nullptr;
    ScopeComputeWorker *m_scopeWorker = nullptr;
    QThread *m_scopeThread = nullptr;
    QPushButton *m_btnApply = nullptr;
    QPushButton *m_btnCapture = nullptr;
    QPushButton *m_btnRollback = nullptr;
    QPushButton *m_btnConnect = nullptr;
    QPushButton *m_btnSaveRef = nullptr;
    QPushButton *m_btnLoadRef = nullptr;
    class PreviewOverlayWidget *m_previewOverlay = nullptr;
    class QCheckBox *m_chkWhiteBalance = nullptr;
    class QCheckBox *m_chkTint = nullptr;
    class QCheckBox *m_chkLift = nullptr;
    class QCheckBox *m_chkGamma = nullptr;
    class QCheckBox *m_chkGain = nullptr;
    class QCheckBox *m_chkLevel = nullptr;
    class QCheckBox *m_chkBalance = nullptr;
    class QCheckBox *m_chkExposure = nullptr;
    class QCheckBox *m_chkFitLift = nullptr;
    class QCheckBox *m_chkFitGamma = nullptr;
    class QCheckBox *m_chkFitSat = nullptr;
    class QCheckBox *m_chkAutoIris = nullptr;
    QComboBox *m_balanceViaCombo = nullptr;
    QComboBox *m_exposureViaCombo = nullptr;
    QComboBox *m_chartCombo = nullptr;
    QComboBox *m_sampleTargetCombo = nullptr;
    QComboBox *m_presetCombo = nullptr;
    class QDoubleSpinBox *m_targetIreSpin = nullptr;
    QSpinBox *m_strengthSpin = nullptr;
    QLabel *m_modeHint = nullptr;
    QLabel *m_refLabel = nullptr;
    QLabel *m_markLabel = nullptr;
    class QMenu *m_presetMenu = nullptr;
    bool m_atemWasConnected = false;

    ColorMatchMode m_tuningMode = ColorMatchMode::ReferenceCamera;
    CorrectionParameterMask m_paramMask = CorrectionParameterMask::allEnabled();
    FrameColorAnalysis m_referenceFrameAnalysis;
    QString m_referenceFramePath;
    QImage m_referenceFrameImage;
    QImage m_lastPreviewFrame;

    qint64 m_fpsStamp = 0;
    int m_previewFrames = 0;
    double m_lastScopeFps = 0.0;
    QString m_streamInfo;

    CaptureJob *m_captureJob = nullptr;
    DeviceScanThread *m_deviceScan = nullptr;
#ifdef HAVE_GSTREAMER
    GstLivePreview *m_preview = nullptr;
#endif
};
