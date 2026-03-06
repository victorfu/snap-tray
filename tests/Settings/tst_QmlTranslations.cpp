#include <QtTest/QtTest>

#include <QDir>
#include <QTranslator>

namespace {

QString translationFilePath()
{
    return QDir(QString::fromUtf8(SNAPTRAY_TEST_TRANSLATION_DIR))
        .filePath(QStringLiteral("snaptray_zh_TW.qm"));
}

} // namespace

class tst_QmlTranslations : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void testQmlSettingsContexts();

private:
    QTranslator m_translator;
};

void tst_QmlTranslations::initTestCase()
{
    QVERIFY2(m_translator.load(translationFilePath()),
             qPrintable(QStringLiteral("Failed to load translation file: %1")
                            .arg(translationFilePath())));
    QVERIFY(QCoreApplication::installTranslator(&m_translator));
}

void tst_QmlTranslations::testQmlSettingsContexts()
{
    QCOMPARE(QCoreApplication::translate("SnapTray::QmlSettingsWindow", "Settings"),
             QString::fromUtf8("設定"));
    QCOMPARE(QCoreApplication::translate("GeneralSettings", "Start on login"),
             QString::fromUtf8("登入時啟動"));
    QCOMPARE(QCoreApplication::translate("SettingsSidebar", "General"),
             QString::fromUtf8("一般"));
    QCOMPARE(QCoreApplication::translate("SettingsPermissionRow", "Granted"),
             QString::fromUtf8("已授權"));
    QCOMPARE(QCoreApplication::translate("SnapTray::QmlSettingsWindow",
                                         "Settings saved. Language change will apply after restart."),
             QString::fromUtf8("設定已儲存。語言變更將在重新啟動後生效。"));
}

QTEST_MAIN(tst_QmlTranslations)
#include "tst_QmlTranslations.moc"
