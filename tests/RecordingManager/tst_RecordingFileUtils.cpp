#include <QtTest/QtTest>

#include "recording/RecordingFileUtils.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

bool writeFile(const QString &path, const QByteArray &contents)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && file.write(contents) == contents.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return file.readAll();
}

} // namespace

class TestRecordingFileUtils : public QObject
{
    Q_OBJECT

private slots:
    void testExistingDestinationReplacedAfterCompleteCopy();
    void testMissingSourcePreservesExistingDestination();
    void testCommitFailurePreservesExistingDestination();
    void testNewDestinationMovesSource();
    void testSamePathIsNoOp();
    void testCaseAliasOfSameFileIsNoOp();
};

void TestRecordingFileUtils::testExistingDestinationReplacedAfterCompleteCopy()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString sourcePath = tempDir.filePath(QStringLiteral("source.mp4"));
    const QString destinationPath = tempDir.filePath(QStringLiteral("destination.mp4"));
    const QByteArray oldRecording("existing recording that must survive until commit");
    const QByteArray newRecording(2 * 1024 * 1024, 'n');
    QVERIFY(writeFile(sourcePath, newRecording));
    QVERIFY(writeFile(destinationPath, oldRecording));

    QString error;
    QVERIFY2(SnapTray::RecordingFileUtils::replaceRecordingFile(
                 sourcePath, destinationPath, &error),
             qPrintable(error));

    QVERIFY(!QFileInfo::exists(sourcePath));
    QCOMPARE(readFile(destinationPath), newRecording);
}

void TestRecordingFileUtils::testMissingSourcePreservesExistingDestination()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString sourcePath = tempDir.filePath(QStringLiteral("missing.mp4"));
    const QString destinationPath = tempDir.filePath(QStringLiteral("destination.mp4"));
    const QByteArray oldRecording("irreplaceable existing recording");
    QVERIFY(writeFile(destinationPath, oldRecording));

    QString error;
    QVERIFY(!SnapTray::RecordingFileUtils::replaceRecordingFile(
        sourcePath, destinationPath, &error));

    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(destinationPath), oldRecording);
}

void TestRecordingFileUtils::testCommitFailurePreservesExistingDestination()
{
#ifndef Q_OS_WIN
    QSKIP("The deterministic destination-lock failure uses Windows file sharing semantics.");
#else
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString sourcePath = tempDir.filePath(QStringLiteral("source.mp4"));
    const QString destinationPath = tempDir.filePath(QStringLiteral("destination.mp4"));
    const QByteArray oldRecording("locked existing recording");
    const QByteArray newRecording("replacement recording");
    QVERIFY(writeFile(sourcePath, newRecording));
    QVERIFY(writeFile(destinationPath, oldRecording));

    const HANDLE destinationLock = CreateFileW(
        reinterpret_cast<LPCWSTR>(destinationPath.utf16()),
        GENERIC_READ,
        0,
        nullptr,
        OPEN_EXISTING,
        FILE_ATTRIBUTE_NORMAL,
        nullptr);
    QVERIFY(destinationLock != INVALID_HANDLE_VALUE);

    QString error;
    const bool replaced = SnapTray::RecordingFileUtils::replaceRecordingFile(
        sourcePath, destinationPath, &error);
    CloseHandle(destinationLock);

    QVERIFY(!replaced);
    QVERIFY(!error.isEmpty());
    QCOMPARE(readFile(destinationPath), oldRecording);
    QCOMPARE(readFile(sourcePath), newRecording);
#endif
}

void TestRecordingFileUtils::testNewDestinationMovesSource()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString sourcePath = tempDir.filePath(QStringLiteral("source.gif"));
    const QString destinationPath = tempDir.filePath(QStringLiteral("destination.gif"));
    const QByteArray recording("new recording");
    QVERIFY(writeFile(sourcePath, recording));

    QString error;
    QVERIFY2(SnapTray::RecordingFileUtils::replaceRecordingFile(
                 sourcePath, destinationPath, &error),
             qPrintable(error));

    QVERIFY(!QFileInfo::exists(sourcePath));
    QCOMPARE(readFile(destinationPath), recording);
}

void TestRecordingFileUtils::testSamePathIsNoOp()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString path = tempDir.filePath(QStringLiteral("recording.webp"));
    const QByteArray recording("same path recording");
    QVERIFY(writeFile(path, recording));

    QString error;
    QVERIFY2(SnapTray::RecordingFileUtils::replaceRecordingFile(path, path, &error),
             qPrintable(error));
    QCOMPARE(readFile(path), recording);
}

void TestRecordingFileUtils::testCaseAliasOfSameFileIsNoOp()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString sourcePath = tempDir.filePath(QStringLiteral("CaseAliasRecording.mp4"));
    const QString aliasPath = tempDir.filePath(QStringLiteral("casealiasrecording.mp4"));
    const QByteArray recording("case alias recording");
    QVERIFY(writeFile(sourcePath, recording));

    if (!QFileInfo::exists(aliasPath)) {
        QSKIP("The test volume is case-sensitive.");
    }

    QString error;
    QVERIFY2(SnapTray::RecordingFileUtils::replaceRecordingFile(
                 sourcePath, aliasPath, &error),
             qPrintable(error));
    QVERIFY(QFileInfo::exists(sourcePath));
    QVERIFY(QFileInfo::exists(aliasPath));
    QCOMPARE(readFile(sourcePath), recording);
    QCOMPARE(readFile(aliasPath), recording);
}

QTEST_MAIN(TestRecordingFileUtils)
#include "tst_RecordingFileUtils.moc"
