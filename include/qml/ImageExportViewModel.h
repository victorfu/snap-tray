#pragma once

#include "utils/ImageExportOptions.h"

#include <QImage>
#include <QObject>
#include <QString>

#include <atomic>
#include <functional>
#include <memory>
#include <optional>

class QTimer;

namespace SnapTray {

/**
 * @brief State of the still-image Export dialog (#357).
 *
 * Holds the composed source image and the chosen options, and keeps one
 * encoded candidate for them. The candidate is made on a worker thread; its
 * byte count is the file size shown, and the same bytes are what gets saved.
 * The source image itself is never changed.
 *
 * At most one encode runs at a time. Options changed meanwhile start one more
 * run when it ends; a result for options that are no longer current is dropped.
 */
class ImageExportViewModel : public QObject
{
    Q_OBJECT

    Q_PROPERTY(int resizeMode READ resizeMode WRITE setResizeMode NOTIFY optionsChanged)
    Q_PROPERTY(int percent READ percent WRITE setPercent NOTIFY optionsChanged)
    Q_PROPERTY(int longEdge READ longEdge WRITE setLongEdge NOTIFY optionsChanged)
    Q_PROPERTY(int format READ format WRITE setFormat NOTIFY optionsChanged)
    Q_PROPERTY(int jpegQuality READ jpegQuality WRITE setJpegQuality NOTIFY optionsChanged)
    Q_PROPERTY(bool lossy READ lossy NOTIFY optionsChanged)

    Q_PROPERTY(int minPercent READ minPercent CONSTANT)
    Q_PROPERTY(int maxPercent READ maxPercent CONSTANT)
    Q_PROPERTY(int minLongEdge READ minLongEdge CONSTANT)
    Q_PROPERTY(int maxLongEdge READ maxLongEdge CONSTANT)
    Q_PROPERTY(int minJpegQuality READ minJpegQuality CONSTANT)
    Q_PROPERTY(int maxJpegQuality READ maxJpegQuality CONSTANT)

    Q_PROPERTY(QString sourceSizeText READ sourceSizeText CONSTANT)
    Q_PROPERTY(QString outputSizeText READ outputSizeText NOTIFY optionsChanged)
    Q_PROPERTY(bool estimating READ estimating NOTIFY estimateChanged)
    Q_PROPERTY(QString fileSizeText READ fileSizeText NOTIFY estimateChanged)
    Q_PROPERTY(bool canSave READ canSave NOTIFY estimateChanged)
    Q_PROPERTY(QString noteText READ noteText NOTIFY estimateChanged)
    Q_PROPERTY(QString errorText READ errorText NOTIFY errorTextChanged)

public:
    using Encoder = std::function<ImageExport::Encoded(const QImage&, const ImageExportOptions&)>;

    ImageExportViewModel(const QImage& source, const ImageExportOptions& initialOptions,
                         QObject* parent = nullptr);
    ~ImageExportViewModel() override;

    int resizeMode() const { return static_cast<int>(m_options.resize); }
    void setResizeMode(int mode);
    int percent() const { return m_options.percent; }
    void setPercent(int percent);
    // Shown no larger than the source; the stored preference is left alone.
    int longEdge() const;
    void setLongEdge(int pixels);
    int format() const { return static_cast<int>(m_options.format); }
    void setFormat(int format);
    int jpegQuality() const { return m_options.jpegQuality; }
    void setJpegQuality(int quality);
    bool lossy() const { return m_options.isLossy(); }

    int minPercent() const { return ImageExportOptions::kMinPercent; }
    int maxPercent() const { return ImageExportOptions::kMaxPercent; }
    int minLongEdge() const { return ImageExportOptions::kMinLongEdge; }
    // The source's own long edge: a larger limit would change nothing.
    int maxLongEdge() const;
    int minJpegQuality() const { return ImageExportOptions::kMinJpegQuality; }
    int maxJpegQuality() const { return ImageExportOptions::kMaxJpegQuality; }

    QString sourceSizeText() const;
    QString outputSizeText() const;
    bool estimating() const { return !m_candidate.has_value() && m_estimateError.isEmpty(); }
    QString fileSizeText() const;
    bool canSave() const { return m_candidate.has_value(); }
    QString noteText() const;
    QString errorText() const { return m_errorText; }
    void setErrorText(const QString& text);

    ImageExportOptions options() const { return m_options; }
    QSize outputPixelSize() const { return m_options.outputSize(m_source.size()); }
    // The encoded file for the current options; empty until it is ready.
    std::optional<ImageExport::Encoded> candidate() const { return m_candidate; }

    // Test seams: a replacement encoder, and how many encodes were started.
    void setEncoderForTesting(Encoder encoder);
    int encodeRunsStarted() const { return m_runsStarted; }
    bool encodeRunning() const { return m_running; }

    Q_INVOKABLE void save();
    Q_INVOKABLE void cancel();

signals:
    void optionsChanged();
    void estimateChanged();
    void errorTextChanged();
    void saveRequested();
    void cancelRequested();

private:
    void applyOptions(const ImageExportOptions& options);
    void scheduleEstimate();
    void startEncode();
    void finishEncode(quint64 generation, const ImageExport::Encoded& encoded);

    const QImage m_source;
    ImageExportOptions m_options;
    Encoder m_encoder;
    QTimer* m_debounce = nullptr;
    // Bumped by every options change; a result carrying an older value is stale.
    quint64 m_generation = 0;
    // What the worker checks before it starts: a job whose generation is no
    // longer this value (changed options, closed dialog) skips its encode.
    std::shared_ptr<std::atomic<quint64>> m_liveGeneration;
    bool m_running = false;
    bool m_restartPending = false;
    int m_runsStarted = 0;
    std::optional<ImageExport::Encoded> m_candidate;
    QString m_estimateError;
    QString m_errorText;

    // Quiet time after the last change before encoding, so dragging a slider
    // does not encode every intermediate value.
    static constexpr int kEstimateDebounceMs = 120;
};

} // namespace SnapTray
