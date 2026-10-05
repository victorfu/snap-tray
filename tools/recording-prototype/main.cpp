#include "IVideoEncoder.h"
#include "encoding/EncodingWorker.h"
#include "platform/PlatformCapabilities.h"

#include <QApplication>
#include <QComboBox>
#include <QElapsedTimer>
#include <QFileDialog>
#include <QLabel>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QSpinBox>
#include <QThread>
#include <QTimer>
#include <QVBoxLayout>
#include <QWidget>
#include <memory>

namespace {
constexpr int kFrameRate = 30;
constexpr int kMillisecondsPerSecond = 1000;

// A development harness, deliberately separate from the shipping recording UI.
// Capture stays on the GUI thread; the existing bounded EncodingWorker queue
// supplies backpressure and keeps software encoding off the GUI thread.
class RecordingPrototype : public QWidget
{
public:
    RecordingPrototype()
    {
        setWindowTitle(QStringLiteral("SnapTray — Linux recording prototype"));
        auto* layout = new QVBoxLayout(this);
        layout->addWidget(new QLabel(QStringLiteral(
            "System FFmpeg • full screen • silent MP4 • 30 fps\n"
            "This window is included if it is on the recorded screen."), this));
        m_screens = new QComboBox(this);
        for (QScreen* screen : QGuiApplication::screens()) {
            m_screenList.append(screen);
            m_screens->addItem(screen->name());
        }
        layout->addWidget(m_screens);
        m_seconds = new QSpinBox(this);
        m_seconds->setRange(1, 300);
        m_seconds->setValue(10);
        m_seconds->setSuffix(QStringLiteral(" seconds (automatic stop)"));
        layout->addWidget(m_seconds);
        m_start = new QPushButton(QStringLiteral("Record screen…"), this);
        m_stop = new QPushButton(QStringLiteral("Stop and save"), this);
        m_stop->setEnabled(false);
        m_status = new QLabel(QStringLiteral("Ready"), this);
        m_status->setWordWrap(true);
        layout->addWidget(m_start);
        layout->addWidget(m_stop);
        layout->addWidget(m_status);
        connect(m_start, &QPushButton::clicked, this, [this] { start(); });
        connect(m_stop, &QPushButton::clicked, this, [this] { finish(); });
        m_timer.setTimerType(Qt::PreciseTimer);
        connect(&m_timer, &QTimer::timeout, this, [this] { capture(); });
        resize(520, 240);
    }

    ~RecordingPrototype() override { cleanup(); }

private:
    void cleanup()
    {
        ++m_generation;
        m_timer.stop();
        if (m_worker) {
            disconnect(m_worker, nullptr, this, nullptr);
            m_worker->stop();
            m_worker = nullptr;
        }
        m_thread.quit();
        m_thread.wait();
    }

    void setRecording(bool recording)
    {
        m_start->setEnabled(!recording);
        m_stop->setEnabled(recording);
        m_screens->setEnabled(!recording);
        m_seconds->setEnabled(!recording);
    }

    void failed(const QString& message)
    {
        cleanup();
        setRecording(false);
        m_status->setText(message);
    }

    void start()
    {
        if (SnapTray::currentDisplayServerKind() != SnapTray::DisplayServerKind::X11) {
            failed(QStringLiteral("This prototype requires an X11 session."));
            return;
        }
        m_screen = m_screenList.value(m_screens->currentIndex());
        if (!m_screen) {
            failed(QStringLiteral("Selected screen is no longer connected."));
            return;
        }
        const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save recording"),
            QStringLiteral("recording.mp4"), QStringLiteral("MP4 video (*.mp4)"));
        if (path.isEmpty()) {
            return;
        }
        // Get physical dimensions from an actual full-screen frame (HiDPI).
        const QImage first = m_screen ? m_screen->grabWindow(0).toImage() : QImage();
        std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
        if (!encoder) {
            failed(QStringLiteral("System FFmpeg needs a libx264 or libopenh264 encoder."));
            return;
        }
        if (!encoder->start(path, first.size(), kFrameRate)) {
            failed(encoder->lastError());
            return;
        }
        const quint64 generation = ++m_generation;
        connect(encoder.get(), &IVideoEncoder::error, this,
                [this, generation](const QString& message) {
            if (generation == m_generation) {
                failed(message);
            }
        }, Qt::QueuedConnection);
        m_worker = new EncodingWorker;
        m_worker->setVideoEncoder(encoder.release());
        m_worker->moveOwnedEncodersToThread(&m_thread);
        m_worker->moveToThread(&m_thread);
        connect(&m_thread, &QThread::finished, m_worker, &QObject::deleteLater);
        connect(m_worker, &EncodingWorker::error, this,
                [this, generation](const QString& message) {
            if (generation == m_generation) {
                failed(message);
            }
        });
        connect(m_worker, &EncodingWorker::finished, this,
                [this, generation](bool success, const QString& output) {
            if (generation != m_generation) {
                return;
            }
            const qint64 written = m_worker->framesWritten();
            cleanup();
            setRecording(false);
            m_status->setText(success
                ? QStringLiteral("Saved %1\n%2 frames; %3 rejected by the queue; capture elapsed %4 ms.")
                    .arg(output).arg(written).arg(m_dropped).arg(m_captureElapsed)
                : QStringLiteral("Recording failed; output was not committed."));
        });
        m_thread.start();
        if (!m_worker->start()) {
            failed(QStringLiteral("Could not start encoding worker."));
            return;
        }
        m_dropped = 0;
        m_captureElapsed = 0;
        m_elapsed.start();
        m_worker->enqueueFrame({first, 0});
        setRecording(true);
        m_status->setText(QStringLiteral("Recording…"));
        m_timer.start(kMillisecondsPerSecond / kFrameRate);
    }

    void capture()
    {
        if (m_elapsed.elapsed() >= m_seconds->value() * kMillisecondsPerSecond) {
            finish();
            return;
        }
        if (!m_screen) {
            failed(QStringLiteral("Recorded screen was disconnected."));
            return;
        }
        const qint64 timestamp = m_elapsed.elapsed();
        const QImage image = m_screen->grabWindow(0).toImage();
        if (image.isNull()) {
            failed(QStringLiteral("Screen capture failed."));
            return;
        }
        if (!m_worker->enqueueFrame({image, timestamp})) {
            ++m_dropped;
        }
    }

    void finish()
    {
        m_timer.stop();
        m_captureElapsed = m_elapsed.elapsed();
        m_stop->setEnabled(false);
        m_status->setText(QStringLiteral("Finishing MP4…"));
        m_worker->requestFinish();
    }

    QComboBox* m_screens = nullptr;
    QSpinBox* m_seconds = nullptr;
    QPushButton* m_start = nullptr;
    QPushButton* m_stop = nullptr;
    QLabel* m_status = nullptr;
    QList<QPointer<QScreen>> m_screenList;
    QPointer<QScreen> m_screen;
    EncodingWorker* m_worker = nullptr;
    QThread m_thread;
    QTimer m_timer;
    QElapsedTimer m_elapsed;
    qint64 m_dropped = 0;
    qint64 m_captureElapsed = 0;
    quint64 m_generation = 0;
};
} // namespace

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    RecordingPrototype window;
    window.show();
    return app.exec();
}
