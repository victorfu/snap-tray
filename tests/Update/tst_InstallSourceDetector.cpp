#include <QtTest>
#include <QCoreApplication>
#include "update/InstallSourceDetector.h"

/**
 * @brief Unit tests for InstallSourceDetector class.
 *
 * Tests installation source detection including:
 * - Source display names
 * - Store detection
 * - Store URLs and names
 * - Development build detection
 *
 * Note: The actual detect() method is platform-specific and
 * depends on runtime environment, so we test the helper methods.
 */
class tst_InstallSourceDetector : public QObject
{
    Q_OBJECT

private slots:
    // Display name tests
    void testGetSourceDisplayName_MicrosoftStore();
    void testGetSourceDisplayName_MacAppStore();
    void testGetSourceDisplayName_DirectDownload();
    void testGetSourceDisplayName_Homebrew();
    void testGetSourceDisplayName_Development();
    void testGetSourceDisplayName_Unknown();

    // Store detection helper tests
    void testGetStoreName_MicrosoftStore();
    void testGetStoreName_MacAppStore();
    void testGetStoreUrl_MicrosoftStore();
    void testGetStoreUrl_MacAppStore();

    // isStoreInstall tests (depends on current detection)
    void testIsStoreInstall_ReturnsConsistentResult();

    // Development build detection
    void testIsDevelopmentBuild_ReturnsExpectedForBuildType();

    // detect() consistency tests
    void testDetect_ReturnsCachedResult();
    void testDetect_ReturnsValidEnum();
};

// ============================================================================
// Display name tests
// ============================================================================

void tst_InstallSourceDetector::testGetSourceDisplayName_MicrosoftStore()
{
    QString name = InstallSourceDetector::getSourceDisplayName(InstallSource::MicrosoftStore);
    QCOMPARE(name, QStringLiteral("Microsoft Store"));
}

void tst_InstallSourceDetector::testGetSourceDisplayName_MacAppStore()
{
    QString name = InstallSourceDetector::getSourceDisplayName(InstallSource::MacAppStore);
    QCOMPARE(name, QStringLiteral("Mac App Store"));
}

void tst_InstallSourceDetector::testGetSourceDisplayName_DirectDownload()
{
    QString name = InstallSourceDetector::getSourceDisplayName(InstallSource::DirectDownload);
    QCOMPARE(name, QStringLiteral("Direct Download"));
}

void tst_InstallSourceDetector::testGetSourceDisplayName_Homebrew()
{
    QString name = InstallSourceDetector::getSourceDisplayName(InstallSource::Homebrew);
    QCOMPARE(name, QStringLiteral("Homebrew"));
}

void tst_InstallSourceDetector::testGetSourceDisplayName_Development()
{
    QString name = InstallSourceDetector::getSourceDisplayName(InstallSource::Development);
    QCOMPARE(name, QStringLiteral("Development Build"));
}

void tst_InstallSourceDetector::testGetSourceDisplayName_Unknown()
{
    QString name = InstallSourceDetector::getSourceDisplayName(InstallSource::Unknown);
    QCOMPARE(name, QStringLiteral("Unknown"));
}

// ============================================================================
// Store name and URL tests
// ============================================================================

void tst_InstallSourceDetector::testGetStoreName_MicrosoftStore()
{
    // Note: This depends on the actual detection result
    // We're testing that the method returns valid results for known sources
    InstallSource source = InstallSourceDetector::detect();
    QString storeName = InstallSourceDetector::getStoreName();

    if (source == InstallSource::MicrosoftStore) {
        QCOMPARE(storeName, QStringLiteral("Microsoft Store"));
    } else if (source == InstallSource::MacAppStore) {
        QCOMPARE(storeName, QStringLiteral("Mac App Store"));
    } else {
        QVERIFY(storeName.isEmpty());
    }
}

void tst_InstallSourceDetector::testGetStoreName_MacAppStore()
{
    // Same as above - testing consistency
    QString storeName = InstallSourceDetector::getStoreName();
    bool isStore = InstallSourceDetector::isStoreInstall();

    // If it's a store install, name should not be empty
    // If it's not a store install, name should be empty
    QCOMPARE(!storeName.isEmpty(), isStore);
}

void tst_InstallSourceDetector::testGetStoreUrl_MicrosoftStore()
{
    QString storeUrl = InstallSourceDetector::getStoreUrl();
    bool isStore = InstallSourceDetector::isStoreInstall();

    if (isStore) {
        QVERIFY(!storeUrl.isEmpty());
        // URL should start with a valid protocol
        QVERIFY(storeUrl.startsWith("ms-windows-store://") ||
                storeUrl.startsWith("macappstore://"));
    } else {
        QVERIFY(storeUrl.isEmpty());
    }
}

void tst_InstallSourceDetector::testGetStoreUrl_MacAppStore()
{
    // Combined with MicrosoftStore test - same logic
    QString storeUrl = InstallSourceDetector::getStoreUrl();
    InstallSource source = InstallSourceDetector::detect();

    if (source == InstallSource::MacAppStore) {
        QVERIFY(storeUrl.startsWith("macappstore://"));
    } else if (source == InstallSource::MicrosoftStore) {
        QVERIFY(storeUrl.startsWith("ms-windows-store://"));
    }
}

// ============================================================================
// isStoreInstall tests
// ============================================================================

void tst_InstallSourceDetector::testIsStoreInstall_ReturnsConsistentResult()
{
    InstallSource source = InstallSourceDetector::detect();
    bool isStore = InstallSourceDetector::isStoreInstall();

    bool expectedStore = (source == InstallSource::MicrosoftStore ||
                          source == InstallSource::MacAppStore);
    QCOMPARE(isStore, expectedStore);
}

// ============================================================================
// Development build detection
// ============================================================================

void tst_InstallSourceDetector::testIsDevelopmentBuild_ReturnsExpectedForBuildType()
{
    bool isDev = InstallSourceDetector::isDevelopmentBuild();

#ifdef QT_DEBUG
    QVERIFY(isDev);
#else
    QVERIFY(!isDev);
#endif
}

// ============================================================================
// detect() consistency tests
// ============================================================================

void tst_InstallSourceDetector::testDetect_ReturnsCachedResult()
{
    // Call detect() multiple times and verify same result
    InstallSource source1 = InstallSourceDetector::detect();
    InstallSource source2 = InstallSourceDetector::detect();
    InstallSource source3 = InstallSourceDetector::detect();

    QCOMPARE(source1, source2);
    QCOMPARE(source2, source3);
}

void tst_InstallSourceDetector::testDetect_ReturnsValidEnum()
{
    InstallSource source = InstallSourceDetector::detect();

    // Verify the result is one of the valid enum values
    QVERIFY(source == InstallSource::MicrosoftStore ||
            source == InstallSource::MacAppStore ||
            source == InstallSource::DirectDownload ||
            source == InstallSource::Homebrew ||
            source == InstallSource::Development ||
            source == InstallSource::Unknown);
}

QTEST_MAIN(tst_InstallSourceDetector)
#include "tst_InstallSourceDetector.moc"
