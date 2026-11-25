#include <QtTest>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

// The recording control bar sits on the recorded screen. Anything animating
// there makes the compositor present every refresh, so a still screen is
// never still for the capture and every frame is encoded (#417).
class TestRecordingControlBarIndicator : public QObject
{
    Q_OBJECT
private slots:
    void init();
    void cleanup();
    void recordingIndicatorIsStillBetweenDurationTicks();
    void recordingIndicatorAdvancesOncePerDurationChange();
    void pausedIndicatorDoesNotAdvance();
    void preparingIndicatorStillAnimates();

private:
    qreal angle() const { return m_indicator->property("gradientAngle").toReal(); }

    QQmlEngine* m_engine = nullptr;
    QQuickItem* m_bar = nullptr;
    QObject* m_indicator = nullptr;
};

void TestRecordingControlBarIndicator::init()
{
    m_engine = new QQmlEngine;
    m_engine->addImportPath(QStringLiteral("qrc:/"));
    QQmlComponent component(m_engine, QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingControlBar.qml")));
    m_bar = qobject_cast<QQuickItem*>(component.create());
    QVERIFY2(m_bar, qPrintable(component.errorString()));
    m_indicator = m_bar->findChild<QObject*>(QStringLiteral("recordingIndicator"));
    QVERIFY(m_indicator);
    QVERIFY(!m_bar->property("isPreparing").toBool());
    QVERIFY(!m_bar->property("isPaused").toBool());
}

void TestRecordingControlBarIndicator::cleanup()
{
    delete m_bar;
    m_bar = nullptr;
    m_indicator = nullptr;
    delete m_engine;
    m_engine = nullptr;
}

void TestRecordingControlBarIndicator::recordingIndicatorIsStillBetweenDurationTicks()
{
    const qreal before = angle();
    QTest::qWait(500);
    QCOMPARE(angle(), before);
}

void TestRecordingControlBarIndicator::recordingIndicatorAdvancesOncePerDurationChange()
{
    const qreal step = m_indicator->property("recordingStep").toReal();
    QVERIFY(step > 0 && step < 1);
    const qreal before = angle();
    m_bar->setProperty("duration", QStringLiteral("00:00:01"));
    QVERIFY(qFuzzyCompare(1 + angle(), 1 + before + step));
    // The same text again (several updates within one second) is no change.
    m_bar->setProperty("duration", QStringLiteral("00:00:01"));
    QVERIFY(qFuzzyCompare(1 + angle(), 1 + before + step));
    QTest::qWait(300);
    QVERIFY(qFuzzyCompare(1 + angle(), 1 + before + step));
}

void TestRecordingControlBarIndicator::pausedIndicatorDoesNotAdvance()
{
    m_bar->setProperty("isPaused", true);
    const qreal before = angle();
    m_bar->setProperty("duration", QStringLiteral("00:00:02"));
    QTest::qWait(300);
    QCOMPARE(angle(), before);
}

void TestRecordingControlBarIndicator::preparingIndicatorStillAnimates()
{
    m_bar->setProperty("isPreparing", true);
    const qreal before = angle();
    QTRY_VERIFY_WITH_TIMEOUT(angle() != before, 2000);
}

QTEST_MAIN(TestRecordingControlBarIndicator)
#include "tst_RecordingControlBarIndicator.moc"
