#pragma once

#include <QDialog>

class QLineEdit;
class QLabel;

class AtemPathsSetupDialog final : public QDialog
{
    Q_OBJECT
public:
  explicit AtemPathsSetupDialog(QWidget *parent = nullptr);

  QString sdkPath() const;
  QString softwareControlPath() const;

  void setSdkPath(const QString &path);
  void setSoftwareControlPath(const QString &path);

  void applyAutoDetection();

private Q_SLOTS:
  void onBrowseSdk();
  void onBrowseSoftwareControl();
  void onAutoDetect();
  void onPathEdited();

private:
  void refreshValidityIndicators();
  void runAutoDetect();

  QLineEdit *m_sdkEdit = nullptr;
  QLineEdit *m_softwareControlEdit = nullptr;
  QLabel *m_sdkStatus = nullptr;
  QLabel *m_softwareControlStatus = nullptr;
};