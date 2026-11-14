#pragma once

#include "atem/AtemClient.h"
#include "color/ColorMatching.h"

#include <QHash>
#include <QMainWindow>
#include <memory>

class AtemClient;
class HistogramWidget;
class QTableWidget;
class QComboBox;
class QLabel;
class QPushButton;
class VectorscopeWidget;

class MainWindow final : public QMainWindow
{
    Q_OBJECT
public:
    explicit MainWindow(std::unique_ptr<AtemClient> client, QWidget *parent = nullptr);

private Q_SLOTS:
    void onCaptureAnalyze();
    void onApply();
    void onReferenceChanged(int index);

private:
    struct CameraRow
    {
        qint64 atemId = 0;
        QString label;
        QString sourceUri;
        FrameColorAnalysis analysis;
        CameraMatchResult match;
        CameraCorrectionBundle rollback;
        bool hasRollback = false;
    };

    bool loadFrameForCamera(int index, QImage &image);
    void refreshCorrectionsTable();
    void updatePlots();
    void rebuildAtemAvailabilityUi();
    void openSettingsDialog();

    std::unique_ptr<AtemClient> m_client;
    QVector<AtemInputInfo> m_inputs;
    QVector<CameraRow> m_cameras;
    int m_referenceIndex = 0;

    QComboBox *m_referenceCombo = nullptr;
    QTableWidget *m_sourceTable = nullptr;
    QTableWidget *m_corrTable = nullptr;
    HistogramWidget *m_hist = nullptr;
    VectorscopeWidget *m_vectorscope = nullptr;
    QLabel *m_configBanner = nullptr;
    QPushButton *m_btnApply = nullptr;
    QHash<qint64, CameraCorrectionBundle> m_lastRollback;
};