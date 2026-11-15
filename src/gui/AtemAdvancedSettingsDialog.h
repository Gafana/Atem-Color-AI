#pragma once

#include <QDialog>

class QLineEdit;
class QLabel;

/** Optional ATEM SDK path for developers (build/MIDL diagnostics only; not required at runtime). */
class AtemAdvancedSettingsDialog final : public QDialog
{
    Q_OBJECT
public:
  explicit AtemAdvancedSettingsDialog(QWidget *parent = nullptr);

  QString sdkPath() const;
  void setSdkPath(const QString &path);

private Q_SLOTS:
  void onBrowseSdk();
  void onAutoDetectSdk();
  void onPathEdited();

private:
  void refreshValidityIndicators();

  QLineEdit *m_sdkEdit = nullptr;
  QLabel *m_sdkStatus = nullptr;
};