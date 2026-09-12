#include <QtTest>

#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>
#include <QTextBoundaryFinder>

#include "utils/FilenameTemplateEngine.h"

class tst_FilenameTemplateEngine : public QObject
{
    Q_OBJECT

private slots:
    void testRender_BasicTokens();
    void testRender_UnknownTokenFallsBack();
    void testRender_CounterTokenSkipsWhenZero();
    void testRender_MetadataFallbackToUnknown();
    void testBuildUniqueFilePath_AppendsCounter();
    void testSanitize_InvalidCharacters();
    void testLengthLimit_IsEnforced();
    void testUtf8LengthLimit_data();
    void testUtf8LengthLimit();
    void testCollisionSuffixSurvivesLengthLimit();
};

void tst_FilenameTemplateEngine::testRender_BasicTokens()
{
    FilenameTemplateEngine::Context context;
    context.timestamp = QDateTime::fromString("2025-01-01T12:00:00", Qt::ISODate);
    context.type = QStringLiteral("Screenshot");
    context.prefix = QStringLiteral("Doc");
    context.width = 1280;
    context.height = 720;
    context.monitor = QStringLiteral("1");
    context.windowTitle = QStringLiteral("Browser");
    context.ext = QStringLiteral("png");

    const auto result = FilenameTemplateEngine::renderFilename(
        "{prefix}_{type}_{yyyyMMdd_HHmmss}_{w}x{h}_{monitor}_{windowTitle}.{ext}",
        context);

    QVERIFY(!result.usedFallback);
    QCOMPARE(result.filename, QString("Doc_Screenshot_20250101_120000_1280x720_1_Browser.png"));
}

void tst_FilenameTemplateEngine::testRender_UnknownTokenFallsBack()
{
    FilenameTemplateEngine::Context context;
    context.timestamp = QDateTime::fromString("2025-01-01T12:00:00", Qt::ISODate);
    context.type = QStringLiteral("Screenshot");
    context.ext = QStringLiteral("png");

    const auto result = FilenameTemplateEngine::renderFilename("{foo}.{ext}", context);
    QVERIFY(result.usedFallback);
    QVERIFY(result.filename.endsWith(".png"));
    QVERIFY(!result.filename.isEmpty());
}

void tst_FilenameTemplateEngine::testRender_CounterTokenSkipsWhenZero()
{
    FilenameTemplateEngine::Context context;
    context.ext = QStringLiteral("png");

    context.counter = 0;
    auto first = FilenameTemplateEngine::renderFilename("shot_{#}.{ext}", context);
    QCOMPARE(first.filename, QString("shot.png"));

    context.counter = 2;
    auto second = FilenameTemplateEngine::renderFilename("shot_{#}.{ext}", context);
    QCOMPARE(second.filename, QString("shot_2.png"));
}

void tst_FilenameTemplateEngine::testRender_MetadataFallbackToUnknown()
{
    FilenameTemplateEngine::Context context;
    context.ext = QStringLiteral("png");
    context.regionIndex = -1;

    const auto result = FilenameTemplateEngine::renderFilename(
        "{windowTitle}_{appName}_{regionIndex}.{ext}", context);

    QCOMPARE(result.filename, QString("unknown_unknown_unknown.png"));
}

void tst_FilenameTemplateEngine::testBuildUniqueFilePath_AppendsCounter()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    FilenameTemplateEngine::Context context;
    context.timestamp = QDateTime::fromString("2025-01-01T12:00:00", Qt::ISODate);
    context.type = QStringLiteral("Screenshot");
    context.ext = QStringLiteral("png");
    context.outputDir = tempDir.path();

    const QString firstPath = FilenameTemplateEngine::buildUniqueFilePath(
        tempDir.path(), "{type}_{yyyyMMdd_HHmmss}.{ext}", context, 10);
    QVERIFY(!firstPath.isEmpty());

    QFile firstFile(firstPath);
    QVERIFY(firstFile.open(QIODevice::WriteOnly));
    firstFile.close();

    const QString secondPath = FilenameTemplateEngine::buildUniqueFilePath(
        tempDir.path(), "{type}_{yyyyMMdd_HHmmss}.{ext}", context, 10);
    QVERIFY(!secondPath.isEmpty());
    QVERIFY(secondPath != firstPath);
    QVERIFY(QFileInfo(secondPath).fileName().contains("_1."));
}

void tst_FilenameTemplateEngine::testSanitize_InvalidCharacters()
{
    FilenameTemplateEngine::Context context;
    context.windowTitle = QStringLiteral("A:B/C\\D*E?F\"G<H>I|J");
    context.ext = QStringLiteral("png");

    const auto result = FilenameTemplateEngine::renderFilename("{windowTitle}.{ext}", context);
    QVERIFY(!result.filename.contains('/'));
    QVERIFY(!result.filename.contains(':'));
#ifdef Q_OS_WIN
    QVERIFY(!result.filename.contains('\\'));
    QVERIFY(!result.filename.contains('*'));
    QVERIFY(!result.filename.contains('?'));
    QVERIFY(!result.filename.contains('\"'));
    QVERIFY(!result.filename.contains('<'));
    QVERIFY(!result.filename.contains('>'));
    QVERIFY(!result.filename.contains('|'));
#endif
}

void tst_FilenameTemplateEngine::testLengthLimit_IsEnforced()
{
    FilenameTemplateEngine::Context context;
    context.windowTitle = QString(400, QChar('a'));
    context.ext = QStringLiteral("png");
    context.outputDir = QStringLiteral("/tmp");

    const auto result = FilenameTemplateEngine::renderFilename("{windowTitle}.{ext}", context);
    QVERIFY(result.filename.length() <= 255);
    QVERIFY(result.filename.endsWith(".png"));
}

void tst_FilenameTemplateEngine::testUtf8LengthLimit_data()
{
    QTest::addColumn<QString>("cluster");
    QTest::addColumn<QString>("suffix");
    for (const QString& cluster : {QStringLiteral("測試"), QStringLiteral("A測📌"),
                                   QStringLiteral("e\u0301"), QStringLiteral("👨‍👩‍👧‍👦")}) {
        for (const QString& suffix : {QString(), QStringLiteral("_1"), QStringLiteral("_01234567")}) {
            QTest::addRow("%s-%s", qPrintable(cluster), qPrintable(suffix)) << cluster << suffix;
        }
    }
}

void tst_FilenameTemplateEngine::testUtf8LengthLimit()
{
    QFETCH(QString, cluster);
    QFETCH(QString, suffix);
    const QString base = cluster.repeated(100);
    const QString source = base + suffix + QStringLiteral(".png");
    const QString limited = FilenameTemplateEngine::limitFilenameComponent(source, 255, true, suffix);
    QVERIFY(!limited.isEmpty());
    QVERIFY(limited.toUtf8().size() <= 255);
    QVERIFY(limited.endsWith(suffix + QStringLiteral(".png")));
    const int hashStart = limited.indexOf('~');
    QVERIFY(hashStart >= 0);
    QCOMPARE(limited.mid(hashStart + 1, 6).size(), 6);
    const QString prefix = limited.left(hashStart);
    QVERIFY(base.startsWith(prefix));
    QTextBoundaryFinder boundaries(QTextBoundaryFinder::Grapheme, base);
    boundaries.setPosition(prefix.size());
    QVERIFY(boundaries.isAtBoundary());
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile file(dir.filePath(limited));
    QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), qPrintable(file.errorString()));
    QCOMPARE(file.write("complete"), qint64(8));
    file.close();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("complete"));
}

void tst_FilenameTemplateEngine::testCollisionSuffixSurvivesLengthLimit()
{
    QTemporaryDir dir;
    FilenameTemplateEngine::Context context;
    context.windowTitle = QStringLiteral("界").repeated(400);
    context.outputDir = dir.path();
    for (const QString& templ : {QStringLiteral("{windowTitle}.{ext}"),
                                 QStringLiteral("{windowTitle}_{#:4}.{ext}")}) {
        QTemporaryDir caseDirectory;
        QVERIFY(caseDirectory.isValid());
        context.outputDir = caseDirectory.path();
        const QString initial = FilenameTemplateEngine::renderFilename(templ, context).filename;
        const QString numbered = FilenameTemplateEngine::collisionFilename(templ, context, initial, 42);
        const QString uuid = FilenameTemplateEngine::collisionFilename(templ, context, initial, 0, "deadbeef");
        QVERIFY(numbered.endsWith(templ.contains("{#") ? "_0042.png" : "_42.png"));
        QVERIFY(uuid.endsWith("_deadbeef.png"));
        QVERIFY(initial != numbered && numbered != uuid);
#ifdef Q_OS_LINUX
        for (const QString& filename : {initial, numbered, uuid}) {
            QVERIFY(filename.toUtf8().size() <= 255);
            QFile file(caseDirectory.filePath(filename));
            QVERIFY2(file.open(QIODevice::WriteOnly | QIODevice::NewOnly), qPrintable(file.errorString()));
        }
#endif
    }
    QVERIFY(FilenameTemplateEngine::limitFilenameComponent(QString(40, 'a') + ".png", 10, true).isEmpty());
}

QTEST_MAIN(tst_FilenameTemplateEngine)
#include "tst_FilenameTemplateEngine.moc"
