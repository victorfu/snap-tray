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

QByteArray recordingManagerSource()
{
    QDir sourceDir(QStringLiteral(CAPTURE_SOURCE_ROOT));
    if (!sourceDir.cdUp()) {
        return {};
    }
    QFile source(sourceDir.filePath(QStringLiteral("RecordingManager.cpp")));
    if (!source.open(QIODevice::ReadOnly)) {
        return {};
    }
    return source.readAll();
}

QByteArray avFoundationEncoderSource()
{
    QDir sourceDir(QStringLiteral(CAPTURE_SOURCE_ROOT));
    if (!sourceDir.cdUp()) {
        return {};
    }
    QFile source(sourceDir.filePath(QStringLiteral("AVFoundationEncoder.mm")));
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
    void delegatesRouteThroughCanonicalTimestampMixer();
    void avFoundationUsesExactAudioSampleTiming();
    void microphoneRequestsConvertiblePcm();
    void callbackBarriersPrecedeMixerLifecycleTransitions();
    void runtimeSystemAudioFailureUsesGuardedDegradation();
    void recordingManagerUsesEffectiveSourceState();
    void requestedSourceFailurePreventsFalseStartSuccess();
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
             qsizetype(4));
    QVERIFY(source.count("if (engineGuard)") >= 4);

    const QByteArray stopCallback = section(
        source,
        "- (void)stream:(SCStream *)stream didStopWithError:(NSError *)error",
        "@end");
    QVERIFY(!stopCallback.isEmpty());
    QVERIFY(!stopCallback.contains("self.engine"));
    QVERIFY(!stopCallback.contains("CoreAudioCaptureEngine *engine"));
    QVERIFY(stopCallback.contains("failureBridge"));
    QVERIFY(stopCallback.contains("QMetaObject::invokeMethod"));
    QVERIFY(stopCallback.contains("handleSystemAudioCaptureFailure"));
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

void TestCoreAudioCaptureEngineSafety::delegatesRouteThroughCanonicalTimestampMixer()
{
    const QByteArray source = coreAudioSource();
    QVERIFY2(!source.isEmpty(), "Could not read CoreAudioCaptureEngine_mac.mm");

    QVERIFY(source.contains("CMSampleBufferGetPresentationTimeStamp"));
    QVERIFY(source.contains("TimestampedPcmMixer::outputFormat()"));
    QVERIFY(source.contains("SnapTray::Audio::Source::Microphone"));
    QVERIFY(source.contains("SnapTray::Audio::Source::SystemAudio"));
    QCOMPARE(source.count("engineGuard->processCapturedAudio("), qsizetype(2));
    QVERIFY(!source.contains("emit engineGuard->audioDataReady"));
    QVERIFY(source.contains("audioDataReady(output.pcm, output.startFrame)"));
}

void TestCoreAudioCaptureEngineSafety::avFoundationUsesExactAudioSampleTiming()
{
    const QByteArray source = avFoundationEncoderSource();
    QVERIFY2(!source.isEmpty(), "Could not read AVFoundationEncoder.mm");

    QVERIFY(source.contains(
        "CMTime presentationTime = CMTimeMake(startFrame, d->audioSampleRate)"));
    QVERIFY(source.contains(
        "CMTime duration = CMTimeMake(1, d->audioSampleRate)"));
    QVERIFY(!source.contains(
        "CMTime duration = CMTimeMake(numSamples, d->audioSampleRate)"));
}

void TestCoreAudioCaptureEngineSafety::microphoneRequestsConvertiblePcm()
{
    const QByteArray source = coreAudioSource();
    QVERIFY2(!source.isEmpty(), "Could not read CoreAudioCaptureEngine_mac.mm");

    const QByteArray microphoneSetup = section(
        source,
        "// Create audio output",
        "bool microphoneActive = setupMicrophone()");
    QVERIFY(!microphoneSetup.isEmpty());
    QVERIFY(microphoneSetup.contains("AVFormatIDKey: @(kAudioFormatLinearPCM)"));
    QVERIFY(microphoneSetup.contains("AVLinearPCMBitDepthKey: @16"));
    QVERIFY(microphoneSetup.contains("AVLinearPCMIsFloatKey: @NO"));
    QVERIFY(microphoneSetup.contains("AVLinearPCMIsBigEndianKey: @NO"));
    QVERIFY(microphoneSetup.contains("AVLinearPCMIsNonInterleaved: @NO"));
    QCOMPARE(source.count(
                 "if (unsupportedFormat && CMSampleBufferGetNumSamples(sampleBuffer) > 0)"),
             qsizetype(2));
}

void TestCoreAudioCaptureEngineSafety::callbackBarriersPrecedeMixerLifecycleTransitions()
{
    const QByteArray source = coreAudioSource();
    QVERIFY2(!source.isEmpty(), "Could not read CoreAudioCaptureEngine_mac.mm");

    const QByteArray pause = section(
        source,
        "void CoreAudioCaptureEngine::pause()",
        "void CoreAudioCaptureEngine::resume()");
    const QByteArray resume = section(
        source,
        "void CoreAudioCaptureEngine::resume()",
        "// ========== Permission Helper Functions");
    const QByteArray stop = section(
        source,
        "void CoreAudioCaptureEngine::stop()",
        "void CoreAudioCaptureEngine::disposeAsync()");
    QVERIFY(!pause.isEmpty());
    QVERIFY(!resume.isEmpty());
    QVERIFY(!stop.isEmpty());

    const qsizetype pauseBarrier = pause.indexOf("updateDelegatePauseState(true");
    const qsizetype pauseMixer = pause.indexOf("m_mixer->pause(");
    QVERIFY(pauseBarrier >= 0);
    QVERIFY(pauseMixer > pauseBarrier);

    const qsizetype resumeMixer = resume.indexOf("m_mixer->resume(");
    const qsizetype resumeBarrier = resume.indexOf("updateDelegatePauseState(false");
    QVERIFY(resumeMixer >= 0);
    QVERIFY(resumeBarrier > resumeMixer);

    const qsizetype cleanup = stop.indexOf("d->cleanup()");
    const qsizetype closeGate = stop.indexOf("m_running = false");
    const qsizetype flush = stop.indexOf("m_mixer->flush(");
    QVERIFY(cleanup >= 0);
    QVERIFY(closeGate > cleanup);
    QVERIFY(flush > closeGate);
}

void TestCoreAudioCaptureEngineSafety::runtimeSystemAudioFailureUsesGuardedDegradation()
{
    const QByteArray source = coreAudioSource();
    QVERIFY2(!source.isEmpty(), "Could not read CoreAudioCaptureEngine_mac.mm");

    QVERIFY(source.contains("delegate:d->scDelegate"));
    QVERIFY(source.contains("std::make_shared<SystemAudioFailureBridge>()"));

    const QByteArray handler = section(
        source,
        "void CoreAudioCaptureEngine::handleSystemAudioCaptureFailure(",
        "void CoreAudioCaptureEngine::deliverMixerOutput(");
    QVERIFY(!handler.isEmpty());
    const qsizetype cleanup = handler.indexOf("d->cleanupSystemAudio()");
    const qsizetype disable = handler.indexOf("m_mixer->setSourceEnabled(");
    const qsizetype updateState = handler.indexOf("m_systemAudioActive = false");
    const qsizetype notify = handler.indexOf("notifyActiveSourceChanged()");
    QVERIFY(cleanup >= 0);
    QVERIFY(disable > cleanup);
    QVERIFY(updateState >= 0);
    QVERIFY(notify > disable);
}

void TestCoreAudioCaptureEngineSafety::recordingManagerUsesEffectiveSourceState()
{
    const QByteArray source = recordingManagerSource();
    QVERIFY2(!source.isEmpty(), "Could not read RecordingManager.cpp");

    const qsizetype stateConnection = source.indexOf(
        "&IAudioCaptureEngine::activeSourceChanged");
    const qsizetype configureSource = source.indexOf(
        "m_audioEngine->setAudioSource(source)");
    QVERIFY(stateConnection >= 0);
    QVERIFY(configureSource > stateConnection);

    QVERIFY(source.contains("case IAudioCaptureEngine::AudioSource::None:"));
    QVERIFY(source.contains("m_controlBar->setAudioEnabled(false)"));
    QVERIFY(source.contains("m_audioEngine.get() != configuredEngine.data()"));
    QVERIFY(source.contains(
        "Audio is unavailable. This recording will be silent. "));
    QVERIFY(!source.contains("Audio capture warning: %1"));
    QVERIFY(!source.contains(".arg(msg)"));
}

void TestCoreAudioCaptureEngineSafety::requestedSourceFailurePreventsFalseStartSuccess()
{
    const QByteArray source = coreAudioSource();
    const QByteArray start = section(
        source,
        "bool CoreAudioCaptureEngine::start()",
        "void CoreAudioCaptureEngine::processCapturedAudio(");
    QVERIFY(!start.isEmpty());

    const qsizetype sourcePolicy = start.indexOf(
        "if (wantsSystemAudio && !systemAudioActive)");
    const qsizetype noSources = start.indexOf(
        "if (!microphoneActive && !systemAudioActive)");
    const qsizetype openGate = start.indexOf("m_running = true");
    const qsizetype microphoneStart = start.indexOf("[d->captureSession startRunning]");
    const qsizetype systemStart = start.indexOf("startCaptureWithCompletionHandler:");
    const qsizetype success = start.lastIndexOf("return true");
    QVERIFY(openGate >= 0);
    QVERIFY(microphoneStart > openGate);
    QVERIFY(systemStart > openGate);
    QVERIFY(sourcePolicy >= 0);
    QVERIFY(noSources > sourcePolicy);
    QVERIFY(success > noSources);
    QVERIFY(start.mid(sourcePolicy, noSources - sourcePolicy)
                .contains("m_running = false"));
    QVERIFY(start.mid(noSources, success - noSources)
                .contains("m_running = false"));
    QVERIFY(start.contains("m_mixer->setSourceEnabled("));
}

QTEST_GUILESS_MAIN(TestCoreAudioCaptureEngineSafety)
#include "tst_CoreAudioCaptureEngineSafety.moc"
