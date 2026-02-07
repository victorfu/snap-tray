#include "RecordingInitTask.h"
#include "capture/ICaptureEngine.h"
#include "capture/IAudioCaptureEngine.h"
#include "IVideoEncoder.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "encoding/EncoderFactory.h"

#include <QDebug>
#include <QScreen>
#include <QDateTime>
#include <QStandardPaths>
#include <QDir>
#include <QUuid>
#include <QCoreApplication>

void RecordingInitTask::Result::cleanup()
{
    if (captureEngine) {
        if (captureEngineStarted) {
            captureEngine->stop();
        }
        delete captureEngine;
        captureEngine = nullptr;
        captureEngineStarted = false;
    }
    if (nativeEncoder) {
        nativeEncoder->abort();
        delete nativeEncoder;
        nativeEncoder = nullptr;
    }
    if (gifEncoder) {
        gifEncoder->abort();
        delete gifEncoder;
        gifEncoder = nullptr;
    }
    if (webpEncoder) {
        webpEncoder->abort();
        delete webpEncoder;
        webpEncoder = nullptr;
    }
    if (audioEngine) {
        audioEngine->stop();
        delete audioEngine;
        audioEngine = nullptr;
    }
}

RecordingInitTask::RecordingInitTask(const Config& config, QObject* parent)
    : QObject(parent)
    , m_config(config)
    , m_cancelled(0)
{
}

RecordingInitTask::~RecordingInitTask()
{
    // Clean up any resources if initialization was cancelled or failed
    if (!m_result.success) {
        m_result.cleanup();
    }
}

void RecordingInitTask::cancel()
{
    m_cancelled.storeRelease(1);
}

void RecordingInitTask::run()
{
    qDebug() << "RecordingInitTask::run() - BEGIN";

    // Check for early cancellation before any work
    if (isCancelled()) {
        m_result.error = "Initialization cancelled";
        emit finished();
        return;
    }

    // Step 1: Initialize capture engine
    emit progress(tr("Initializing..."));
    if (!initializeCaptureEngine()) {
        emit finished();
        return;
    }

    if (isCancelled()) {
        m_result.error = "Initialization cancelled";
        m_result.cleanup();
        emit finished();
        return;
    }

    // Step 2: Initialize audio engine (if enabled, only for MP4)
    if (m_config.audioEnabled && m_config.outputFormat == EncoderFactory::Format::MP4) {
        emit progress(tr("Setting up audio capture..."));
        initializeAudioEngine();  // Actual creation happens on main thread
    }

    if (isCancelled()) {
        m_result.error = "Initialization cancelled";
        m_result.cleanup();
        emit finished();
        return;
    }

    // Step 3: Initialize encoder
    emit progress(tr("Creating video encoder..."));
    if (!initializeEncoder()) {
        m_result.cleanup();
        emit finished();
        return;
    }

    if (isCancelled()) {
        m_result.error = "Initialization cancelled";
        m_result.cleanup();
        emit finished();
        return;
    }

    // Move created QObjects to main thread before signaling completion
    // This must be done from the worker thread (which currently owns the objects)
    QThread* mainThread = QCoreApplication::instance()->thread();
    if (m_result.captureEngine) {
        m_result.captureEngine->moveToThread(mainThread);
    }
    if (m_result.nativeEncoder) {
        m_result.nativeEncoder->moveToThread(mainThread);
    }
    if (m_result.gifEncoder) {
        m_result.gifEncoder->moveToThread(mainThread);
    }
    if (m_result.webpEncoder) {
        m_result.webpEncoder->moveToThread(mainThread);
    }

    emit progress(tr("Ready"));
    m_result.success = true;
    qDebug() << "RecordingInitTask::run() - END (success)";
    emit finished();
}

bool RecordingInitTask::initializeCaptureEngine()
{
    qDebug() << "RecordingInitTask: Creating capture engine...";

    // Create capture engine with nullptr parent (will be reparented on main thread)
    m_result.captureEngine = ICaptureEngine::createBestEngine(nullptr);
    if (!m_result.captureEngine) {
        m_result.error = "Failed to create capture engine";
        return false;
    }

    qDebug() << "RecordingInitTask: Setting region:" << m_config.region
        << "screen:" << (m_config.screen ? m_config.screen->name() : "NULL");

    if (!m_result.captureEngine->setRegion(m_config.region, m_config.screen)) {
        m_result.error = "Failed to configure capture region";
        return false;
    }

    m_result.captureEngine->setFrameRate(m_config.frameRate);

    // Set windows to exclude from capture (e.g., recording UI overlays)
    if (!m_config.excludedWindowIds.isEmpty()) {
        qDebug() << "RecordingInitTask: Setting" << m_config.excludedWindowIds.size() << "excluded windows";
        m_result.captureEngine->setExcludedWindows(m_config.excludedWindowIds);
    }

    qDebug() << "RecordingInitTask: Starting capture engine...";
    if (!m_result.captureEngine->start()) {
        m_result.error = "Failed to start capture engine";
        return false;
    }
    m_result.captureEngineStarted = true;

    qDebug() << "RecordingInitTask: Capture engine started:"
        << m_result.captureEngine->engineName();
    return true;
}

bool RecordingInitTask::initializeAudioEngine()
{
    // Audio engine must be created on main thread due to macOS API requirements
    // (AVFoundation/ScreenCaptureKit have thread affinity requirements)
    // Just mark that audio was requested - actual creation happens in onInitializationComplete()
    qDebug() << "RecordingInitTask: Audio requested, will initialize on main thread";
    return true;
}

bool RecordingInitTask::initializeEncoder()
{
    qDebug() << "RecordingInitTask: Creating encoder...";

    // Configure encoder
    EncoderFactory::EncoderConfig encoderConfig;
    encoderConfig.format = m_config.outputFormat;
    encoderConfig.priority = EncoderFactory::Priority::NativeFirst;
    encoderConfig.frameSize = m_config.frameSize;
    encoderConfig.frameRate = m_config.frameRate;
    encoderConfig.outputPath = m_config.outputPath;
    encoderConfig.quality = m_config.quality;

    // Configure audio settings for native encoder using default format
    // (Audio engine is created on main thread, so we use standard defaults here)
    if (m_config.audioEnabled && m_config.outputFormat == EncoderFactory::Format::MP4) {
        encoderConfig.enableAudio = true;
        encoderConfig.audioSampleRate = 48000;   // Standard sample rate
        encoderConfig.audioChannels = 2;          // Stereo
        encoderConfig.audioBitsPerSample = 16;    // 16-bit PCM
    }

    // Create encoder (pass nullptr parent - will be reparented on main thread)
    auto encoderResult = EncoderFactory::create(encoderConfig, nullptr);

    if (!encoderResult.success) {
        m_result.error = encoderResult.errorMessage;
        return false;
    }

    m_result.usingNativeEncoder = encoderResult.isNative;
    m_result.nativeEncoder = encoderResult.nativeEncoder;
    m_result.gifEncoder = encoderResult.gifEncoder;
    m_result.webpEncoder = encoderResult.webpEncoder;

    // Note: Audio writer will be created on main thread if needed
    // (after audio engine is created there)

    qDebug() << "RecordingInitTask: Encoder created successfully";
    return true;
}
