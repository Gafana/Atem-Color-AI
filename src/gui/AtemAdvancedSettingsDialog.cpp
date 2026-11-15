#include "AtemAdvancedSettingsDialog.h"

#include "platform/AtemPathValidation.h"

#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

static QLabel *makeStatusLabel(QWidget *parent)
{
    auto *label = new QLabel(parent);
    label->setWordWrap(true);
    return label;
}

static void styleValidity(QLabel *label, bool valid, const QString &detail)
{
    const QString icon = valid ? QStringLiteral("✓") : QStringLiteral("✗");
    const QString color = valid ? QStringLiteral("#1a7f37") : QStringLiteral("#b54708");
    label->setText(QStringLiteral("<span style=\"color:%1;font-weight:600\">%2</span> %3")
                       .arg(color, icon, detail.toHtmlEscaped()));
}

AtemAdvancedSettingsDialog::AtemAdvancedSettingsDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Дополнительно"));
    setMinimumWidth(560);
    resize(620, 280);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        QStringLiteral(
            "<b>Путь к ATEM SDK</b> (необязательно). SDK нужен при <i>сборке</i> проекта "
            "(MIDL, заголовки <code>BMDSwitcherAPI.h</code> / <code>.idl</code>). "
            "В рантайме приложение общается с пультом через COM <b>ATEM Software Control</b>; "
            "неверный или пустой путь к SDK <b>не блокирует</b> работу с ATEM. "
            "Поле служит для локальной диагностики и сверки с деревом SDK на машине разработчика."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout();

    auto *sdkRow = new QHBoxLayout();
    m_sdkEdit = new QLineEdit(this);
    m_sdkEdit->setPlaceholderText(QStringLiteral("Каталог Blackmagic ATEM Switchers SDK"));
    auto *sdkBrowse = new QPushButton(QStringLiteral("Обзор…"), this);
    connect(sdkBrowse, &QPushButton::clicked, this, &AtemAdvancedSettingsDialog::onBrowseSdk);
    sdkRow->addWidget(m_sdkEdit, 1);
    sdkRow->addWidget(sdkBrowse);
    form->addRow(QStringLiteral("Путь к ATEM SDK:"), sdkRow);
    m_sdkStatus = makeStatusLabel(this);
    form->addRow(QString(), m_sdkStatus);

    layout->addLayout(form);

    auto *detectRow = new QHBoxLayout();
    auto *autoBtn = new QPushButton(QStringLiteral("Автоопределение SDK"), this);
    connect(autoBtn, &QPushButton::clicked, this, &AtemAdvancedSettingsDialog::onAutoDetectSdk);
    detectRow->addWidget(autoBtn);
    detectRow->addStretch();
    layout->addLayout(detectRow);

    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(m_sdkEdit, &QLineEdit::textChanged, this, &AtemAdvancedSettingsDialog::onPathEdited);

    refreshValidityIndicators();
}

QString AtemAdvancedSettingsDialog::sdkPath() const
{
    return m_sdkEdit->text().trimmed();
}

void AtemAdvancedSettingsDialog::setSdkPath(const QString &path)
{
    m_sdkEdit->setText(path);
}

void AtemAdvancedSettingsDialog::onBrowseSdk()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Каталог ATEM SDK"), m_sdkEdit->text());
    if (!dir.isEmpty())
        m_sdkEdit->setText(dir);
}

void AtemAdvancedSettingsDialog::onAutoDetectSdk()
{
    const QString sdk = AtemPathValidation::detectLikelySdkPath();
    if (!sdk.isEmpty())
        m_sdkEdit->setText(sdk);
    refreshValidityIndicators();
}

void AtemAdvancedSettingsDialog::onPathEdited()
{
    refreshValidityIndicators();
}

void AtemAdvancedSettingsDialog::refreshValidityIndicators()
{
    const auto sdk = AtemPathValidation::validateSdkPath(m_sdkEdit->text());
    if (m_sdkEdit->text().trimmed().isEmpty())
        styleValidity(m_sdkStatus, false, QStringLiteral("Не задан — диагностика SDK отключена"));
    else
        styleValidity(m_sdkStatus, sdk.valid, sdk.detail);
}