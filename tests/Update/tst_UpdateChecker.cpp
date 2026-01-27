#include <QtTest>
#include <QVersionNumber>
#include "update/UpdateChecker.h"

/**
 * @brief Unit tests for UpdateChecker class.
 *
 * Tests version comparison logic and other utility functions.
 * Network-dependent tests are excluded from unit tests.
 */
class tst_UpdateChecker : public QObject
{
    Q_OBJECT

private slots:
    // Version comparison tests
    void testIsNewerVersion_BasicComparison();
    void testIsNewerVersion_MajorVersionDiff();
    void testIsNewerVersion_MinorVersionDiff();
    void testIsNewerVersion_PatchVersionDiff();
    void testIsNewerVersion_SameVersion();
    void testIsNewerVersion_OlderVersion();
    void testIsNewerVersion_InvalidVersions();
    void testIsNewerVersion_ThreeDigitVersions();

    // Current version tests
    void testCurrentVersion_NotEmpty();

    // Install source tests
    void testInstallSource_Valid();
};

// ============================================================================
// Version comparison tests
// ============================================================================

void tst_UpdateChecker::testIsNewerVersion_BasicComparison()
{
    QVERIFY(UpdateChecker::isNewerVersion("1.0.1", "1.0.0"));
    QVERIFY(UpdateChecker::isNewerVersion("1.1.0", "1.0.0"));
    QVERIFY(UpdateChecker::isNewerVersion("2.0.0", "1.0.0"));
}

void tst_UpdateChecker::testIsNewerVersion_MajorVersionDiff()
{
    QVERIFY(UpdateChecker::isNewerVersion("2.0.0", "1.9.9"));
    QVERIFY(UpdateChecker::isNewerVersion("3.0.0", "2.99.99"));
    QVERIFY(UpdateChecker::isNewerVersion("10.0.0", "9.9.9"));
}

void tst_UpdateChecker::testIsNewerVersion_MinorVersionDiff()
{
    QVERIFY(UpdateChecker::isNewerVersion("1.2.0", "1.1.0"));
    QVERIFY(UpdateChecker::isNewerVersion("1.10.0", "1.9.0"));
    QVERIFY(UpdateChecker::isNewerVersion("1.99.0", "1.98.0"));
}

void tst_UpdateChecker::testIsNewerVersion_PatchVersionDiff()
{
    QVERIFY(UpdateChecker::isNewerVersion("1.0.2", "1.0.1"));
    QVERIFY(UpdateChecker::isNewerVersion("1.0.10", "1.0.9"));
    QVERIFY(UpdateChecker::isNewerVersion("1.0.99", "1.0.98"));
}

void tst_UpdateChecker::testIsNewerVersion_SameVersion()
{
    QVERIFY(!UpdateChecker::isNewerVersion("1.0.0", "1.0.0"));
    QVERIFY(!UpdateChecker::isNewerVersion("2.5.3", "2.5.3"));
    QVERIFY(!UpdateChecker::isNewerVersion("10.20.30", "10.20.30"));
}

void tst_UpdateChecker::testIsNewerVersion_OlderVersion()
{
    QVERIFY(!UpdateChecker::isNewerVersion("1.0.0", "1.0.1"));
    QVERIFY(!UpdateChecker::isNewerVersion("1.0.0", "1.1.0"));
    QVERIFY(!UpdateChecker::isNewerVersion("1.0.0", "2.0.0"));
    QVERIFY(!UpdateChecker::isNewerVersion("1.9.9", "2.0.0"));
}

void tst_UpdateChecker::testIsNewerVersion_InvalidVersions()
{
    // Invalid versions should return false
    QVERIFY(!UpdateChecker::isNewerVersion("", "1.0.0"));
    QVERIFY(!UpdateChecker::isNewerVersion("1.0.0", ""));
    QVERIFY(!UpdateChecker::isNewerVersion("", ""));
    QVERIFY(!UpdateChecker::isNewerVersion("invalid", "1.0.0"));
    QVERIFY(!UpdateChecker::isNewerVersion("1.0.0", "invalid"));
}

void tst_UpdateChecker::testIsNewerVersion_ThreeDigitVersions()
{
    // Test with higher patch numbers
    QVERIFY(UpdateChecker::isNewerVersion("1.0.100", "1.0.99"));
    QVERIFY(UpdateChecker::isNewerVersion("1.100.0", "1.99.0"));
    QVERIFY(!UpdateChecker::isNewerVersion("1.0.99", "1.0.100"));
}

// ============================================================================
// Current version tests
// ============================================================================

void tst_UpdateChecker::testCurrentVersion_NotEmpty()
{
    QString version = UpdateChecker::currentVersion();
    QVERIFY(!version.isEmpty());

    // Version should be parseable
    QVersionNumber vn = QVersionNumber::fromString(version);
    QVERIFY(!vn.isNull());
}

// ============================================================================
// Install source tests
// ============================================================================

void tst_UpdateChecker::testInstallSource_Valid()
{
    UpdateChecker checker;
    InstallSource source = checker.installSource();

    // Should return a valid enum value
    QVERIFY(source >= InstallSource::MicrosoftStore);
    QVERIFY(source <= InstallSource::Unknown);
}

QTEST_MAIN(tst_UpdateChecker)
#include "tst_UpdateChecker.moc"
