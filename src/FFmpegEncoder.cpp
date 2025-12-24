#include "FFmpegEncoder.h"

#include <QFile>
#include <QImage>
#include <QDebug>
#include <QPointer>
#include <QTimer>
#include <QSettings>
#include <QThread>

FFmpegEncoder::FFmpegEncoder(QObject *parent)
    : QObject(parent)
    , m_process(nullptr)
    , m_frameRate(30)
    , m_framesWritten(0)
    , m_finishing(false)
    , m_outputFormat(OutputFormat::MP4)
    , m_crf(23)
    , m_preset("ultrafast")
{
}

FFmpegEncoder::~FFmpegEncoder()
{
    abort();
}

bool FFmpegEncoder::isFFmpegAvailable()
{
    QString path = ffmpegPath();
    if (path.isEmpty()) {
        return false;
    }

    QProcess test;
    test.start(path, QStringList() << "-version");
    if (!test.waitForStarted(3000)) {
        return false;  // Process failed to start
    }
    if (!test.waitForFinished(3000)) {
        test.kill();
        return false;  // Process timed out
    }
    // Check for process errors (crash, failed to start, etc.)
    if (test.error() != QProcess::UnknownError) {
        return false;
    }
    return test.exitCode() == 0;
}

QString FFmpegEncoder::ffmpegPath()
{
    // Check user-configured custom path first
    QSettings settings("Victor Fu", "SnapTray");
    QString customPath = settings.value("recording/ffmpegPath").toString().trimmed();
    if (!customPath.isEmpty() && QFile::exists(customPath)) {
        return customPath;
    }

#ifdef Q_OS_MAC
    // Check Homebrew locations first
    if (QFile::exists("/opt/homebrew/bin/ffmpeg")) {
        return "/opt/homebrew/bin/ffmpeg";
    }
    if (QFile::exists("/usr/local/bin/ffmpeg")) {
        return "/usr/local/bin/ffmpeg";
    }
#endif

#ifdef Q_OS_WIN
    // Check common Windows install locations
    if (QFile::exists("C:/ffmpeg/bin/ffmpeg.exe")) {
        return "C:/ffmpeg/bin/ffmpeg.exe";
    }
    if (QFile::exists("C:/Program Files/ffmpeg/bin/ffmpeg.exe")) {
        return "C:/Program Files/ffmpeg/bin/ffmpeg.exe";
    }
#endif

    // Fall back to PATH
    return "ffmpeg";
}

bool FFmpegEncoder::start(const QString &outputPath, const QSize &frameSize, int frameRate)
{
    if (m_process && m_process->state() != QProcess::NotRunning) {
        m_lastError = "Encoder already running";
        return false;
    }

    if (!isFFmpegAvailable()) {
        m_lastError = "FFmpeg not found. Please install FFmpeg.";
        emit error(m_lastError);
        return false;
    }

    m_outputPath = outputPath;
    // Round dimensions to even numbers (libx264 requirement)
    int width = (frameSize.width() + 1) & ~1;
    int height = (frameSize.height() + 1) & ~1;
    m_frameSize = QSize(width, height);
    m_frameRate = frameRate;
    m_framesWritten = 0;
    m_finishing = false;
    m_lastError.clear();

    m_process = new QProcess(this);

    connect(m_process, &QProcess::errorOccurred,
            this, &FFmpegEncoder::onProcessError);
    connect(m_process, QOverload<int, QProcess::ExitStatus>::of(&QProcess::finished),
            this, &FFmpegEncoder::onProcessFinished);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &FFmpegEncoder::onReadyReadStandardError);

    // Use rounded m_frameSize for FFmpeg arguments (must be even for libx264)
    QStringList args = buildArguments(outputPath, m_frameSize, frameRate);

    qDebug() << "FFmpegEncoder: Starting with command:" << ffmpegPath() << args;
    m_process->start(ffmpegPath(), args);

    if (!m_process->waitForStarted(5000)) {
        m_lastError = "Failed to start FFmpeg process";
        emit error(m_lastError);
        delete m_process;
        m_process = nullptr;
        return false;
    }

    qDebug() << "FFmpegEncoder: Process started successfully";
    return true;
}

QStringList FFmpegEncoder::buildArguments(const QString &outputPath,
                                          const QSize &frameSize, int frameRate) const
{
    if (m_outputFormat == OutputFormat::GIF) {
        return buildGifArguments(outputPath, frameSize, frameRate);
    }
    return buildMp4Arguments(outputPath, frameSize, frameRate);
}

QStringList FFmpegEncoder::buildMp4Arguments(const QString &outputPath,
                                             const QSize &frameSize, int frameRate) const
{
    QStringList args;

    // Input: raw video from stdin
    args << "-f" << "rawvideo";
    args << "-pix_fmt" << "rgba";
    args << "-s" << QString("%1x%2").arg(frameSize.width()).arg(frameSize.height());
    args << "-r" << QString::number(frameRate);
    args << "-i" << "-";  // Read from stdin

    // Output settings for H.264 MP4
    args << "-c:v" << "libx264";
    args << "-preset" << m_preset;
    args << "-crf" << QString::number(m_crf);
    args << "-pix_fmt" << "yuv420p";   // Compatibility with most players
    args << "-movflags" << "+faststart";  // Web-friendly MP4 (metadata at start)

    // Overwrite output file if exists
    args << "-y";
    args << outputPath;

    return args;
}

QStringList FFmpegEncoder::buildGifArguments(const QString &outputPath,
                                             const QSize &frameSize, int frameRate) const
{
    QStringList args;

    // Input: raw video from stdin
    args << "-f" << "rawvideo";
    args << "-pix_fmt" << "rgba";
    args << "-s" << QString("%1x%2").arg(frameSize.width()).arg(frameSize.height());
    args << "-r" << QString::number(frameRate);
    args << "-i" << "-";  // Read from stdin

    // GIF output with palette optimization using split filter
    // This generates an optimal palette and applies it in a single pass
    // split[a][b]: Duplicates the input stream
    // [a]palettegen: Generates optimal 256-color palette from first copy
    // [b][p]paletteuse: Applies palette to second copy with Bayer dithering
    QString filterComplex = QString(
        "[0:v]split[a][b];"
        "[a]palettegen=max_colors=256:stats_mode=diff[p];"
        "[b][p]paletteuse=dither=bayer:bayer_scale=3"
    );

    args << "-filter_complex" << filterComplex;

    // Overwrite output file if exists
    args << "-y";
    args << outputPath;

    return args;
}

void FFmpegEncoder::writeFrame(const QImage &frame)
{
    if (!m_process || m_process->state() != QProcess::Running || m_finishing) {
        return;
    }

    // Backpressure handling: skip frame if buffer is too full
    const qint64 maxBufferSize = 50 * 1024 * 1024; // 50MB
    if (m_process->bytesToWrite() > maxBufferSize) {
        qWarning() << "FFmpegEncoder: Buffer full, dropping frame";
        return;
    }

    if (frame.isNull()) {
        return;
    }

    // Process entirely in CPU memory - no GPU involvement
    QImage processed = frame;

    // Step 1: Scale if needed (do this FIRST to reduce pixel count for format conversion)
    if (processed.size() != m_frameSize) {
        processed = processed.scaled(m_frameSize, Qt::IgnoreAspectRatio, Qt::FastTransformation);
        if (processed.isNull()) {
            qWarning() << "FFmpegEncoder: Scale failed, dropping frame";
            return;
        }
    }

    // Step 2: Convert to RGBA8888 format expected by FFmpeg (only if needed)
    if (processed.format() != QImage::Format_RGBA8888) {
        processed = processed.convertToFormat(QImage::Format_RGBA8888);
        if (processed.isNull()) {
            qWarning() << "FFmpegEncoder: Format conversion failed, dropping frame";
            return;
        }
    }

    // Write raw pixel data to stdin with partial write handling and retry limit
    const char* data = reinterpret_cast<const char*>(processed.constBits());
    qint64 totalBytes = processed.sizeInBytes();
    qint64 bytesWritten = 0;
    int retryCount = 0;
    const int maxRetries = 100;

    while (bytesWritten < totalBytes) {
        qint64 written = m_process->write(data + bytesWritten, totalBytes - bytesWritten);
        if (written <= 0) {
            if (++retryCount > maxRetries) {
                qWarning() << "FFmpegEncoder: Write failed after" << maxRetries << "retries at byte"
                           << bytesWritten << "of" << totalBytes;
                return;
            }
            QThread::msleep(1);  // Brief sleep before retry
            continue;
        }
        retryCount = 0;  // Reset retry count on successful write
        bytesWritten += written;
    }

    m_framesWritten++;
    emit progress(m_framesWritten);
}

void FFmpegEncoder::finish()
{
    if (!m_process || m_finishing) {
        return;
    }

    qDebug() << "FFmpegEncoder: Finishing encoding, frames written:" << m_framesWritten;
    m_finishing = true;

    // Close stdin to signal end of input
    m_process->closeWriteChannel();

    // Use QPointer to track object lifetime for safety
    // This ensures the lambda won't access invalid memory if the encoder is destroyed
    QPointer<FFmpegEncoder> weakThis = this;
    QPointer<QProcess> weakProcess = m_process;

    // Set up timeout timer instead of blocking waitForFinished
    // The process will emit finished() signal when done, which is connected to onProcessFinished
    QTimer::singleShot(30000, this, [weakThis, weakProcess]() {
        if (!weakThis || !weakProcess) {
            return;
        }
        if (weakProcess->state() != QProcess::NotRunning) {
            qDebug() << "FFmpegEncoder: Timeout waiting for FFmpeg, terminating";
            weakProcess->terminate();
            // Give it a few seconds to terminate gracefully, then force kill
            QTimer::singleShot(5000, weakProcess.data(), [weakProcess]() {
                if (weakProcess && weakProcess->state() != QProcess::NotRunning) {
                    qDebug() << "FFmpegEncoder: Force killing FFmpeg process";
                    weakProcess->kill();
                }
            });
        }
    });
}

void FFmpegEncoder::abort()
{
    if (m_process) {
        qDebug() << "FFmpegEncoder: Aborting";
        QProcess *proc = m_process;
        m_process = nullptr;  // Clear first to prevent re-entry

        proc->kill();
        proc->waitForFinished(1000);
        proc->deleteLater();

        // Remove partial output file (only if we're actually aborting, not if finished)
        if (!m_outputPath.isEmpty() && !m_finishing) {
            QFile::remove(m_outputPath);
        }
    }
    m_finishing = false;
}

bool FFmpegEncoder::isRunning() const
{
    return m_process && m_process->state() == QProcess::Running;
}

QString FFmpegEncoder::lastError() const
{
    return m_lastError;
}

qint64 FFmpegEncoder::framesWritten() const
{
    return m_framesWritten;
}

void FFmpegEncoder::onProcessError(QProcess::ProcessError error)
{
    QString errorStr;
    switch (error) {
        case QProcess::FailedToStart:
            errorStr = "FFmpeg failed to start";
            break;
        case QProcess::Crashed:
            errorStr = "FFmpeg crashed";
            break;
        case QProcess::Timedout:
            errorStr = "FFmpeg timed out";
            break;
        case QProcess::WriteError:
            errorStr = "Failed to write to FFmpeg";
            break;
        case QProcess::ReadError:
            errorStr = "Failed to read from FFmpeg";
            break;
        default:
            errorStr = "Unknown FFmpeg error";
            break;
    }

    m_lastError = errorStr;
    qDebug() << "FFmpegEncoder: Process error:" << errorStr;
    emit this->error(errorStr);
}

void FFmpegEncoder::onProcessFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    bool success = (exitStatus == QProcess::NormalExit && exitCode == 0);

    if (success) {
        qDebug() << "FFmpegEncoder: Finished successfully, frames:" << m_framesWritten
                 << "output:" << m_outputPath;
        emit finished(true, m_outputPath);
    } else {
        m_lastError = QString("FFmpeg exited with code %1").arg(exitCode);
        qDebug() << "FFmpegEncoder: Failed -" << m_lastError;
        emit finished(false, QString());
        // Note: Not emitting error() here to avoid duplicate signals.
        // RecordingManager will call lastError() to get the error message.
    }

    // Clean up process
    if (m_process) {
        m_process->deleteLater();
        m_process = nullptr;
    }
}

void FFmpegEncoder::onReadyReadStandardError()
{
    if (m_process) {
        QString stderrOutput = QString::fromUtf8(m_process->readAllStandardError());
        if (!stderrOutput.trimmed().isEmpty()) {
            qDebug() << "FFmpeg:" << stderrOutput.trimmed();
        }
    }
}
