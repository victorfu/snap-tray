#include <QtTest/QtTest>

#include <QDir>
#include <QFile>

namespace {

QByteArray coreAudioSource()
{
    QFile source(QDir(QStringLiteral(CAPTURE_SOURCE_ROOT))
                     .filePath(QStringLiteral("CoreAudioCaptureEngine_mac.mm")));
    if (!source.open(QIODevice::ReadOnly)) {
        return {};
    }
    return source.readAll();
}

QByteArray section(const QByteArray &source,
                   const QByteArray &beginMarker,
                   const QByteArray &endMarker)
{
    const qsizetype begin = source.indexOf(beginMarker);
    if (begin < 0) {
        return {};
    }

    const qsizetype end = source.indexOf(endMarker, begin + beginMarker.size());
    if (end < 0) {
        return source.mid(begin);
    }
    return source.mid(begin, end - begin);
}

} // namespace

class TestCoreAudioCaptureEngineSafety : public QObject
{
    Q_OBJECT

private slots:
    void delegatesGuardQueuedEngineAccess();
    void systemAudioTeardownRetainsAndInvalidatesCallbackGraph();
};

void TestCoreAudioCaptureEngineSafety::delegatesGuardQueuedEngineAccess()
{
    const QByteArray source = coreAudioSource();
    QVERIFY2(!source.isEmpty(), "Could not read CoreAudioCaptureEngine_mac.mm");

    QCOMPARE(source.count("@property (atomic, assign) CoreAudioCaptureEngine *engine;"),
             qsizetype(2));
    QCOMPARE(source.count("@property (atomic, assign) BOOL invalidated;"),
             qsizetype(2));
    QCOMPARE(source.count("QPointer<CoreAudioCaptureEngine> engineGuard(engine)"),
             qsizetype(2));
    QVERIFY(source.count("if (engineGuard)") >= 2);

    const QByteArray stopCallback = section(
        source,
        "- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error",
        "@end");
    QVERIFY(!stopCallback.isEmpty());
    QVERIFY(!stopCallback.contains("self.engine"));
    QVERIFY(!stopCallback.contains("CoreAudioCaptureEngine *engine"));
}

void TestCoreAudioCaptureEngineSafety::systemAudioTeardownRetainsAndInvalidatesCallbackGraph()
{
    const QByteArray source = coreAudioSource();
    QVERIFY2(!source.isEmpty(), "Could not read CoreAudioCaptureEngine_mac.mm");

    const QByteArray cleanup = section(
        source,
        "void cleanupSystemAudio()",
        "#endif");
    QVERIFY(!cleanup.isEmpty());

    const qsizetype localStream = cleanup.indexOf("SCStream *localStream = scStream");
    const qsizetype localDelegate = cleanup.indexOf(
        "SCKAudioCaptureDelegate *localDelegate = scDelegate");
    const qsizetype invalidate = cleanup.indexOf("localDelegate.invalidated = YES");
    const qsizetype clearTarget = cleanup.indexOf("localDelegate.engine = nil");
    const qsizetype removeOutput = cleanup.indexOf("removeStreamOutput:localDelegate");
    const qsizetype stopCapture = cleanup.indexOf("stopCaptureWithCompletionHandler:");
    const qsizetype drainQueue = cleanup.indexOf("drainCaptureQueue()");
    const qsizetype releaseStream = cleanup.indexOf("scStream = nil");

    QVERIFY(localStream >= 0);
    QVERIFY(localDelegate >= 0);
    QVERIFY(invalidate > localDelegate);
    QVERIFY(clearTarget > invalidate);
    QVERIFY(removeOutput > clearTarget);
    QVERIFY(stopCapture > removeOutput);
    QVERIFY(drainQueue > stopCapture);
    QVERIFY(releaseStream > drainQueue);

    QVERIFY(cleanup.contains("(void)localStream"));
    QVERIFY(cleanup.contains("(void)localDelegate"));
    QCOMPARE(cleanup.count("drainCaptureQueue()"), qsizetype(2));
    QVERIFY(!cleanup.contains("stopCaptureWithCompletionHandler:nil"));
    QVERIFY(!cleanup.contains("CoreAudioCaptureEngine *engine"));
    QVERIFY(!cleanup.contains("[this"));
}

QTEST_GUILESS_MAIN(TestCoreAudioCaptureEngineSafety)
#include "tst_CoreAudioCaptureEngineSafety.moc"
