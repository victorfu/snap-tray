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
    void testRecordingControlBarContext();
    void testPhase4PanelContexts();

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

void tst_QmlTranslations::testRecordingControlBarContext()
{
    QCOMPARE(QCoreApplication::translate("RecordingControlBar", "-- fps"),
             QString::fromUtf8("-- 幀/秒"));
    QCOMPARE(QCoreApplication::translate("RecordingControlBar", "%1 fps").arg(30),
             QString::fromUtf8("30 幀/秒"));
    QCOMPARE(QCoreApplication::translate("RecordingControlBar", "Pause Recording"),
             QString::fromUtf8("暫停錄製"));
    QCOMPARE(QCoreApplication::translate("RecordingControlBar", "Resume Recording"),
             QString::fromUtf8("繼續錄製"));
    QCOMPARE(QCoreApplication::translate("RecordingControlBar", "Stop Recording"),
             QString::fromUtf8("停止錄製"));
    QCOMPARE(QCoreApplication::translate("RecordingControlBar", "Cancel Recording"),
             QString::fromUtf8("取消錄製"));
}

void tst_QmlTranslations::testPhase4PanelContexts()
{
    QCOMPARE(QCoreApplication::translate("BeautifyPanel", "Linear Gradient"),
             QString::fromUtf8("線性漸層"));
    QCOMPARE(QCoreApplication::translate("BeautifyPanel", "Select Color"),
             QString::fromUtf8("選擇顏色"));
    QCOMPARE(QCoreApplication::translate("PinHistoryWindow", "Open Cache Folder"),
             QString::fromUtf8("開啟快取資料夾"));
    QCOMPARE(QCoreApplication::translate("PinHistoryWindow", "No pinned screenshots yet"),
             QString::fromUtf8("尚無釘選的螢幕截圖"));
}

QTEST_MAIN(tst_QmlTranslations)
#include "tst_QmlTranslations.moc"
