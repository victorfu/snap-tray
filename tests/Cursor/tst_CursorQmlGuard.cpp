#include <QtTest/QtTest>

#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>

class TestCursorQmlGuard : public QObject
{
    Q_OBJECT

private slots:
    void testNoRawQmlCursorShapeLiterals();
    void testNoDirectNativeForceCallsOutsidePlatformLayer();
    void testNoRawSetCursorOutsidePlatformApplier();
};

void TestCursorQmlGuard::testNoRawQmlCursorShapeLiterals()
{
    const QRegularExpression rawCursorPattern(
        QStringLiteral("cursorShape\\s*:\\s*Qt\\.[A-Za-z]+Cursor"));
    QStringList offenders;

    QDirIterator it(QStringLiteral(CURSOR_QML_SOURCE_DIR),
                    QStringList() << QStringLiteral("*.qml"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                 qPrintable(path + QStringLiteral(": failed to open")));
        const QString content = QString::fromUtf8(file.readAll());
        if (rawCursorPattern.match(content).hasMatch()) {
            offenders << path;
        }
    }

    QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1Char('\n'))));
}

void TestCursorQmlGuard::testNoDirectNativeForceCallsOutsidePlatformLayer()
{
    const QRegularExpression forcePattern(
        QStringLiteral("\\bforceNative(?:Arrow|Crosshair)Cursor\\s*\\("));
    QStringList offenders;

    QDirIterator it(QStringLiteral(CURSOR_GUARD_SOURCE_ROOT),
                    QStringList() << QStringLiteral("*.cpp")
                                  << QStringLiteral("*.mm")
                                  << QStringLiteral("*.h"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.contains(QStringLiteral("/src/platform/WindowLevel_")) ||
            path.endsWith(QStringLiteral("/src/platform/WindowLevel.h"))) {
            continue;
        }

        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                 qPrintable(path + QStringLiteral(": failed to open")));
        const QString content = QString::fromUtf8(file.readAll());
        if (forcePattern.match(content).hasMatch()) {
            offenders << path;
        }
    }

    QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1Char('\n'))));
}

void TestCursorQmlGuard::testNoRawSetCursorOutsidePlatformApplier()
{
    const QRegularExpression setCursorPattern(QStringLiteral("\\bsetCursor\\s*\\("));
    QStringList offenders;

    QDirIterator it(QStringLiteral(CURSOR_GUARD_SOURCE_ROOT),
                    QStringList() << QStringLiteral("*.cpp")
                                  << QStringLiteral("*.mm")
                                  << QStringLiteral("*.h"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QStringLiteral("/src/cursor/CursorPlatformApplier.cpp"))) {
            continue;
        }

        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                 qPrintable(path + QStringLiteral(": failed to open")));
        const QString content = QString::fromUtf8(file.readAll());
        if (setCursorPattern.match(content).hasMatch()) {
            offenders << path;
        }
    }

    QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1Char('\n'))));
}

QTEST_MAIN(TestCursorQmlGuard)
#include "tst_CursorQmlGuard.moc"
