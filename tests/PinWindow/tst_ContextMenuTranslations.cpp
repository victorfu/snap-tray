#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTranslator>

namespace {

QString translationFilePath()
{
    return QDir(QString::fromUtf8(SNAPTRAY_TEST_TRANSLATION_DIR))
        .filePath(QStringLiteral("snaptray_zh_TW.qm"));
}

QString pinWindowSourcePath()
{
    return QDir(QString::fromUtf8(SNAPTRAY_SOURCE_DIR))
        .filePath(QStringLiteral("src/PinWindow.cpp"));
}

QString readTextFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    return QString::fromUtf8(file.readAll());
}

} // namespace

class TestPinWindowContextMenuTranslations : public QObject
{
    Q_OBJECT

private slots:
    void testOpenHistoryFolderActionUsesSharedHistoryWindowTranslation();
    void testContextMenuDoesNotExposeBeautifyAction();
};

void TestPinWindowContextMenuTranslations::
    testOpenHistoryFolderActionUsesSharedHistoryWindowTranslation()
{
    QTranslator translator;
    QVERIFY2(translator.load(translationFilePath()),
             qPrintable(QStringLiteral("Failed to load translation file: %1")
                            .arg(translationFilePath())));
    QVERIFY(QCoreApplication::installTranslator(&translator));

    QCOMPARE(QCoreApplication::translate("HistoryWindow", "Open History Folder"),
             QString::fromUtf8("開啟歷程資料夾"));

    const QString pinWindowSource = readTextFile(pinWindowSourcePath());
    QVERIFY2(!pinWindowSource.isEmpty(),
             qPrintable(QStringLiteral("Failed to read source file: %1")
                            .arg(pinWindowSourcePath())));

    const QRegularExpression actionPattern(
        QStringLiteral(
            R"(addAction\(\s*QCoreApplication::translate\("HistoryWindow",\s*"Open History Folder"\)\s*\))"));
    QVERIFY2(actionPattern.match(pinWindowSource).hasMatch(),
             qPrintable(QStringLiteral(
                 "PinWindow context menu should reuse the HistoryWindow translation context in %1")
                            .arg(pinWindowSourcePath())));

    QCoreApplication::removeTranslator(&translator);
}

void TestPinWindowContextMenuTranslations::testContextMenuDoesNotExposeBeautifyAction()
{
    const QString pinWindowSource = readTextFile(pinWindowSourcePath());
    QVERIFY2(!pinWindowSource.isEmpty(),
             qPrintable(QStringLiteral("Failed to read source file: %1")
                            .arg(pinWindowSourcePath())));

    const QRegularExpression beautifyActionPattern(
        QStringLiteral(R"(addAction\(\s*tr\("Beautify"\)\s*\))"));
    QVERIFY2(!beautifyActionPattern.match(pinWindowSource).hasMatch(),
             qPrintable(QStringLiteral(
                 "PinWindow context menu should not expose a Beautify action in %1")
                            .arg(pinWindowSourcePath())));
}

QTEST_MAIN(TestPinWindowContextMenuTranslations)
#include "tst_ContextMenuTranslations.moc"
