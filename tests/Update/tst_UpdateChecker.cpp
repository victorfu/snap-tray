#include <QtTest>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSignalSpy>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTemporaryFile>
#include <QVersionNumber>
#include "update/UpdateChecker.h"
#include "update/UpdateSettingsManager.h"
#include "settings/Settings.h"

Q_DECLARE_METATYPE(ReleaseInfo)

namespace {

void clearUpdateSettings()
{
    auto settings = SnapTray::getSettings();
    settings.remove("update/autoCheck");
    settings.remove("update/checkIntervalHours");
    settings.remove("update/lastCheckTime");
    settings.remove("update/skippedVersion");
    settings.sync();
}

QByteArray makeReleasePayload(const QString& tagName)
{
    const QJsonObject payload{
        {QStringLiteral("tag_name"), tagName},
        {QStringLiteral("body"), QStringLiteral("Release notes")},
        {QStringLiteral("html_url"), QStringLiteral("https://example.com/releases/%1").arg(tagName)},
        {QStringLiteral("published_at"), QStringLiteral("2026-03-13T12:00:00Z")}
    };
    return QJsonDocument(payload).toJson(QJsonDocument::Compact);
}

} // namespace

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
    void initTestCase();
    void init();
    void cleanup();

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
    void testCheckForUpdates_UnsupportedInstallSource_EmitsUnavailable();

    // Reply handling tests
    void testOnNetworkReply_MalformedJson_DoesNotUpdateLastCheckTime();
    void testOnNetworkReply_NonObjectJson_DoesNotUpdateLastCheckTime();
    void testOnNetworkReply_InvalidRelease_DoesNotUpdateLastCheckTime();
    void testOnNetworkReply_SkippedVersion_UpdatesLastCheckTime();
    void testOnNetworkReply_ValidNoUpdate_UpdatesLastCheckTime();
    void testOnNetworkReply_ValidUpdate_UpdatesLastCheckTime();

private:
    void invokeReply(UpdateChecker& checker, QNetworkReply* reply, bool silent);
    void verifyLastCheckUpdated() const;
};

namespace {

class FileReplyContext
{
public:
    QNetworkReply* createReply(const QByteArray& payload)
    {
        if (!file.open()) {
            return nullptr;
        }

        if (file.write(payload) != payload.size()) {
            return nullptr;
        }

        file.flush();
        return networkAccessManager.get(QNetworkRequest(QUrl::fromLocalFile(file.fileName())));
    }

    QTemporaryFile file;
    QNetworkAccessManager networkAccessManager;
};

} // namespace

void tst_UpdateChecker::initTestCase()
{
    qRegisterMetaType<ReleaseInfo>("ReleaseInfo");
}

void tst_UpdateChecker::init()
{
    clearUpdateSettings();
}

void tst_UpdateChecker::cleanup()
{
    clearUpdateSettings();
}

void tst_UpdateChecker::invokeReply(UpdateChecker& checker, QNetworkReply* reply, bool silent)
{
    checker.m_silentCheck = silent;
    checker.m_isChecking = true;
    checker.onNetworkReply(reply);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
}

void tst_UpdateChecker::verifyLastCheckUpdated() const
{
    const QDateTime lastCheckTime = UpdateSettingsManager::instance().lastCheckTime();
    QVERIFY(lastCheckTime.isValid());
    QVERIFY(qAbs(lastCheckTime.secsTo(QDateTime::currentDateTime())) <= 1);
}

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

void tst_UpdateChecker::testCheckForUpdates_UnsupportedInstallSource_EmitsUnavailable()
{
    UpdateChecker checker;
    checker.m_installSource = InstallSource::MicrosoftStore;

    QSignalSpy startedSpy(&checker, &UpdateChecker::checkStarted);
    QSignalSpy unavailableSpy(&checker, &UpdateChecker::checkUnavailable);
    QSignalSpy failedSpy(&checker, &UpdateChecker::checkFailed);

    checker.checkForUpdates(false);

    QCOMPARE(startedSpy.count(), 0);
    QCOMPARE(unavailableSpy.count(), 1);
    QCOMPARE(failedSpy.count(), 0);
    QCOMPARE(unavailableSpy.at(0).at(0).toString(),
             UpdateChecker::updateCheckDisabledReason(InstallSource::MicrosoftStore));
    QVERIFY(!checker.m_isChecking);
    QVERIFY(!UpdateSettingsManager::instance().lastCheckTime().isValid());
}

// ============================================================================
// Reply handling tests
// ============================================================================

void tst_UpdateChecker::testOnNetworkReply_MalformedJson_DoesNotUpdateLastCheckTime()
{
    UpdateChecker checker;
    FileReplyContext replyContext;
    QSignalSpy failedSpy(&checker, &UpdateChecker::checkFailed);
    QNetworkReply* reply = replyContext.createReply("not json");

    QVERIFY(reply != nullptr);
    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    QVERIFY(finishedSpy.wait());
    invokeReply(checker, reply, false);

    QCOMPARE(failedSpy.count(), 1);
    QVERIFY(!UpdateSettingsManager::instance().lastCheckTime().isValid());
}

void tst_UpdateChecker::testOnNetworkReply_NonObjectJson_DoesNotUpdateLastCheckTime()
{
    UpdateChecker checker;
    FileReplyContext replyContext;
    QSignalSpy failedSpy(&checker, &UpdateChecker::checkFailed);
    QNetworkReply* reply = replyContext.createReply("[\"unexpected\"]");

    QVERIFY(reply != nullptr);
    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    QVERIFY(finishedSpy.wait());
    invokeReply(checker, reply, false);

    QCOMPARE(failedSpy.count(), 1);
    QVERIFY(!UpdateSettingsManager::instance().lastCheckTime().isValid());
}

void tst_UpdateChecker::testOnNetworkReply_InvalidRelease_DoesNotUpdateLastCheckTime()
{
    UpdateChecker checker;
    FileReplyContext replyContext;
    QSignalSpy failedSpy(&checker, &UpdateChecker::checkFailed);
    QNetworkReply* reply = replyContext.createReply("{\"body\":\"missing tag\"}");

    QVERIFY(reply != nullptr);
    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    QVERIFY(finishedSpy.wait());
    invokeReply(checker, reply, false);

    QCOMPARE(failedSpy.count(), 1);
    QVERIFY(!UpdateSettingsManager::instance().lastCheckTime().isValid());
}

void tst_UpdateChecker::testOnNetworkReply_SkippedVersion_UpdatesLastCheckTime()
{
    UpdateChecker checker;
    FileReplyContext replyContext;
    QSignalSpy noUpdateSpy(&checker, &UpdateChecker::noUpdateAvailable);

    UpdateSettingsManager::instance().setSkippedVersion("999.0.0");
    QNetworkReply* reply = replyContext.createReply(makeReleasePayload("v999.0.0"));

    QVERIFY(reply != nullptr);
    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    QVERIFY(finishedSpy.wait());
    invokeReply(checker, reply, false);

    QCOMPARE(noUpdateSpy.count(), 1);
    verifyLastCheckUpdated();
}

void tst_UpdateChecker::testOnNetworkReply_ValidNoUpdate_UpdatesLastCheckTime()
{
    UpdateChecker checker;
    FileReplyContext replyContext;
    QSignalSpy noUpdateSpy(&checker, &UpdateChecker::noUpdateAvailable);
    const QString currentVersion = UpdateChecker::currentVersion();
    QNetworkReply* reply = replyContext.createReply(makeReleasePayload("v" + currentVersion));

    QVERIFY(reply != nullptr);
    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    QVERIFY(finishedSpy.wait());
    invokeReply(checker, reply, false);

    QCOMPARE(noUpdateSpy.count(), 1);
    verifyLastCheckUpdated();
}

void tst_UpdateChecker::testOnNetworkReply_ValidUpdate_UpdatesLastCheckTime()
{
    UpdateChecker checker;
    FileReplyContext replyContext;
    QSignalSpy updateSpy(&checker, &UpdateChecker::updateAvailable);
    QNetworkReply* reply = replyContext.createReply(makeReleasePayload("v999.0.0"));

    QVERIFY(reply != nullptr);
    QSignalSpy finishedSpy(reply, &QNetworkReply::finished);
    QVERIFY(finishedSpy.wait());
    invokeReply(checker, reply, false);

    QCOMPARE(updateSpy.count(), 1);
    verifyLastCheckUpdated();
}

QTEST_MAIN(tst_UpdateChecker)
#include "tst_UpdateChecker.moc"
