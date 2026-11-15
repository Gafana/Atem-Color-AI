#include "AtemPathsSetupDialog.h"

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

AtemPathsSetupDialog::AtemPathsSetupDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QStringLiteral("Настройка путей ATEM"));
    setMinimumWidth(560);
    resize(620, 320);

    auto *layout = new QVBoxLayout(this);

    auto *intro = new QLabel(
        QStringLiteral(
            "Укажите каталоги Blackmagic. <b>ATEM Software Control</b> нужен в работе: "
            "через его COM-сервер приложение подключается к пульту (ищется "
            "<code>BMDSwitcherAPI.dll</code>). "
            "<b>ATEM SDK</b> используется при <i>сборке</i> (MIDL); в рантайме путь к SDK нужен "
            "только для проверки и диагностики — заголовки <code>BMDSwitcherAPI.h</code> / "
            "<code>.idl</code>."),
        this);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    auto *form = new QFormLayout();

    auto *sdkRow = new QHBoxLayout();
    m_sdkEdit = new QLineEdit(this);
    m_sdkEdit->setPlaceholderText(QStringLiteral("Каталог Blackmagic ATEM Switchers SDK"));
    auto *sdkBrowse = new QPushButton(QStringLiteral("Обзор…"), this);
    connect(sdkBrowse, &QPushButton::clicked, this, &AtemPathsSetupDialog::onBrowseSdk);
    sdkRow->addWidget(m_sdkEdit, 1);
    sdkRow->addWidget(sdkBrowse);
    form->addRow(QStringLiteral("Путь к ATEM SDK:"), sdkRow);
    m_sdkStatus = makeStatusLabel(this);
    form->addRow(QString(), m_sdkStatus);

    auto *scRow = new QHBoxLayout();
    m_softwareControlEdit = new QLineEdit(this);
    m_softwareControlEdit->setPlaceholderText(
        QStringLiteral("Папка установки или ATEM Software Control.exe"));
    auto *scBrowse = new QPushButton(QStringLiteral("Обзор…"), this);
    connect(scBrowse, &QPushButton::clicked, this, &AtemPathsSetupDialog::onBrowseSoftwareControl);
    scRow->addWidget(m_softwareControlEdit, 1);
    scRow->addWidget(scBrowse);
    form->addRow(QStringLiteral("ATEM Software Control:"), scRow);
    m_softwareControlStatus = makeStatusLabel(this);
    form->addRow(QString(), m_softwareControlStatus);

    layout->addLayout(form);

    auto *detectRow = new QHBoxLayout();
    auto *autoBtn = new QPushButton(QStringLiteral("Автоопределение"), this);
    connect(autoBtn, &QPushButton::clicked, this, &AtemPathsSetupDialog::onAutoDetect);
    detectRow->addWidget(autoBtn);
    detectRow->addStretch();
    layout->addLayout(detectRow);

    auto *buttons = new QDialogButtonBox(this);
    auto *saveBtn = buttons->addButton(QStringLiteral("Сохранить"), QDialogButtonBox::AcceptRole);
    auto *skipBtn = buttons->addButton(QStringLiteral("Пропустить"), QDialogButtonBox::RejectRole);
    Q_UNUSED(skipBtn);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    Q_UNUSED(saveBtn);

    connect(m_sdkEdit, &QLineEdit::textChanged, this, &AtemPathsSetupDialog::onPathEdited);
    connect(m_softwareControlEdit, &QLineEdit::textChanged, this,
            &AtemPathsSetupDialog::onPathEdited);

    refreshValidityIndicators();
}

QString AtemPathsSetupDialog::sdkPath() const
{
    return m_sdkEdit->text().trimmed();
}

QString AtemPathsSetupDialog::softwareControlPath() const
{
    return m_softwareControlEdit->text().trimmed();
}

void AtemPathsSetupDialog::setSdkPath(const QString &path)
{
    m_sdkEdit->setText(path);
}

void AtemPathsSetupDialog::setSoftwareControlPath(const QString &path)
{
    m_softwareControlEdit->setText(path);
}

void AtemPathsSetupDialog::applyAutoDetection()
{
    runAutoDetect();
}

void AtemPathsSetupDialog::onBrowseSdk()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Каталог ATEM SDK"), m_sdkEdit->text());
    if (!dir.isEmpty())
        m_sdkEdit->setText(dir);
}

void AtemPathsSetupDialog::onBrowseSoftwareControl()
{
    const QString path = QFileDialog::getOpenFileName(
        this, QStringLiteral("ATEM Software Control"),
        m_softwareControlEdit->text(),
        QStringLiteral("Программа (*.exe);;Все файлы (*.*)"));
    if (!path.isEmpty()) {
        m_softwareControlEdit->setText(path);
        return;
    }
    const QString dir = QFileDialog::getExistingDirectory(
        this, QStringLiteral("Папка установки ATEM Software Control"),
        m_softwareControlEdit->text());
    if (!dir.isEmpty())
        m_softwareControlEdit->setText(dir);
}

void AtemPathsSetupDialog::onAutoDetect()
{
    runAutoDetect();
}

void AtemPathsSetupDialog::runAutoDetect()
{
    const QString sc = AtemPathValidation::detectInstalledSoftwareControlPath();
    if (!sc.isEmpty())
        m_softwareControlEdit->setText(sc);

    if (m_sdkEdit->text().trimmed().isEmpty()) {
        const QString sdk = AtemPathValidation::detectLikelySdkPath();
        if (!sdk.isEmpty())
            m_sdkEdit->setText(sdk);
    }
    refreshValidityIndicators();
}

void AtemPathsSetupDialog::onPathEdited()
{
    refreshValidityIndicators();
}

void AtemPathsSetupDialog::refreshValidityIndicators()
{
    const auto sdk = AtemPathValidation::validateSdkPath(m_sdkEdit->text());
    if (m_sdkEdit->text().trimmed().isEmpty())
        styleValidity(m_sdkStatus, false, QStringLiteral("Не задан (диагностика SDK)"));
    else
        styleValidity(m_sdkStatus, sdk.valid, sdk.detail);

    const auto sc = AtemPathValidation::validateSoftwareControlPath(m_softwareControlEdit->text());
    if (m_softwareControlEdit->text().trimmed().isEmpty())
        styleValidity(m_softwareControlStatus, false, QStringLiteral("Не задан — функции ATEM недоступны"));
    else
        styleValidity(m_softwareControlStatus, sc.valid, sc.detail);
}