#include "platform/AtemAppSettings.h"
#include "platform/AtemPathValidation.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QtTest>

class TestAtemPaths : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase();
    void sdkValidationFindsHeaderInWinSubdir();
    void sdkValidationRejectsMissingFiles();
    void softwareControlValidationDll();
    void softwareControlValidationExe();
    void settingsRoundTrip();
    void controlSavePreservesSdkPath();
};

void TestAtemPaths::initTestCase()
{
    QCoreApplication::setApplicationName(QStringLiteral("atem-color-ai"));
    QCoreApplication::setOrganizationName(QStringLiteral("Gafana"));
    QStandardPaths::setTestModeEnabled(true);
}

void TestAtemPaths::sdkValidationFindsHeaderInWinSubdir()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QDir root(tmp.path());
    QDir(root.filePath(QStringLiteral("Win"))).mkpath(QStringLiteral("."));
    QFile header(root.filePath(QStringLiteral("Win/BMDSwitcherAPI.h")));
    QVERIFY(header.open(QIODevice::WriteOnly));
    header.write("// test\n");
    header.close();

    const auto r = AtemPathValidation::validateSdkPath(tmp.path());
    QVERIFY(r.valid);
    QVERIFY(r.detail.contains(QStringLiteral("BMDSwitcherAPI.h")));
}

void TestAtemPaths::sdkValidationRejectsMissingFiles()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const auto r = AtemPathValidation::validateSdkPath(tmp.path());
    QVERIFY(!r.valid);
}

void TestAtemPaths::softwareControlValidationDll()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QFile dll(QDir(tmp.path()).filePath(QStringLiteral("BMDSwitcherAPI.dll")));
    QVERIFY(dll.open(QIODevice::WriteOnly));
    dll.write("x");
    dll.close();

    const auto r = AtemPathValidation::validateSoftwareControlPath(tmp.path());
    QVERIFY(r.valid);
}

void TestAtemPaths::softwareControlValidationExe()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    QFile exe(QDir(tmp.path()).filePath(QStringLiteral("ATEM Software Control.exe")));
    QVERIFY(exe.open(QIODevice::WriteOnly));
    exe.write("x");
    exe.close();

    const auto r = AtemPathValidation::validateSoftwareControlPath(exe.fileName());
    QVERIFY(r.valid);
}

void TestAtemPaths::settingsRoundTrip()
{
    AtemAppSettings::reset();
    AtemAppSettings::save(QStringLiteral("/sdk/path"), QStringLiteral("/control/path"), true);

    QString sdk;
    QString control;
    bool done = false;
    AtemAppSettings::load(&sdk, &control, &done);
    QCOMPARE(sdk, QStringLiteral("/sdk/path"));
    QCOMPARE(control, QStringLiteral("/control/path"));
    QVERIFY(done);

    QVERIFY(QFile::exists(AtemAppSettings::settingsFilePath()));
    AtemAppSettings::reset();
    QVERIFY(!QFile::exists(AtemAppSettings::settingsFilePath()));
}

void TestAtemPaths::controlSavePreservesSdkPath()
{
    AtemAppSettings::reset();
    AtemAppSettings::save(QStringLiteral("/sdk/kept"), QStringLiteral("/control/old"), true);

    QString sdk;
    QString control;
    bool done = false;
    AtemAppSettings::load(&sdk, &control, &done);
    QCOMPARE(sdk, QStringLiteral("/sdk/kept"));
    QCOMPARE(control, QStringLiteral("/control/old"));
    QVERIFY(done);

    // As in «Пути ATEM…» after first-run refactor: only Software Control is edited; sdkPath stays.
    AtemAppSettings::save(sdk, QStringLiteral("/control/new"), true);
    AtemAppSettings::load(&sdk, &control, &done);
    QCOMPARE(sdk, QStringLiteral("/sdk/kept"));
    QCOMPARE(control, QStringLiteral("/control/new"));

    const auto sdkCheck = AtemPathValidation::validateSdkPath(sdk);
    QVERIFY(!sdkCheck.valid);
}

QTEST_MAIN(TestAtemPaths)
#include "tst_atem_paths.moc"