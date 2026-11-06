#include "qml/RecordingPreviewBackend.h"
#include "cursor/CursorSurfaceSupport.h"
#include "qml/QmlOverlayManager.h"
#include "qml/VideoPlaybackItem.h"
#include "utils/VideoCropGeometry.h"
#include "video/IVideoTranscoder.h"
#include "video/IVideoFrameReader.h"
#include "video/IVideoPlayer.h"
#include "encoding/NativeGifEncoder.h"
#include "encoding/WebPAnimEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "settings/RecordingSettingsManager.h"
#include "settings/BeautifySettingsManager.h"
#include "recording/RecordingAppearance.h"
#include "recording/WindowTimelineSidecar.h"
#include "recording/CursorTimeline.h"
#include "recording/KeyTimeline.h"
#include "recording/AudioTracks.h"

#include <QCloseEvent>
#include "recording/DiskSpaceGuard.h"
#include "recording/RecordingSession.h"
#include <QLocale>
#include <QMutex>
#include <QMutexLocker>
#include <QSaveFile>
#include <QStorageInfo>
#include <cmath>
#include <QCoreApplication>
#include <QDateTime>
#include <QDebug>
#include <QEvent>
#include <QEventLoop>
#include <QFile>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QPainter>
#include <QPointer>
#include <utility>
#include <QQmlContext>
#include <QQuickView>
#include <QTimer>
#include <QtMath>
#include <QScreen>
#include <QtConcurrent/QtConcurrentRun>
#include <QUuid>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

#ifdef Q_OS_WIN
#include <objbase.h>
#endif

namespace {
constexpr int kVideoLoadTimeoutMs = 5000;
// How long an animated export waits for each extracted frame to arrive.
constexpr int kFrameExtractionTimeoutMs = 5000;
}

struct RecordingPreviewBackend::PreviewAudio {
    QMutex mutex;
    SnapTray::AudioTrackMixer mixer;
    PreviewAudio(const QString& videoPath, const SnapTray::AudioMixSettings& settings)
        : mixer(videoPath, settings)
    {
    }
};

RecordingPreviewBackend::RecordingPreviewBackend(const QString &videoPath, QObject *parent)
    : RecordingPreviewBackend(videoPath, true, parent)
{
}

RecordingPreviewBackend::RecordingPreviewBackend(const QString &videoPath, bool recordedAsIntermediate,
                                                 QObject *parent)
    : QObject(parent)
    , m_videoPath(videoPath)
    , m_recordedAsIntermediate(recordedAsIntermediate)
{
    for (const auto signal : {&RecordingPreviewBackend::trimRangeChanged, &RecordingPreviewBackend::timelineChanged,
                              &RecordingPreviewBackend::durationChanged}) {
        connect(this, signal, this, &RecordingPreviewBackend::outputDurationChanged);
    }
    m_windowTimeline = SnapTray::WindowTimelineSidecar::read(videoPath);
    if (auto cursor = SnapTray::CursorTimelineSidecar::read(videoPath)) {
        m_cursorTimeline = std::make_shared<const SnapTray::CursorTimeline>(std::move(*cursor));
    }
    // The pointer look the user chose last time; undo starts from it.
    m_cursorEffects = SnapTray::CursorEffectSettings::fromJson(RecordingSettingsManager::instance().pointerEffects())
                          .preferences();
    // Separate tracks (#189): only a recording made with both sources has them.
    m_hasSeparateTracks = SnapTray::AudioTracks::exist(videoPath);
    // Shortcut hints (#187): only for a recording made with keys switched on.
    if (auto keys = SnapTray::KeyTimelineSidecar::read(videoPath)) {
        m_keyTimeline = std::make_shared<const SnapTray::KeyTimeline>(std::move(*keys));
    }
    m_keyOverlay = SnapTray::KeyOverlaySettings::fromJson(RecordingSettingsManager::instance().keyOverlay());
    // The frame around exports: the last one used for recordings, or at first
    // the screenshot Beautify look (off until switched on).
    const QJsonObject appearance = RecordingSettingsManager::instance().appearance();
    m_appearance = appearance.isEmpty()
        ? SnapTray::RecordingAppearance::fromBeautify(BeautifySettingsManager::instance().loadSettings())
        : SnapTray::RecordingAppearance::fromJson(appearance);
    m_editHistory.reset(currentEditState());
    // Availability depends on the format (MP4 needs a capable transcoder).
    connect(this, &RecordingPreviewBackend::formatChanged, this, &RecordingPreviewBackend::cursorEffectsChanged);
    connect(this, &RecordingPreviewBackend::formatChanged, this, &RecordingPreviewBackend::zoomChanged);
    connect(this, &RecordingPreviewBackend::formatChanged, this, &RecordingPreviewBackend::masksChanged);
    connect(this, &RecordingPreviewBackend::formatChanged, this, &RecordingPreviewBackend::appearanceChanged);
    connect(this, &RecordingPreviewBackend::formatChanged, this, &RecordingPreviewBackend::keyOverlayChanged);
    connect(this, &RecordingPreviewBackend::formatChanged, this, &RecordingPreviewBackend::overlaysChanged);
    connect(this, &RecordingPreviewBackend::formatChanged, this, &RecordingPreviewBackend::audioMixChanged);
    // The exported length and the cut availability follow the trim, duration and format.
    connect(this, &RecordingPreviewBackend::formatChanged, this, &RecordingPreviewBackend::timelineChanged);
    connect(this, &RecordingPreviewBackend::trimRangeChanged, this, &RecordingPreviewBackend::timelineChanged);
    connect(this, &RecordingPreviewBackend::durationChanged, this, &RecordingPreviewBackend::timelineChanged);
    for (const auto signal : {&RecordingPreviewBackend::zoomChanged, &RecordingPreviewBackend::cursorEffectsChanged,
                              &RecordingPreviewBackend::timelineChanged, &RecordingPreviewBackend::cropRectChanged,
                              &RecordingPreviewBackend::videoSizeChanged}) {
        connect(this, signal, this, [this] { m_preparedZoom.reset(); });
    }
    m_mp4Options = SnapTray::Mp4ExportOptions::fromJson(RecordingSettingsManager::instance().mp4ExportOptions());
    // The estimate follows everything that changes the exported picture or length.
    for (const auto signal : {&RecordingPreviewBackend::mp4OptionsChanged, &RecordingPreviewBackend::cropRectChanged,
                              &RecordingPreviewBackend::trimRangeChanged, &RecordingPreviewBackend::durationChanged,
                              &RecordingPreviewBackend::videoSizeChanged, &RecordingPreviewBackend::cursorEffectsChanged,
                              &RecordingPreviewBackend::zoomChanged, &RecordingPreviewBackend::masksChanged,
                              &RecordingPreviewBackend::appearanceChanged, &RecordingPreviewBackend::timelineChanged,
                              // These decide whether an unedited recording is
                              // saved as is (PR #213 review).
                              &RecordingPreviewBackend::audioMixChanged, &RecordingPreviewBackend::keyOverlayChanged,
                              &RecordingPreviewBackend::overlaysChanged}) {
        connect(this, signal, this, &RecordingPreviewBackend::mp4EstimateChanged);
    }
    // The saved size follows the same inputs, and the format.
    connect(this, &RecordingPreviewBackend::mp4EstimateChanged, this, &RecordingPreviewBackend::exportSizeChanged);
    connect(this, &RecordingPreviewBackend::formatChanged, this, &RecordingPreviewBackend::exportSizeChanged);
}

std::function<void(QImage&, qint64)> RecordingPreviewBackend::effectsFrameFilter() const
{
    // A snapshot: shares the immutable timeline, copies settings and geometry.
    const QSize videoSize = effectsVideoSize();
    const QRectF bounds = m_cropRect.isEmpty() ? QRectF(QPointF(0, 0), QSizeF(videoSize)) : QRectF(m_cropRect);
    const auto timeline = m_cursorTimeline;
    const bool drawCursor = cursorEffectsActive();
    const auto cursor = drawCursor
        ? std::make_shared<SnapTray::CursorEffectRenderer>(*timeline, m_cursorEffects, effectsClipEnd())
        : std::shared_ptr<SnapTray::CursorEffectRenderer>();
    // Nothing of the pointer reaches across a cut (#185).
    if (cursor && hasTimelineEdits()) cursor->setTimeline(timelineMap());
    const auto zoom = zoomAvailable() && m_zoom.isActive() ? preparedZoom()
                                                            : std::shared_ptr<const SnapTray::ZoomTrack>();
    const auto redaction = m_redaction.isEmpty() ? std::shared_ptr<SnapTray::RedactionTrack>()
                                                  : std::make_shared<SnapTray::RedactionTrack>(m_redaction);
    // The frame around the picture (#184), last; its static parts are drawn once here.
    const auto appearance = appearanceActive() && !bounds.isEmpty()
        ? std::make_shared<const SnapTray::RecordingAppearanceRenderer>(
              m_appearance, bounds.size().toSize(),
              m_appearance.background == SnapTray::RecordingAppearance::Background::Image
                  ? QImage(m_appearance.imagePath) : QImage())
        : std::shared_ptr<const SnapTray::RecordingAppearanceRenderer>();
    const bool blurredBackground = appearance
        && m_appearance.background == SnapTray::RecordingAppearance::Background::BlurredVideo;
    // Shortcut hints (#187) on the output picture, after the zoom and pointer.
    std::shared_ptr<SnapTray::KeyOverlayRenderer> keys;
    if (keyOverlayActive()) {
        keys = std::shared_ptr<SnapTray::KeyOverlayRenderer>(
            new SnapTray::KeyOverlayRenderer(*m_keyTimeline, m_keyOverlay),
            [held = m_keyTimeline](SnapTray::KeyOverlayRenderer* renderer) { delete renderer; });
        // Always the trimmed range (PR #200 review): a key pressed before the
        // trim start must not show in the first hint, even without cuts.
        keys->setTimeline(timelineMap());
    }
    // Text and image overlays (#188): on top, inside the frame.
    const auto overlays = m_overlays.isEmpty() || !overlaysAvailable()
        ? std::shared_ptr<const SnapTray::OverlayTrack>()
        : std::make_shared<const SnapTray::OverlayTrack>(m_overlays);
    return [timeline, cursor, zoom, redaction, appearance, blurredBackground, keys, overlays, bounds, videoSize](
               QImage& frame, qint64 sourceMs) {
        if (frame.format() != QImage::Format_RGB32 && frame.format() != QImage::Format_ARGB32_Premultiplied) {
            frame = frame.convertToFormat(QImage::Format_RGB32);
        }
        // Masks first, on the source content: nothing they cover can come
        // back through the zoom's resampling.
        // Never skip masks for lack of geometry: without a known size the
        // frame itself is the source area.
        if (redaction) {
            redaction->apply(frame, sourceMs,
                             bounds.isEmpty() ? QRectF(QPointF(0, 0), QSizeF(frame.size())) : bounds);
        }
        // A blurred background comes from the masked picture, before zoom and
        // pointer: nothing a mask covers can show through it.
        const QImage blurSource = blurredBackground
            ? SnapTray::RecordingAppearanceRenderer::blurSourceFor(frame) : QImage();
        // The frame shows `bounds`; a zoom shows part of it, scaled to fill.
        QRectF shown = bounds;
        if (zoom) {
            shown = zoom->viewportAt(sourceMs, bounds);
            if (shown != bounds) {
                QImage zoomed(frame.size(), frame.format());
                QPainter painter(&zoomed);
                painter.setRenderHint(QPainter::SmoothPixmapTransform);
                const double sx = frame.width() / bounds.width();
                const double sy = frame.height() / bounds.height();
                const QRectF source((shown.x() - bounds.x()) * sx, (shown.y() - bounds.y()) * sy,
                                    shown.width() * sx, shown.height() * sy);
                painter.drawImage(QRectF(zoomed.rect()), frame, source);
                painter.end();
                frame = zoomed;
            }
        }
        if (cursor) cursor->paint(frame, sourceMs, {shown, videoSize, frame.size()});
        if (keys) keys->paint(frame, sourceMs);
        if (overlays) overlays->paint(frame, sourceMs);
        if (appearance) frame = appearance->compose(frame, blurSource);
    };
}

QSize RecordingPreviewBackend::effectsVideoSize() const
{
    return m_videoSize.isEmpty() && m_cursorTimeline ? m_cursorTimeline->info.frameSize : m_videoSize;
}

QSize RecordingPreviewBackend::exportContentSize() const
{
    return m_cropRect.isEmpty() ? effectsVideoSize() : m_cropRect.size();
}

QSize RecordingPreviewBackend::framedExportSize() const
{
    const QSize content = exportContentSize();
    return appearanceActive() && !content.isEmpty() ? m_appearance.outputSize(content) : QSize();
}

QRectF RecordingPreviewBackend::appearanceCanvasInView(const QRectF& contentInView) const
{
    const QSize content = exportContentSize();
    if (!appearanceActive() || content.isEmpty() || contentInView.isEmpty()) return contentInView;
    const double scale = contentInView.width() / content.width();
    const QRect inset = m_appearance.contentRect(content);
    const QSize framed = m_appearance.outputSize(content);
    return QRectF(contentInView.topLeft() - QPointF(inset.topLeft()) * scale, QSizeF(framed) * scale);
}

QVariantList RecordingPreviewBackend::appearanceGradients() const
{
    QVariantList list;
    for (const auto& preset : beautifyPresets()) {
        list.append(QVariantMap{{QStringLiteral("name"), preset.name},
                                {QStringLiteral("start"), preset.gradientStart},
                                {QStringLiteral("end"), preset.gradientEnd}});
    }
    return list;
}

void RecordingPreviewBackend::setAppearance(const SnapTray::RecordingAppearance& appearance)
{
    if (m_appearance == appearance) return;
    m_appearance = appearance;
    emit appearanceChanged();
}

void RecordingPreviewBackend::editAppearance(const std::function<void(SnapTray::RecordingAppearance&)>& change)
{
    if (isProcessing()) return; // a running export keeps what it started with
    SnapTray::RecordingAppearance appearance = m_appearance;
    change(appearance);
    if (appearance == m_appearance) return;
    rememberAppearance(appearance);
    setAppearance(appearance);
    recordEdit();
}

void RecordingPreviewBackend::rememberAppearance(const SnapTray::RecordingAppearance& appearance)
{
    // A project's own copy of a background image lives and dies with the
    // project: the next recording keeps the image it remembered before
    // (PR #203 review).
    QJsonObject json = appearance.toJson();
    if (!appearance.imagePath.isEmpty() && SnapTray::RecordingProjects::contains(appearance.imagePath)) {
        const auto remembered = SnapTray::RecordingAppearance::fromJson(RecordingSettingsManager::instance().appearance());
        json.insert(QStringLiteral("imagePath"), remembered.imagePath);
    }
    RecordingSettingsManager::instance().setAppearance(json);
}

void RecordingPreviewBackend::setAppearanceEnabled(bool value)
{
    editAppearance([value](SnapTray::RecordingAppearance& a) { a.enabled = value; });
}

void RecordingPreviewBackend::setAppearanceBackground(int value)
{
    const int clamped = qBound(0, value, int(SnapTray::RecordingAppearance::Background::Image));
    // An image background needs an image: setAppearanceImage() picks one.
    if (clamped == int(SnapTray::RecordingAppearance::Background::Image) && m_appearance.imagePath.isEmpty()) return;
    editAppearance([clamped](SnapTray::RecordingAppearance& a) {
        a.enabled = true;
        a.background = SnapTray::RecordingAppearance::Background(clamped);
    });
}

void RecordingPreviewBackend::setAppearanceGradientPreset(int value)
{
    if (value < 0 || value >= beautifyPresets().size()) return;
    editAppearance([value](SnapTray::RecordingAppearance& a) {
        a.enabled = true;
        a.background = SnapTray::RecordingAppearance::Background::Gradient;
        a.gradientPreset = value;
    });
}

void RecordingPreviewBackend::setAppearanceSolidColor(const QColor& value)
{
    if (!value.isValid()) return;
    editAppearance([value](SnapTray::RecordingAppearance& a) {
        a.enabled = true;
        a.background = SnapTray::RecordingAppearance::Background::Solid;
        a.solidColor = value.toRgb();
        a.solidColor.setAlpha(255);
    });
}

bool RecordingPreviewBackend::setAppearanceImage(const QUrl& file)
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    if (path.isEmpty() || QImage(path).isNull()) return false;
    editAppearance([path](SnapTray::RecordingAppearance& a) {
        a.enabled = true;
        a.background = SnapTray::RecordingAppearance::Background::Image;
        a.imagePath = path;
    });
    return true;
}

bool RecordingPreviewBackend::chooseAppearanceImage()
{
    if (isProcessing()) return false;
    const QString file = QFileDialog::getOpenFileName(nullptr, tr("Select Background Image"), m_appearance.imagePath,
                                                      tr("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
    if (file.isEmpty()) return false;
    if (setAppearanceImage(QUrl::fromLocalFile(file))) return true;
    setErrorMessage(tr("The image could not be read."));
    return false;
}

void RecordingPreviewBackend::setAppearancePadding(int value)
{
    const int clamped = qBound(0, value, int(SnapTray::RecordingAppearance::Amount::Large));
    editAppearance([clamped](SnapTray::RecordingAppearance& a) { a.padding = SnapTray::RecordingAppearance::Amount(clamped); });
}

void RecordingPreviewBackend::setAppearanceCorners(int value)
{
    const int clamped = qBound(0, value, int(SnapTray::RecordingAppearance::Amount::Large));
    editAppearance([clamped](SnapTray::RecordingAppearance& a) { a.corners = SnapTray::RecordingAppearance::Amount(clamped); });
}

void RecordingPreviewBackend::setAppearanceShadow(bool value)
{
    editAppearance([value](SnapTray::RecordingAppearance& a) { a.shadow = value; });
}

void RecordingPreviewBackend::setAppearanceBorder(bool value)
{
    editAppearance([value](SnapTray::RecordingAppearance& a) { a.border = value; });
}

void RecordingPreviewBackend::setAppearanceAspectRatio(int value)
{
    const int clamped = qBound(0, value, int(BeautifyAspectRatio::Twitter_2_1));
    editAppearance([clamped](SnapTray::RecordingAppearance& a) { a.aspectRatio = BeautifyAspectRatio(clamped); });
}

bool RecordingPreviewBackend::hasPointerData() const
{
    return SnapTray::CursorEffectAvailability::of(m_cursorTimeline.get()).cursor;
}

bool RecordingPreviewBackend::cursorEffectsAvailable() const
{
    return hasPointerData() && formatCanDrawEffects();
}

const RecordingPreviewBackend::Mp4Capabilities& RecordingPreviewBackend::mp4Capabilities() const
{
    if (!m_mp4Capabilities) {
        const auto factory = transcoderFactoryOverride() ? transcoderFactoryOverride()
                                                         : TranscoderFactory(&IVideoTranscoder::create);
        const auto transcoder = factory ? factory() : nullptr;
        Mp4Capabilities capabilities;
        if (transcoder) {
            capabilities.frameFilter = transcoder->supportsFrameFilter();
            capabilities.outputOptions = transcoder->supportsOutputOptions();
            capabilities.hevc = transcoder->supportsVideoCodec(QString::fromLatin1(kVideoCodecHevc));
            capabilities.segments = transcoder->supportsSegments();
            capabilities.audioMix = transcoder->supportsAudioMix();
        }
        m_mp4Capabilities = capabilities;
    }
    return *m_mp4Capabilities;
}

const VideoFileProbe& RecordingPreviewBackend::sourceProbe() const
{
    static const VideoFileProbe kNotYetProbed;
    if (m_sourceProbe) return *m_sourceProbe;
    if (!m_sourceProbePending) {
        m_sourceProbePending = true;
        const auto factory = transcoderFactoryOverride() ? transcoderFactoryOverride()
                                                         : TranscoderFactory(&IVideoTranscoder::create);
        const QString path = m_videoPath;
        const QPointer<RecordingPreviewBackend> weakThis(const_cast<RecordingPreviewBackend*>(this));
        (void)QtConcurrent::run([factory, path, weakThis]() {
            const auto transcoder = factory ? factory() : nullptr;
            const VideoFileProbe probe = transcoder ? transcoder->probe(path) : VideoFileProbe();
            if (QCoreApplication* app = QCoreApplication::instance()) {
                QMetaObject::invokeMethod(app, [weakThis, probe]() {
                    if (!weakThis) return;
                    weakThis->m_sourceProbe = probe;
                    weakThis->m_sourceProbePending = false;
                    emit weakThis->mp4EstimateChanged();
                }, Qt::QueuedConnection);
            }
        });
    }
    return kNotYetProbed;
}

bool RecordingPreviewBackend::sourceProbeReady() const
{
    (void)sourceProbe();
    return m_sourceProbe.has_value();
}

bool RecordingPreviewBackend::formatCanDrawEffects() const
{
    if (m_selectedFormat != MP4) return true; // GIF/WebP frames are drawn here
    return mp4Capabilities().frameFilter;
}

bool RecordingPreviewBackend::mp4OutputOptionsAvailable() const
{
    return mp4Capabilities().outputOptions;
}

bool RecordingPreviewBackend::mp4HevcAvailable() const
{
    return mp4Capabilities().hevc;
}

SnapTray::Mp4ExportOptions RecordingPreviewBackend::effectiveMp4Options() const
{
    SnapTray::Mp4ExportOptions options = m_mp4Options;
    if (!mp4OutputOptionsAvailable()) {
        options.resolution = SnapTray::Mp4ExportOptions::Resolution::Original;
        options.frameRate = 0;
    }
    if (options.codec == SnapTray::Mp4ExportOptions::Codec::Hevc && !mp4HevcAvailable()) {
        options.codec = SnapTray::Mp4ExportOptions::Codec::H264;
    }
    return options;
}

QSize RecordingPreviewBackend::mp4BaseSize() const
{
    const QSize framed = framedExportSize();
    if (!framed.isEmpty()) return framed;
    if (!m_cropRect.isEmpty()) return m_cropRect.size();
    return m_videoSize.isEmpty() ? sourceProbe().videoSize : m_videoSize;
}

bool RecordingPreviewBackend::mp4OptionsActive() const
{
    const VideoFileProbe& probe = sourceProbe();
    // Unknown source codec: only an explicit HEVC choice counts as a change.
    return effectiveMp4Options().changesStream(mp4BaseSize(), probe.frameRate, probe.videoCodec);
}

QSize RecordingPreviewBackend::mp4OutputSize() const
{
    return effectiveMp4Options().outputSize(mp4BaseSize());
}

QSize RecordingPreviewBackend::exportSize() const
{
    // GIF and WebP keep the picture's size; only MP4 has a Size option.
    return m_selectedFormat == MP4 ? mp4OutputSize() : mp4BaseSize();
}

int RecordingPreviewBackend::mp4OutputFrameRate() const
{
    return effectiveMp4Options().outputFrameRate(sourceProbe().frameRate);
}

int RecordingPreviewBackend::mp4SourceFrameRate() const
{
    return SnapTray::IntermediateQuality::effectiveFrameRate(sourceProbe().frameRate);
}

namespace {
// How far short of the recording's end its sample table may stop and still
// be trusted (the last frame's own length, rounding).
constexpr qint64 kSampleCoverageSlackMs = 1000;
} // namespace

qint64 RecordingPreviewBackend::mp4EstimatedBytes() const
{
    const VideoFileProbe& probe = sourceProbe();
    const qint64 durationMs = m_duration > 0 ? trimmedDuration() : probe.durationMs;
    if (durationMs <= 0) return 0;
    const int settingsQuality = outputQualityOverride() ? outputQualityOverride()()
                                                        : RecordingSettingsManager::instance().quality();
    const SnapTray::Mp4ExportOptions options = effectiveMp4Options();
    // The same edits that make save() transcode (PR #213 review).
    const bool edited = hasTrim() || hasCrop() || effectsActive() || hasTimelineEdits() || audioMixActive()
                        || mp4OptionsActive();
    if (!edited) {
        // Saved as recorded when it already meets the target (see performTranscode).
        const qint64 fileBytes = QFileInfo(m_videoPath).size();
        // Like the export: Default moves a recording made at the selected
        // quality; an explicit quality is judged by its bitrate (PR #197 review).
        const bool defaultQuality = options.quality == SnapTray::Mp4ExportOptions::Quality::Default;
        if ((!m_recordedAsIntermediate && defaultQuality)
            || SnapTray::IntermediateQuality::smartSaveShouldMove(probe, fileBytes,
                                                                  options.outputQuality(settingsQuality))) {
            return fileBytes;
        }
    }
    const QSize outputSize = mp4OutputSize();
    const int target = options.videoBitrate(outputSize, mp4OutputFrameRate(), settingsQuality);
    SnapTray::Mp4SizeEstimate::Source source;
    // The recording tells how much this content needs (PR #213 review), when:
    // - it is an intermediate, recorded better than any export asks for. A
    //   recording made at the user's quality of the day could be below what
    //   the export now asks, and that quality is not kept with it;
    // - no effect adds picture it never had (see pictureEffectsActive());
    // - its sample table was read and covers what is kept.
    if (m_recordedAsIntermediate && !pictureEffectsActive()) {
        source.keptVideoBytes = keptSourceVideoBytes();
        // Scaled only for a smaller resolution: a crop is not counted down,
        // since the kept area may hold most of the detail (a busy window on a
        // still screen). The estimate stays on the safe side.
        source.videoSize = mp4BaseSize();
        source.hevc = probe.videoCodec == QLatin1String(kVideoCodecHevc);
    }
    // durationMs is the kept, sped-up length.
    return SnapTray::Mp4SizeEstimate::expectedBytes(
        target, options.codec == SnapTray::Mp4ExportOptions::Codec::Hevc, source, outputSize,
        probe.hasAudio, durationMs);
}

qint64 RecordingPreviewBackend::keptSourceVideoBytes() const
{
    const auto samples = sourceSamples();
    if (!samples || !samples->isValid()) return -1;
    // A table cut short (a damaged or unusual file) would count what it
    // misses as nothing: then it does not tell.
    const qint64 needEndMs = m_duration > 0 ? m_duration : sourceProbe().durationMs;
    if (needEndMs <= 0 || samples->endMs() < needEndMs - kSampleCoverageSlackMs) return -1;
    if (m_duration <= 0) return samples->totalBytes(); // not loaded: nothing trimmed yet

    // Held in a local: segments() refers into the map, which a temporary in
    // the range-for would destroy before the loop starts (PR #213 review).
    const SnapTray::TimelineMap map = timelineMap();
    double bytes = 0;
    qint64 previousEndMs = -1;
    for (const SnapTray::TimelineSegment& segment : map.segments()) {
        const SnapTray::Mp4VideoSamples::RangeBytes range = samples->rangeBytes(segment.startMs, segment.endMs);
        // A faster part shows fewer of its frames (the output frame rate is
        // fixed); a slower one repeats them, which costs almost nothing.
        const double speed = segment.speed > 1.0 ? segment.speed : 1.0;
        if (segment.startMs == previousEndMs) {
            // Carries on from the part before (only the speed changed): the
            // export's stream continues, and its keyframes come per output
            // second like its other frames.
            bytes += double(range.frames) / speed;
        } else {
            // Starts the stream or follows a cut: a whole first frame at any
            // speed, the keyframe it starts on or the one it was predicted
            // from (PR #213 review).
            bytes += double(range.frames - range.firstKeyframe) / speed
                     + double(range.firstKeyframe + range.leadingKeyframe);
        }
        previousEndMs = segment.endMs;
    }
    return qint64(std::ceil(bytes));
}

std::shared_ptr<const SnapTray::Mp4VideoSamples> RecordingPreviewBackend::sourceSamples() const
{
    if (m_sourceSamples || m_sourceSamplesPending) return m_sourceSamples;
    // Read apart from the probe: a long fragmented recording has many
    // fragments to walk, and the save flow waits on the probe (PR #213 review).
    m_sourceSamplesPending = true;
    const QString path = m_videoPath;
    const QPointer<RecordingPreviewBackend> weakThis(const_cast<RecordingPreviewBackend*>(this));
    (void)QtConcurrent::run([path, weakThis]() {
        // Always answer, so the estimate never waits on a read that failed.
        std::shared_ptr<const SnapTray::Mp4VideoSamples> samples;
        try {
            samples = std::make_shared<const SnapTray::Mp4VideoSamples>(SnapTray::Mp4VideoSamples::read(path));
        } catch (const std::exception& e) {
            qWarning() << "RecordingPreviewBackend: cannot read the sample table:" << e.what();
            samples = std::make_shared<const SnapTray::Mp4VideoSamples>();
        }
        if (QCoreApplication* app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, [weakThis, samples]() {
                if (!weakThis) return;
                weakThis->m_sourceSamples = samples;
                weakThis->m_sourceSamplesPending = false;
                emit weakThis->mp4EstimateChanged();
            }, Qt::QueuedConnection);
        }
    });
    return nullptr;
}

QString RecordingPreviewBackend::mp4EstimateText() const
{
    const qint64 bytes = mp4EstimatedBytes();
    if (bytes <= 0) return QString();
    return tr("About %1").arg(QLocale().formattedDataSize(bytes));
}

void RecordingPreviewBackend::editMp4Options(const std::function<void(SnapTray::Mp4ExportOptions&)>& change)
{
    if (isProcessing()) return; // a running export keeps what it started with
    SnapTray::Mp4ExportOptions options = m_mp4Options;
    change(options);
    if (options == m_mp4Options) return;
    m_mp4Options = options;
    RecordingSettingsManager::instance().setMp4ExportOptions(options.toJson());
    emit mp4OptionsChanged();
}

void RecordingPreviewBackend::setMp4Resolution(int value)
{
    const int clamped = qBound(0, value, int(SnapTray::Mp4ExportOptions::Resolution::CustomWidth));
    editMp4Options([clamped](SnapTray::Mp4ExportOptions& o) { o.resolution = SnapTray::Mp4ExportOptions::Resolution(clamped); });
}

void RecordingPreviewBackend::setMp4CustomWidth(int value)
{
    const int clamped = qBound(SnapTray::Mp4ExportOptions::kMinCustomWidth, value,
                               SnapTray::Mp4ExportOptions::kMaxCustomWidth);
    editMp4Options([clamped](SnapTray::Mp4ExportOptions& o) { o.customWidth = clamped; });
}

void RecordingPreviewBackend::setMp4FrameRate(int value)
{
    const auto& choices = SnapTray::Mp4ExportOptions::kFrameRateChoices;
    if (std::find(std::begin(choices), std::end(choices), value) == std::end(choices)) return;
    editMp4Options([value](SnapTray::Mp4ExportOptions& o) { o.frameRate = value; });
}

void RecordingPreviewBackend::setMp4Quality(int value)
{
    const int clamped = qBound(0, value, int(SnapTray::Mp4ExportOptions::Quality::High));
    editMp4Options([clamped](SnapTray::Mp4ExportOptions& o) { o.quality = SnapTray::Mp4ExportOptions::Quality(clamped); });
}

void RecordingPreviewBackend::setMp4Codec(int value)
{
    const int clamped = qBound(0, value, int(SnapTray::Mp4ExportOptions::Codec::Hevc));
    editMp4Options([clamped](SnapTray::Mp4ExportOptions& o) { o.codec = SnapTray::Mp4ExportOptions::Codec(clamped); });
}

bool RecordingPreviewBackend::clickEffectsAvailable() const
{
    return cursorEffectsAvailable() && SnapTray::CursorEffectAvailability::of(m_cursorTimeline.get()).clicks;
}

bool RecordingPreviewBackend::cursorEffectsActive() const
{
    if (!cursorEffectsAvailable()) return false;
    return m_cursorEffects.showCursor || (m_cursorEffects.clickRipple && clickEffectsAvailable());
}

void RecordingPreviewBackend::setCursorEffects(const SnapTray::CursorEffectSettings& settings)
{
    if (m_cursorEffects == settings) return;
    m_cursorEffects = settings;
    if (m_selectedPointerHide >= int(m_cursorEffects.hiddenRanges.size())) setSelectedPointerHide(-1);
    emit cursorEffectsChanged();
}

void RecordingPreviewBackend::editCursorEffects(const std::function<void(SnapTray::CursorEffectSettings&)>& change)
{
    // A running export keeps the settings it started with.
    if (isProcessing()) return;
    auto settings = m_cursorEffects;
    change(settings);
    if (settings == m_cursorEffects) return;
    // Hidden ranges belong to this recording; the look carries over.
    RecordingSettingsManager::instance().setPointerEffects(settings.preferences().toJson());
    setCursorEffects(settings);
    recordEdit();
}

void RecordingPreviewBackend::setShowCursor(bool value)
{
    editCursorEffects([value](SnapTray::CursorEffectSettings& s) { s.showCursor = value; });
}

void RecordingPreviewBackend::setCursorScale(qreal value)
{
    const double clamped = std::clamp(double(value), SnapTray::CursorEffectSettings::kMinScale,
                                      SnapTray::CursorEffectSettings::kMaxScale);
    editCursorEffects([clamped](SnapTray::CursorEffectSettings& s) { s.scale = clamped; });
}

void RecordingPreviewBackend::setCursorSmoothing(int value)
{
    const int level = qBound(0, value, int(SnapTray::CursorEffectSettings::Smoothing::High));
    editCursorEffects([level](SnapTray::CursorEffectSettings& s) { s.smoothing = SnapTray::CursorEffectSettings::Smoothing(level); });
}

void RecordingPreviewBackend::setClickRipple(bool value)
{
    editCursorEffects([value](SnapTray::CursorEffectSettings& s) { s.clickRipple = value; });
}

void RecordingPreviewBackend::setCursorIdleHide(int ms)
{
    const qint64 clamped = std::clamp<qint64>(ms, 0, SnapTray::CursorEffectSettings::kMaxIdleHideMs);
    editCursorEffects([clamped](SnapTray::CursorEffectSettings& s) { s.idleHideMs = clamped; });
}

void RecordingPreviewBackend::setCursorMotionBlur(int level)
{
    const int clamped = qBound(0, level, int(SnapTray::CursorEffectSettings::MotionBlur::High));
    editCursorEffects([clamped](SnapTray::CursorEffectSettings& s) {
        s.motionBlur = SnapTray::CursorEffectSettings::MotionBlur(clamped);
    });
}

void RecordingPreviewBackend::setCursorStillEnd(int ms)
{
    const qint64 clamped = std::clamp<qint64>(ms, 0, SnapTray::CursorEffectSettings::kMaxStillEndMs);
    editCursorEffects([clamped](SnapTray::CursorEffectSettings& s) { s.stillEndMs = clamped; });
}

QVariantList RecordingPreviewBackend::pointerHiddenRanges() const
{
    QVariantList list;
    for (const auto& range : m_cursorEffects.hiddenRanges) {
        list.append(QVariantMap{{QStringLiteral("start"), range.startMs}, {QStringLiteral("end"), range.endMs}});
    }
    return list;
}

void RecordingPreviewBackend::setSelectedPointerHide(int index)
{
    index = index >= 0 && index < int(m_cursorEffects.hiddenRanges.size()) ? index : -1;
    if (index >= 0) {
        setSelectedZoom(-1);
        setSelectedMask(-1);
        setSelectedOverlay(-1);
    }
    if (m_selectedPointerHide == index) return;
    m_selectedPointerHide = index;
    emit selectedPointerHideChanged();
}

namespace {
int hiddenRangeIndexAt(const SnapTray::CursorEffectSettings& settings, qint64 ms)
{
    for (int i = 0; i < int(settings.hiddenRanges.size()); ++i) {
        if (settings.hiddenRanges[i].contains(ms)) return i;
    }
    return -1;
}
} // namespace

int RecordingPreviewBackend::addPointerHideAt(qint64 positionMs)
{
    constexpr qint64 kNewHiddenRangeMs = 2000;
    if (isProcessing() || !cursorEffectsAvailable()) return -1;
    const qint64 end = m_duration > 0 ? m_duration : positionMs + kNewHiddenRangeMs;
    const qint64 start = std::clamp<qint64>(positionMs, 0, end);
    const qint64 rangeEnd = qMin(end, start + kNewHiddenRangeMs); // not std::min: windows.h defines min
    if (rangeEnd - start < SnapTray::CursorEffectSettings::kMinHiddenRangeMs) return -1;
    editCursorEffects([start, rangeEnd](SnapTray::CursorEffectSettings& s) {
        auto ranges = s.hiddenRanges;
        ranges.push_back({start, rangeEnd});
        s.hiddenRanges = SnapTray::CursorEffectSettings::normalizedRanges(std::move(ranges));
    });
    const int index = hiddenRangeIndexAt(m_cursorEffects, start);
    setSelectedPointerHide(index);
    return index;
}

void RecordingPreviewBackend::removePointerHide(int index)
{
    editCursorEffects([index](SnapTray::CursorEffectSettings& s) {
        if (index >= 0 && index < int(s.hiddenRanges.size())) s.hiddenRanges.erase(s.hiddenRanges.begin() + index);
    });
    setSelectedPointerHide(-1);
}

bool RecordingPreviewBackend::setPointerHideRange(int index, qint64 startMs, qint64 endMs)
{
    if (isProcessing() || index < 0 || index >= int(m_cursorEffects.hiddenRanges.size())) return false;
    if (startMs < 0 || endMs - startMs < SnapTray::CursorEffectSettings::kMinHiddenRangeMs) return false;
    if (m_duration > 0 && (startMs >= m_duration || endMs > m_duration)) return false;
    editCursorEffects([index, startMs, endMs](SnapTray::CursorEffectSettings& s) {
        auto ranges = s.hiddenRanges;
        ranges[index] = {startMs, endMs};
        s.hiddenRanges = SnapTray::CursorEffectSettings::normalizedRanges(std::move(ranges));
    });
    setSelectedPointerHide(hiddenRangeIndexAt(m_cursorEffects, startMs));
    return true;
}

bool RecordingPreviewBackend::effectsActive() const
{
    return !m_redaction.isEmpty() || cursorEffectsActive() || pictureEffectsActive();
}

bool RecordingPreviewBackend::pictureEffectsActive() const
{
    // Zoom moves all of the picture, overlays and key hints draw new detail
    // (and fade it), and a frame may put an image behind it. Pointer effects
    // and masks only change what was recorded, a small part of it at that.
    return (zoomAvailable() && m_zoom.isActive()) || appearanceActive() || keyOverlayActive()
        || (!m_overlays.isEmpty() && overlaysAvailable());
}

QVariantList RecordingPreviewBackend::masks() const
{
    QVariantList list;
    for (const auto& mask : m_redaction.masks) {
        list.append(QVariantMap{{QStringLiteral("x"), mask.rect.x()},
                                {QStringLiteral("y"), mask.rect.y()},
                                {QStringLiteral("width"), mask.rect.width()},
                                {QStringLiteral("height"), mask.rect.height()},
                                {QStringLiteral("start"), mask.startMs},
                                {QStringLiteral("end"), mask.endMs},
                                {QStringLiteral("style"), int(mask.style)}});
    }
    return list;
}

void RecordingPreviewBackend::setSelectedMask(int index)
{
    index = index >= 0 && index < int(m_redaction.masks.size()) ? index : -1;
    if (index >= 0) {
        setSelectedZoom(-1);
        setSelectedOverlay(-1);
        setSelectedPointerHide(-1);
    }
    if (m_selectedMask == index) return;
    m_selectedMask = index;
    emit selectedMaskChanged();
}

void RecordingPreviewBackend::editRedaction(const std::function<void(SnapTray::RedactionTrack&)>& change)
{
    if (isProcessing()) return; // a running export keeps what it started with
    SnapTray::RedactionTrack redaction = m_redaction;
    change(redaction);
    if (redaction == m_redaction) return;
    m_redaction = redaction;
    if (m_selectedMask >= int(m_redaction.masks.size())) setSelectedMask(-1);
    emit masksChanged();
    recordEdit();
}

int RecordingPreviewBackend::addMask()
{
    if (isProcessing() || !masksAvailable() || m_videoSize.isEmpty()) return -1;
    // A quarter of the picture in the middle, for the whole recording.
    const QSize size(qMax(SnapTray::RedactionTrack::kMinMaskSide, m_videoSize.width() / 4),
                     qMax(SnapTray::RedactionTrack::kMinMaskSide, m_videoSize.height() / 4));
    SnapTray::RedactionMask mask;
    mask.rect = QRect(QPoint((m_videoSize.width() - size.width()) / 2, (m_videoSize.height() - size.height()) / 2), size);
    editRedaction([&mask](SnapTray::RedactionTrack& r) { r.masks.push_back(mask); });
    const int index = int(m_redaction.masks.size()) - 1;
    setSelectedMask(index);
    return index;
}

void RecordingPreviewBackend::removeMask(int index)
{
    editRedaction([index](SnapTray::RedactionTrack& r) {
        if (index >= 0 && index < int(r.masks.size())) r.masks.erase(r.masks.begin() + index);
    });
    setSelectedMask(-1);
}

void RecordingPreviewBackend::setMaskRectFromView(int index, const QRectF& viewRect, const QRectF& contentRect)
{
    if (m_videoSize.isEmpty() || contentRect.isEmpty()) return;
    const double sx = m_videoSize.width() / contentRect.width();
    const double sy = m_videoSize.height() / contentRect.height();
    // Outward to whole video pixels, inside the picture, never smaller than the minimum.
    QRect rect(QPoint(qFloor((viewRect.left() - contentRect.x()) * sx), qFloor((viewRect.top() - contentRect.y()) * sy)),
               QPoint(qCeil((viewRect.right() - contentRect.x()) * sx) - 1, qCeil((viewRect.bottom() - contentRect.y()) * sy) - 1));
    rect = rect.intersected(QRect(QPoint(0, 0), m_videoSize));
    if (rect.width() < SnapTray::RedactionTrack::kMinMaskSide || rect.height() < SnapTray::RedactionTrack::kMinMaskSide) {
        return;
    }
    editRedaction([index, rect](SnapTray::RedactionTrack& r) {
        if (index >= 0 && index < int(r.masks.size())) r.masks[index].rect = rect;
    });
}

QRectF RecordingPreviewBackend::maskRectInView(int index, const QRectF& contentRect) const
{
    if (index < 0 || index >= int(m_redaction.masks.size()) || m_videoSize.isEmpty()) return {};
    const QRect rect = m_redaction.masks[index].rect;
    const double sx = contentRect.width() / m_videoSize.width();
    const double sy = contentRect.height() / m_videoSize.height();
    return QRectF(contentRect.x() + rect.x() * sx, contentRect.y() + rect.y() * sy, rect.width() * sx, rect.height() * sy);
}

void RecordingPreviewBackend::setMaskStyle(int index, int style)
{
    const int clamped = qBound(0, style, int(SnapTray::RedactionMask::Style::Blur));
    editRedaction([index, clamped](SnapTray::RedactionTrack& r) {
        if (index >= 0 && index < int(r.masks.size())) r.masks[index].style = SnapTray::RedactionMask::Style(clamped);
    });
}

bool RecordingPreviewBackend::setMaskRange(int index, qint64 startMs, qint64 endMs)
{
    if (isProcessing() || index < 0 || index >= int(m_redaction.masks.size())) return false;
    if (startMs < 0 || (endMs >= 0 && endMs <= startMs) || (m_duration > 0 && startMs >= m_duration)) return false;
    editRedaction([index, startMs, endMs](SnapTray::RedactionTrack& r) {
        r.masks[index].startMs = startMs;
        r.masks[index].endMs = endMs;
    });
    return true;
}

void RecordingPreviewBackend::installPreviewFrameProcessor()
{
    auto* player = m_view && m_view->rootObject()
        ? m_view->rootObject()->findChild<VideoPlaybackItem*>(QStringLiteral("previewVideoPlayer"))
        : nullptr;
    if (!player) return;
    // The preview shows masks burned into the real pixels, with the same
    // function as the exports, never a lookalike drawn on top.
    QPointer<RecordingPreviewBackend> weakThis(this);
    player->setFrameProcessor([weakThis](QImage& frame, qint64 positionMs) {
        if (!weakThis) return;
        const QSize source = weakThis->m_videoSize.isEmpty() ? frame.size() : weakThis->m_videoSize;
        if (!weakThis->m_redaction.isEmpty()) {
            // positionMs is this frame's own time (#175), so the preview masks
            // exactly the frames the export does, with the same function; no
            // extra margin that would show frames masked that are exported
            // unmasked (#177 review).
            weakThis->m_redaction.apply(frame, positionMs, QRectF(QPointF(0, 0), QSizeF(source)));
        }
        // A blurred background (#184) is made from this masked frame, cropped
        // like the export's picture.
        if (weakThis->appearanceActive()
            && weakThis->m_appearance.background == SnapTray::RecordingAppearance::Background::BlurredVideo) {
            QRect picture = weakThis->m_cropRect.isEmpty() ? frame.rect() : weakThis->m_cropRect;
            if (frame.size() != source && !source.isEmpty()) {
                const double sx = double(frame.width()) / source.width();
                const double sy = double(frame.height()) / source.height();
                picture = QRect(qRound(picture.x() * sx), qRound(picture.y() * sy), qRound(picture.width() * sx),
                                qRound(picture.height() * sy));
            }
            weakThis->m_appearancePreviewFrame =
                SnapTray::RecordingAppearanceRenderer::blurSourceFor(frame.copy(picture.intersected(frame.rect())));
            emit weakThis->appearancePreviewFrameChanged();
        } else if (!weakThis->m_appearancePreviewFrame.isNull()) {
            weakThis->m_appearancePreviewFrame = QImage();
        }
    });
    connect(this, &RecordingPreviewBackend::masksChanged, player, &VideoPlaybackItem::reprocessFrame);
    connect(this, &RecordingPreviewBackend::appearanceChanged, player, &VideoPlaybackItem::reprocessFrame);
    // The blurred background is made from the cropped picture: a new crop
    // (also undo or clear) needs it again, even while paused (PR #198 review).
    connect(this, &RecordingPreviewBackend::cropRectChanged, player, &VideoPlaybackItem::reprocessFrame);
    // Separate tracks (#189): the preview hears the new mix as it is set.
    if (m_hasSeparateTracks) {
        m_previewAudio = std::make_shared<PreviewAudio>(m_videoPath, m_audioMix);
        if (m_previewAudio->mixer.isValid()) {
            std::weak_ptr<PreviewAudio> weakAudio = m_previewAudio;
            player->setAudioSource([weakAudio](qint64 startFrame, int frames) {
                const auto audio = weakAudio.lock();
                if (!audio) return QByteArray(qMax(0, frames) * SnapTray::AudioTracks::kBytesPerFrame, '\0');
                QMutexLocker lock(&audio->mutex);
                return audio->mixer.read(startFrame, frames);
            });
            const auto audio = m_previewAudio;
            connect(this, &RecordingPreviewBackend::audioMixChanged, this, [this, audio] {
                QMutexLocker lock(&audio->mutex);
                audio->mixer.setSettings(m_audioMix);
            });
        }
    }
}

bool RecordingPreviewBackend::zoomAvailable() const
{
    return formatCanDrawEffects();
}

bool RecordingPreviewBackend::autoZoomAvailable() const
{
    return zoomAvailable() && SnapTray::CursorEffectAvailability::of(m_cursorTimeline.get()).clicks;
}

QRectF RecordingPreviewBackend::zoomBounds() const
{
    const QSize size = m_videoSize.isEmpty() && m_cursorTimeline ? m_cursorTimeline->info.frameSize : m_videoSize;
    return m_cropRect.isEmpty() ? QRectF(QPointF(0, 0), QSizeF(size)) : QRectF(m_cropRect);
}

void RecordingPreviewBackend::editZoom(const std::function<void(SnapTray::ZoomTrack&)>& change)
{
    if (isProcessing()) return; // a running export keeps what it started with
    SnapTray::ZoomTrack zoom = m_zoom;
    change(zoom);
    if (zoom == m_zoom) return;
    m_zoom = zoom;
    if (m_selectedZoom >= int(m_zoom.segments().size())) setSelectedZoom(-1);
    emit zoomChanged();
    recordEdit();
}

void RecordingPreviewBackend::setZoomEnabled(bool value)
{
    editZoom([value](SnapTray::ZoomTrack& z) { z.enabled = value; });
}

void RecordingPreviewBackend::setZoomReduceMotion(bool value)
{
    editZoom([value](SnapTray::ZoomTrack& z) { z.reduceMotion = value; });
}

QVariantList RecordingPreviewBackend::zoomSegments() const
{
    QVariantList list;
    for (const auto& s : m_zoom.segments()) {
        list.append(QVariantMap{{QStringLiteral("start"), s.startMs},
                                {QStringLiteral("end"), s.endMs},
                                {QStringLiteral("scale"), s.scale},
                                {QStringLiteral("manual"), s.manual},
                                {QStringLiteral("follow"), s.follow}});
    }
    return list;
}

void RecordingPreviewBackend::setSelectedZoom(int index)
{
    index = index >= 0 && index < int(m_zoom.segments().size()) ? index : -1;
    if (index >= 0) {
        setSelectedMask(-1);
        setSelectedOverlay(-1);
        setSelectedPointerHide(-1);
    }
    if (m_selectedZoom == index) return;
    m_selectedZoom = index;
    emit selectedZoomChanged();
}

QString RecordingPreviewBackend::appearanceImageName() const
{
    return QFileInfo(m_appearance.imagePath).fileName();
}

int RecordingPreviewBackend::generateAutoZoom()
{
    if (isProcessing() || !autoZoomAvailable()) return 0;
    SnapTray::AutoZoomOptions options;
    options.bounds = zoomBounds();
    options.durationMs = m_duration;
    auto generated = SnapTray::generateAutoZoom(*m_cursorTimeline, options);
    const int count = int(generated.size());
    // The segments are rebuilt, so an index would point at a different zoom:
    // drop the selection while they change, then restore it only on the same
    // (manual) segment.
    std::optional<SnapTray::ZoomSegment> selected;
    if (m_selectedZoom >= 0 && m_selectedZoom < int(m_zoom.segments().size())) {
        selected = m_zoom.segments()[size_t(m_selectedZoom)];
    }
    setSelectedZoom(-1);
    editZoom([&generated](SnapTray::ZoomTrack& z) {
        // The user's own segments win; automatic ones that collide with them go.
        std::vector<SnapTray::ZoomSegment> segments;
        for (const auto& s : z.segments()) {
            if (s.manual) segments.push_back(s);
        }
        const size_t manualCount = segments.size();
        for (const auto& candidate : generated) {
            bool collides = false;
            for (size_t i = 0; i < manualCount; ++i) {
                collides |= candidate.startMs < segments[i].endMs && segments[i].startMs < candidate.endMs;
            }
            if (!collides) segments.push_back(candidate);
        }
        z.enabled = true;
        z.setSegments(std::move(segments));
    });
    if (selected && selected->manual) {
        const auto& segments = m_zoom.segments();
        for (size_t i = 0; i < segments.size(); ++i) {
            if (segments[i] == *selected) setSelectedZoom(int(i));
        }
    }
    return count;
}

int RecordingPreviewBackend::addZoomAt(qint64 positionMs)
{
    if (isProcessing() || !zoomAvailable()) return -1;
    constexpr qint64 kNewZoomMs = 2000;
    constexpr double kNewZoomScale = 2.0;
    const QRectF bounds = zoomBounds();
    if (bounds.isEmpty()) return -1;
    // Only into free time: existing zooms are never trimmed or replaced
    // (#176 review). Inside one, nothing is added.
    const qint64 start = qMax<qint64>(0, positionMs);
    if (m_zoom.segmentAt(start) >= 0) return -1;
    qint64 end = m_duration > 0 ? qMin(m_duration, start + kNewZoomMs) : start + kNewZoomMs;
    for (const auto& existing : m_zoom.segments()) {
        if (existing.startMs > start) {
            end = qMin(end, existing.startMs);
            break;
        }
    }
    if (end - start < SnapTray::ZoomTrack::kMinSegmentMs) return -1;
    SnapTray::ZoomSegment segment;
    segment.startMs = start;
    segment.endMs = end;
    segment.scale = kNewZoomScale;
    segment.focus = bounds.center();
    if (m_cursorTimeline) {
        if (const auto* sample = m_cursorTimeline->sampleAt(positionMs); sample && sample->visible
            && bounds.contains(QPointF(sample->position))) {
            segment.focus = QPointF(sample->position);
        }
    }
    segment.manual = true;
    editZoom([&segment](SnapTray::ZoomTrack& z) {
        auto segments = z.segments();
        segments.push_back(segment);
        z.enabled = true;
        z.setSegments(std::move(segments));
    });
    // Select the zoom that was inserted (it starts exactly at `start`).
    int index = -1;
    for (int i = 0; i < int(m_zoom.segments().size()); ++i) {
        if (m_zoom.segments()[i].startMs == start) index = i;
    }
    setSelectedZoom(index);
    return index;
}

void RecordingPreviewBackend::removeZoom(int index)
{
    editZoom([index](SnapTray::ZoomTrack& z) {
        auto segments = z.segments();
        if (index < 0 || index >= int(segments.size())) return;
        segments.erase(segments.begin() + index);
        z.setSegments(std::move(segments));
    });
    setSelectedZoom(-1);
}

std::shared_ptr<const SnapTray::ZoomTrack> RecordingPreviewBackend::preparedZoom() const
{
    if (!m_preparedZoom) {
        auto zoom = std::make_shared<SnapTray::ZoomTrack>(m_zoom);
        if (m_cursorTimeline)
            zoom->prepareFollow(*m_cursorTimeline, m_cursorEffects, timelineMap(), zoomBounds(), hasTimelineEdits());
        m_preparedZoom = std::move(zoom);
    }
    return m_preparedZoom;
}

void RecordingPreviewBackend::setZoomFollow(int index, bool follow)
{
    if (!cursorEffectsAvailable()) return;
    editZoom([index, follow](SnapTray::ZoomTrack& z) {
        auto segments = z.segments();
        if (index < 0 || index >= int(segments.size())) return;
        segments[index].follow = follow;
        segments[index].manual = true;
        z.setSegments(std::move(segments));
    });
}

void RecordingPreviewBackend::setZoomScale(int index, qreal scale)
{
    editZoom([index, scale](SnapTray::ZoomTrack& z) {
        auto segments = z.segments();
        if (index < 0 || index >= int(segments.size())) return;
        segments[index].scale = scale;
        segments[index].manual = true;
        z.setSegments(std::move(segments));
    });
}

bool RecordingPreviewBackend::setZoomRange(int index, qint64 startMs, qint64 endMs)
{
    const auto& current = m_zoom.segments();
    if (isProcessing() || index < 0 || index >= int(current.size())) return false;
    // Must stay between its neighbours and long enough to see.
    const qint64 lower = index > 0 ? current[index - 1].endMs : 0;
    const qint64 upper = index + 1 < int(current.size()) ? current[index + 1].startMs
                                                         : (m_duration > 0 ? m_duration : endMs);
    if (startMs < lower || endMs > upper || endMs - startMs < SnapTray::ZoomTrack::kMinSegmentMs) return false;
    editZoom([index, startMs, endMs](SnapTray::ZoomTrack& z) {
        auto segments = z.segments();
        segments[index].startMs = startMs;
        segments[index].endMs = endMs;
        segments[index].manual = true;
        z.setSegments(std::move(segments));
    });
    return true;
}

void RecordingPreviewBackend::setZoomFocusFromView(int index, const QPointF& viewPoint, const QRectF& contentRect)
{
    if (m_videoSize.isEmpty() || contentRect.isEmpty()) return;
    const QPointF videoPoint((viewPoint.x() - contentRect.x()) * m_videoSize.width() / contentRect.width(),
                             (viewPoint.y() - contentRect.y()) * m_videoSize.height() / contentRect.height());
    editZoom([index, videoPoint](SnapTray::ZoomTrack& z) {
        auto segments = z.segments();
        if (index < 0 || index >= int(segments.size())) return;
        segments[index].focus = videoPoint;
        segments[index].manual = true;
        z.setSegments(std::move(segments));
    });
}

QRectF RecordingPreviewBackend::zoomViewportInView(qint64 positionMs, const QRectF& contentRect) const
{
    const QRectF bounds = zoomBounds();
    if (m_videoSize.isEmpty() || contentRect.isEmpty() || bounds.isEmpty()) return contentRect;
    const QRectF shown = zoomAvailable() ? preparedZoom()->viewportAt(positionMs, bounds) : bounds;
    const double sx = contentRect.width() / m_videoSize.width();
    const double sy = contentRect.height() / m_videoSize.height();
    return QRectF(contentRect.x() + shown.x() * sx, contentRect.y() + shown.y() * sy,
                  shown.width() * sx, shown.height() * sy);
}

std::function<int()>& RecordingPreviewBackend::outputQualityOverride()
{
    static std::function<int()> override;
    return override;
}

std::function<qint64(const QString&)>& RecordingPreviewBackend::freeBytesForPathOverride()
{
    static std::function<qint64(const QString&)> override;
    return override;
}

RecordingPreviewBackend::TranscoderFactory& RecordingPreviewBackend::transcoderFactoryOverride()
{
    static TranscoderFactory factory;
    return factory;
}

RecordingPreviewBackend::~RecordingPreviewBackend()
{
    m_exportCancelToken->store(true);
    // Never saved: drop the derived export. The recording itself is untouched.
    if (m_awaitingSave && m_pendingSaveOutput != m_videoPath) {
        QFile::remove(m_pendingSaveOutput);
    }

    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        // Disconnect to prevent re-entrant signals during teardown
        disconnect(m_view, nullptr, this, nullptr);
        m_view->close();
        // Delete synchronously so the QML tree is destroyed before this QObject's
        // destructor notifies QML that the "backend" context property is gone.
        // Do NOT call setContextProperty(nullptr) before delete; that would trigger
        // QML binding re-evaluation while the tree still exists, causing null-reference errors.
        delete m_view;
        m_view = nullptr;
    }
}

void RecordingPreviewBackend::ensureView()
{
    if (m_view)
        return;

    auto& mgr = SnapTray::QmlOverlayManager::instance();
    m_view = mgr.createUtilityWindow();
#ifdef Q_OS_WIN
    // Qt 6.11's Windows backend turns a plain Qt::Window that carries
    // WindowStaysOnTopHint into a caption-less WS_POPUP frame; spelling out
    // the caption hints keeps the title bar and its min/max/close buttons.
    m_view->setFlags(Qt::Window | Qt::WindowStaysOnTopHint | Qt::CustomizeWindowHint
                     | Qt::WindowTitleHint | Qt::WindowSystemMenuHint
                     | Qt::WindowMinMaxButtonsHint | Qt::WindowCloseButtonHint);
#else
    m_view->setFlag(Qt::WindowStaysOnTopHint, true);
#endif
    m_view->setMinimumSize(QSize(640, 480));
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setTitle(tr("Recording Preview"));
    m_view->installEventFilter(this);
    if (qApp) qApp->installEventFilter(this);
    m_cursorSurfaceId = CursorSurfaceSupport::registerManagedSurface(
        m_view, QStringLiteral("RecordingPreviewBackend"));
    m_cursorOwnerId = CursorSurfaceSupport::defaultOwnerId(QStringLiteral("RecordingPreviewBackend"));

    // Set backend as context property before loading QML
    m_view->rootContext()->setContextProperty(
        QStringLiteral("backend"), this);

    m_view->setSource(
        QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingPreview.qml")));
    installPreviewFrameProcessor();

    // Handle window close
    connect(m_view, &QQuickView::closing, this, [this](QQuickCloseEvent *) {
        finishClose();
    });
}

bool RecordingPreviewBackend::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event && event->type() == QEvent::Close && m_isProcessing) {
        static_cast<QCloseEvent*>(event)->ignore();
        // Never drop an export silently: let the user choose to keep or cancel it.
        if (canCancelExport() && !m_applicationQuitting && !m_exportCloseQuestionPending) {
            m_exportCloseQuestionPending = true;
            emit exportCloseQuestionChanged();
        }
        return true;
    }
    // QCoreApplication::quit() closes every window and gives up if one refuses.
    // Let that close through so Exit keeps working; the flag only covers the
    // quit being handled right now.
    if (watched == qApp && event && event->type() == QEvent::Quit) {
        m_applicationQuitting = true;
        QTimer::singleShot(0, this, [this] { m_applicationQuitting = false; });
        return QObject::eventFilter(watched, event);
    }
    // The title-bar close button must not delete an unsaved recording silently.
    if (watched == m_view && event && event->type() == QEvent::Close
        && !m_closeApproved && !m_applicationQuitting && !m_saved && !m_closeHandled
        && !(isProject() && !hasUnsavedProjectChanges())) {
        static_cast<QCloseEvent*>(event)->ignore();
        requestDiscard();
        return true;
    }

    if (watched == m_view && event) {
        if (event->type() == QEvent::Hide || event->type() == QEvent::Close) {
            CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        } else if (CursorSurfaceSupport::isPointerRefreshEvent(event->type())) {
            syncCursorSurface();
        }
    }

    return QObject::eventFilter(watched, event);
}

void RecordingPreviewBackend::applyPlatformWindowFlags()
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView *nsView = reinterpret_cast<NSView *>(m_view->winId());
    if (!nsView)
        return;

    NSWindow *nsWindow = [nsView window];
    if (!nsWindow)
        return;

    [nsWindow setHidesOnDeactivate:NO];
#endif
}

void RecordingPreviewBackend::show()
{
    ensureView();

    // Center on primary screen
    if (QScreen *screen = QGuiApplication::primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        int w = 1024, h = 768;
        int x = screenGeometry.center().x() - w / 2;
        int y = screenGeometry.center().y() - h / 2;
        m_view->setGeometry(x, y, w, h);
    } else {
        m_view->resize(1024, 768);
    }

    m_view->show();
    applyPlatformWindowFlags();
    m_view->raise();
    m_view->requestActivate();
    syncCursorSurface();
}

void RecordingPreviewBackend::close()
{
    if (isProcessing() || m_closeHandled) return;
    m_closeApproved = true;
    if (m_view) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        m_view->close();
    } else {
        finishClose();
    }
}

void RecordingPreviewBackend::finishClose()
{
    if (m_closeHandled) return;
    m_closeHandled = true;
    const QString discardPath = m_videoPath;
    const bool saved = m_saved;
    // Preserve teardown ordering: the owner schedules deletion of playback
    // resources before the queued discard handler removes the temporary file.
    emit closed(saved);
    // Quitting SnapTray is not a choice to discard: the recording and its
    // session manifest stay, and the next launch offers to recover it.
    if (!saved && !m_applicationQuitting) emit discardRequested(discardPath);
}

void RecordingPreviewBackend::syncCursorSurface()
{
    if (!m_view || m_cursorSurfaceId.isEmpty() || m_cursorOwnerId.isEmpty()) {
        return;
    }

    if (!m_view->isVisible()) {
        CursorSurfaceSupport::clearWindowSurface(m_cursorSurfaceId, m_cursorOwnerId);
        return;
    }

    CursorSurfaceSupport::syncWindowSurface(
        m_view, m_cursorSurfaceId, m_cursorOwnerId, CursorRequestSource::SurfaceDefault);
}

void RecordingPreviewBackend::setDefaultOutputFormat(int formatInt)
{
    int format = qBound(0, formatInt, 2);
    setSelectedFormat(format);
}

// ---------- Property setters ----------

void RecordingPreviewBackend::setTrimStart(qint64 ms)
{
    ms = qBound(qint64(0), ms, m_duration);
    if (m_trimStart != ms) {
        m_trimStart = ms;
        emit trimRangeChanged();
    }
}

void RecordingPreviewBackend::setTrimEnd(qint64 ms)
{
    if (ms < 0)
        ms = -1;
    else
        ms = qMax(ms, m_trimStart + 1);
    if (m_trimEnd != ms) {
        m_trimEnd = ms;
        emit trimRangeChanged();
    }
}

qint64 RecordingPreviewBackend::trimEnd() const
{
    return (m_trimEnd < 0) ? m_duration : m_trimEnd;
}

bool RecordingPreviewBackend::hasTrim() const
{
    return m_trimStart > 0 || (m_trimEnd >= 0 && m_trimEnd < m_duration);
}

qint64 RecordingPreviewBackend::trimmedDuration() const
{
    // The exported length: cut-out parts and speed changes included (#185).
    if (m_duration > 0 && !m_timeline.isEmpty()) return timelineMap().outputDurationMs();
    return trimEnd() - m_trimStart;
}

qint64 RecordingPreviewBackend::effectsClipEnd() const
{
    if (trimEnd() <= 0) return -1;
    const SnapTray::TimelineMap map = timelineMap();
    return map.isEmpty() ? trimEnd() : map.sourceEndMs();
}

SnapTray::TimelineMap RecordingPreviewBackend::timelineMap() const
{
    return m_timeline.map(m_trimStart, trimEnd());
}

bool RecordingPreviewBackend::hasTimelineEdits() const
{
    if (m_timeline.isEmpty() || m_duration <= 0) return false;
    // Cutting the first or last part is still a change: the export starts or
    // ends elsewhere than the trim says.
    const SnapTray::TimelineMap map = timelineMap();
    return !map.isPlainTrim() || map.sourceStartMs() != m_trimStart || map.sourceEndMs() != trimEnd();
}

bool RecordingPreviewBackend::needsSegments() const
{
    return hasTimelineEdits() && !timelineMap().isPlainTrim();
}

bool RecordingPreviewBackend::timelineEditsAvailable() const
{
    return m_selectedFormat != MP4 || mp4Capabilities().segments;
}

void RecordingPreviewBackend::setProject(const SnapTray::RecordingProject& project)
{
    m_project = project;
    const QJsonObject choices = SnapTray::RecordingRecipe::exportChoices(project.recipe);
    if (choices.contains(QStringLiteral("format"))) setSelectedFormat(choices.value(QStringLiteral("format")).toInt(MP4));
    if (choices.contains(QStringLiteral("mp4"))) {
        m_mp4Options = SnapTray::Mp4ExportOptions::fromJson(choices.value(QStringLiteral("mp4")).toObject());
        emit mp4OptionsChanged();
    }
    emit projectChanged();
    // The edits need the picture's size and length: they are applied once
    // the player has reported both, through the same notifying updates as
    // any recording (PR #203 review).
    m_projectRecipePending = true;
    applyPendingProjectRecipe();
}

void RecordingPreviewBackend::applyPendingProjectRecipe()
{
    if (!m_projectRecipePending || !m_project || m_videoSize.isEmpty() || m_duration <= 0) return;
    m_projectRecipePending = false;
    if (const auto state = SnapTray::RecordingRecipe::editState(m_project->recipe)) {
        m_restoringProject = true;
        applyEditState(*state);
        m_restoringProject = false;
    }
    // Undo starts from the project as it was saved.
    m_editHistory.reset(currentEditState());
    m_projectOpenedRecipe = currentRecipe();
    emit editHistoryChanged();
    emit projectChanged(); // Save is possible now
}

bool RecordingPreviewBackend::hasUnsavedProjectChanges() const
{
    // Nothing can be edited before the saved edits are applied.
    if (!m_project || projectLoading()) return false;
    return currentRecipe() != m_projectOpenedRecipe;
}

void RecordingPreviewBackend::setInitialCrop(const QRect& videoRect)
{
    if (videoRect.isEmpty()) return;
    // Checked against the picture once the player has reported its size
    // (the source is never probed on the GUI thread).
    m_pendingInitialCrop = videoRect;
    emit projectChanged(); // Save waits for it (PR #203 review)
    applyPendingInitialCrop();
}

void RecordingPreviewBackend::applyPendingInitialCrop()
{
    if (m_pendingInitialCrop.isEmpty() || m_videoSize.isEmpty()) return;
    setCropRect(std::exchange(m_pendingInitialCrop, QRect()));
    m_editHistory.reset(currentEditState());
    emit editHistoryChanged();
    emit projectChanged(); // Save is possible now
}

bool RecordingPreviewBackend::keepsProject() const
{
    return m_project.has_value() || RecordingSettingsManager::instance().keepProjects();
}

QJsonObject RecordingPreviewBackend::currentRecipe() const
{
    return SnapTray::RecordingRecipe::toJson(
        currentEditState(), {{QStringLiteral("format"), m_selectedFormat}, {QStringLiteral("mp4"), m_mp4Options.toJson()}});
}

namespace {
// The library's thumbnail, at most this size.
const QSize kProjectThumbnailSize(320, 180);
}

QJsonObject RecordingPreviewBackend::recipeForProject(const QString& projectDir, QString* copied) const
{
    QJsonObject recipe = currentRecipe();
    const QString image = m_appearance.imagePath;
    if (image.isEmpty() || !QFileInfo::exists(image)) return recipe;
    QString owned = image;
    // Already the project's own copy: nothing to do. Otherwise a new name,
    // so the copy the saved manifest uses stays until a new manifest is
    // written (PR #203 review).
    if (QFileInfo(image).absolutePath() != QFileInfo(projectDir).absoluteFilePath()) {
        owned = QDir(projectDir).filePath(QStringLiteral("background-%1.%2")
                                              .arg(QDateTime::currentMSecsSinceEpoch())
                                              .arg(QFileInfo(image).suffix()));
        if (!QFile::copy(image, owned)) return recipe; // keeps the outside path
        if (copied) *copied = owned;
    }
    QJsonObject edit = recipe.value(QStringLiteral("edit")).toObject();
    QJsonObject appearance = edit.value(QStringLiteral("appearance")).toObject();
    appearance.insert(QStringLiteral("imagePath"), owned);
    edit.insert(QStringLiteral("appearance"), appearance);
    recipe.insert(QStringLiteral("edit"), edit);
    return recipe;
}

void RecordingPreviewBackend::removeUnusedBackgrounds(const QString& projectDir, const QJsonObject& recipe)
{
    const QString used = QFileInfo(recipe.value(QStringLiteral("edit")).toObject()
                                       .value(QStringLiteral("appearance")).toObject()
                                       .value(QStringLiteral("imagePath")).toString())
                             .absoluteFilePath();
    const QDir dir(projectDir);
    for (const QFileInfo& file : dir.entryInfoList({QStringLiteral("background*")}, QDir::Files)) {
        if (file.absoluteFilePath() != used) QFile::remove(file.absoluteFilePath());
    }
}

bool RecordingPreviewBackend::keepAsProject(const QString& savedPath)
{
    if (m_project) {
        QString copied;
        const QJsonObject recipe = recipeForProject(m_project->dir, &copied);
        if (!SnapTray::RecordingProjects::update(*m_project, recipe, savedPath)) {
            // The saved manifest still uses the old image: it stays.
            if (!copied.isEmpty()) QFile::remove(copied);
            return false;
        }
        removeUnusedBackgrounds(m_project->dir, recipe);
        return true;
    }
    // Moved with its sidecars; the caller then removes whatever is left.
    // The thumbnail follows below.
    auto project = SnapTray::RecordingProjects::create(m_videoPath, false, currentRecipe(), QImage(), m_duration, savedPath);
    if (!project) {
        qWarning() << "RecordingPreviewBackend: the recording could not be kept as a project";
        return false;
    }
    // A small picture for the library, from the middle of what is kept: the
    // first frame is usually the desktop before anything happens (#210).
    // Off the GUI thread: frame readers decode from the start, which for the
    // middle of a long recording takes a while (PR #232 review).
    const SnapTray::TimelineMap map = timelineMap();
    const qint64 middle = map.outputDurationMs() > 0 ? map.outputToSource(map.outputDurationMs() / 2) : m_trimStart;
    (void)QtConcurrent::run([video = project->videoPath(), path = project->thumbnailPath(), middle]() {
        const auto reader = IVideoFrameReader::create();
        if (!reader || !reader->load(video)) return;
        const QImage frame = reader->frameAt(middle);
        if (frame.isNull()) return;
        // Whole or not at all: the library may list the project meanwhile.
        QSaveFile file(path);
        if (file.open(QIODevice::WriteOnly)
            && frame.scaled(kProjectThumbnailSize, Qt::KeepAspectRatio, Qt::SmoothTransformation).save(&file, "PNG")) {
            file.commit();
        }
    });
    // Its own copy of a background image, now that the folder exists.
    QString copied;
    const QJsonObject recipe = recipeForProject(project->dir, &copied);
    if (recipe != project->recipe && !SnapTray::RecordingProjects::update(*project, recipe, savedPath)
        && !copied.isEmpty()) {
        QFile::remove(copied); // the project keeps the image where it was
    }
    const int days = RecordingSettingsManager::instance().projectCleanupDays();
    SnapTray::RecordingProjects::removeOlderThan(days);
    return true;
}

bool RecordingPreviewBackend::audioMixAvailable() const
{
    return m_hasSeparateTracks && m_selectedFormat == MP4 && mp4Capabilities().audioMix;
}

void RecordingPreviewBackend::editAudioMix(const std::function<void(SnapTray::AudioMixSettings&)>& change)
{
    if (isProcessing()) return; // a running export keeps what it started with
    SnapTray::AudioMixSettings mix = m_audioMix;
    change(mix);
    if (mix == m_audioMix) return;
    m_audioMix = mix;
    emit audioMixChanged();
    recordEdit();
}

void RecordingPreviewBackend::setMicrophoneGainDb(qreal db)
{
    const double clamped = qBound(SnapTray::AudioMixSettings::kMinGainDb, double(db), SnapTray::AudioMixSettings::kMaxGainDb);
    editAudioMix([clamped](SnapTray::AudioMixSettings& m) { m.microphoneGainDb = clamped; });
}

void RecordingPreviewBackend::setSystemAudioGainDb(qreal db)
{
    const double clamped = qBound(SnapTray::AudioMixSettings::kMinGainDb, double(db), SnapTray::AudioMixSettings::kMaxGainDb);
    editAudioMix([clamped](SnapTray::AudioMixSettings& m) { m.systemAudioGainDb = clamped; });
}

void RecordingPreviewBackend::setMicrophoneMuted(bool value)
{
    editAudioMix([value](SnapTray::AudioMixSettings& m) { m.microphoneMuted = value; });
}

void RecordingPreviewBackend::setSystemAudioMuted(bool value)
{
    editAudioMix([value](SnapTray::AudioMixSettings& m) { m.systemAudioMuted = value; });
}

void RecordingPreviewBackend::setMicrophoneNoiseReduction(bool value)
{
    editAudioMix([value](SnapTray::AudioMixSettings& m) { m.microphoneNoiseReduction = value; });
}

QVariantList RecordingPreviewBackend::overlays() const
{
    QVariantList list;
    for (const auto& s : m_overlays.segments) {
        list.append(QVariantMap{{QStringLiteral("kind"), int(s.kind)},
                                {QStringLiteral("start"), s.startMs},
                                {QStringLiteral("end"), s.endMs},
                                {QStringLiteral("text"), s.text},
                                {QStringLiteral("fontFamily"), s.fontFamily},
                                {QStringLiteral("size"), s.size},
                                {QStringLiteral("color"), s.color},
                                {QStringLiteral("background"), int(s.background)},
                                {QStringLiteral("align"), int(s.align)},
                                {QStringLiteral("opacity"), s.opacity}});
    }
    return list;
}

void RecordingPreviewBackend::setSelectedOverlay(int index)
{
    index = index >= 0 && index < int(m_overlays.segments.size()) ? index : -1;
    if (index >= 0) {
        setSelectedZoom(-1);
        setSelectedMask(-1);
        setSelectedPointerHide(-1);
    }
    if (m_selectedOverlay == index) return;
    m_selectedOverlay = index;
    emit selectedOverlayChanged();
}

void RecordingPreviewBackend::editOverlays(const std::function<void(SnapTray::OverlayTrack&)>& change)
{
    if (isProcessing()) return; // a running export keeps what it started with
    SnapTray::OverlayTrack overlays = m_overlays;
    change(overlays);
    if (overlays == m_overlays) return;
    m_overlays = overlays;
    if (m_selectedOverlay >= int(m_overlays.segments.size())) setSelectedOverlay(-1);
    emit overlaysChanged();
    recordEdit();
}

int RecordingPreviewBackend::addTextOverlay(qint64 positionMs)
{
    constexpr qint64 kNewTextMs = 3000;
    if (isProcessing() || !overlaysAvailable()) return -1;
    SnapTray::OverlaySegment segment;
    segment.kind = SnapTray::OverlaySegment::Kind::Text;
    segment.text = tr("Title");
    segment.startMs = qMax<qint64>(0, positionMs);
    segment.endMs = m_duration > 0 ? qMin(m_duration, segment.startMs + kNewTextMs) : segment.startMs + kNewTextMs;
    if (segment.endMs - segment.startMs < SnapTray::OverlaySegment::kMinRangeMs) {
        segment.startMs = qMax<qint64>(0, segment.endMs - kNewTextMs);
    }
    editOverlays([&segment](SnapTray::OverlayTrack& t) { t.segments.push_back(segment); });
    const int index = int(m_overlays.segments.size()) - 1;
    setSelectedOverlay(index);
    return index;
}

int RecordingPreviewBackend::addImageOverlay(const QUrl& file)
{
    if (isProcessing() || !overlaysAvailable()) return -1;
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    // Read once and kept: the file may go away before the export.
    const QImage image = SnapTray::OverlayTrack::loadImage(path);
    if (image.isNull()) return -1;
    SnapTray::OverlaySegment segment;
    segment.kind = SnapTray::OverlaySegment::Kind::Image;
    segment.image = image;
    segment.size = 0.15;
    segment.position = QPointF(0.8, 0.05);
    segment.fadeMs = 0;
    editOverlays([&segment](SnapTray::OverlayTrack& t) { t.segments.push_back(segment); });
    const int index = int(m_overlays.segments.size()) - 1;
    setSelectedOverlay(index);
    return index;
}

int RecordingPreviewBackend::chooseOverlayImage()
{
    if (isProcessing()) return -1;
    const QString file = QFileDialog::getOpenFileName(nullptr, tr("Select Image"), QString(),
                                                      tr("Images (*.png *.svg *.jpg *.jpeg *.webp)"));
    if (file.isEmpty()) return -1;
    const int index = addImageOverlay(QUrl::fromLocalFile(file));
    if (index < 0) setErrorMessage(tr("The image could not be read."));
    return index;
}

void RecordingPreviewBackend::removeOverlay(int index)
{
    editOverlays([index](SnapTray::OverlayTrack& t) {
        if (index >= 0 && index < int(t.segments.size())) t.segments.erase(t.segments.begin() + index);
    });
    setSelectedOverlay(-1);
}

namespace {
template <typename F>
void editSegment(SnapTray::OverlayTrack& t, int index, F&& change)
{
    if (index >= 0 && index < int(t.segments.size())) change(t.segments[size_t(index)]);
}
} // namespace

void RecordingPreviewBackend::setOverlayText(int index, const QString& text)
{
    editOverlays([&](SnapTray::OverlayTrack& t) { editSegment(t, index, [&](auto& s) { s.text = text; }); });
}

void RecordingPreviewBackend::setOverlayFontFamily(int index, const QString& family)
{
    editOverlays([&](SnapTray::OverlayTrack& t) {
        editSegment(t, index, [&](auto& s) { s.fontFamily = family; });
    });
}

void RecordingPreviewBackend::setOverlaySize(int index, qreal size)
{
    const double clamped = qBound(SnapTray::OverlaySegment::kMinSize, double(size), SnapTray::OverlaySegment::kMaxSize);
    editOverlays([&](SnapTray::OverlayTrack& t) { editSegment(t, index, [&](auto& s) { s.size = clamped; }); });
}

void RecordingPreviewBackend::setOverlayColor(int index, const QColor& color)
{
    if (!color.isValid()) return;
    editOverlays([&](SnapTray::OverlayTrack& t) { editSegment(t, index, [&](auto& s) { s.color = color.toRgb(); }); });
}

void RecordingPreviewBackend::setOverlayBackground(int index, int background)
{
    const int clamped = qBound(0, background, int(SnapTray::OverlaySegment::Background::Light));
    editOverlays([&](SnapTray::OverlayTrack& t) {
        editSegment(t, index, [&](auto& s) { s.background = SnapTray::OverlaySegment::Background(clamped); });
    });
}

void RecordingPreviewBackend::setOverlayAlign(int index, int align)
{
    const int clamped = qBound(0, align, int(SnapTray::OverlaySegment::Align::Right));
    editOverlays([&](SnapTray::OverlayTrack& t) {
        editSegment(t, index, [&](auto& s) { s.align = SnapTray::OverlaySegment::Align(clamped); });
    });
}

void RecordingPreviewBackend::setOverlayOpacity(int index, qreal opacity)
{
    const double clamped = qBound(0.1, double(opacity), 1.0);
    editOverlays([&](SnapTray::OverlayTrack& t) { editSegment(t, index, [&](auto& s) { s.opacity = clamped; }); });
}

bool RecordingPreviewBackend::setOverlayRange(int index, qint64 startMs, qint64 endMs)
{
    if (isProcessing() || index < 0 || index >= int(m_overlays.segments.size())) return false;
    if (startMs < 0 || (endMs >= 0 && endMs - startMs < SnapTray::OverlaySegment::kMinRangeMs)
        || (m_duration > 0 && startMs >= m_duration)) {
        return false;
    }
    editOverlays([&](SnapTray::OverlayTrack& t) {
        editSegment(t, index, [&](auto& s) {
            s.startMs = startMs;
            s.endMs = endMs;
        });
    });
    return true;
}

QRectF RecordingPreviewBackend::overlayRectInView(int index, const QRectF& pictureInView) const
{
    const QSize picture = exportContentSize();
    if (index < 0 || index >= int(m_overlays.segments.size()) || picture.isEmpty() || pictureInView.isEmpty()) return {};
    const QRectF bounds = SnapTray::OverlayTrack::boundsOf(m_overlays.segments[size_t(index)], picture);
    const double sx = pictureInView.width() / picture.width();
    const double sy = pictureInView.height() / picture.height();
    return QRectF(pictureInView.x() + bounds.x() * sx, pictureInView.y() + bounds.y() * sy, bounds.width() * sx,
                  bounds.height() * sy);
}

void RecordingPreviewBackend::setOverlayRectFromView(int index, const QRectF& viewRect, const QRectF& pictureInView)
{
    const QRectF current = overlayRectInView(index, pictureInView);
    if (current.isEmpty() || viewRect.isEmpty()) return;
    // The top-left places it; a new size scales it (text and image alike keep their shape).
    const QPointF position(std::clamp((viewRect.x() - pictureInView.x()) / pictureInView.width(), 0.0, 1.0),
                           std::clamp((viewRect.y() - pictureInView.y()) / pictureInView.height(), 0.0, 1.0));
    const double scale = viewRect.width() / current.width();
    editOverlays([&](SnapTray::OverlayTrack& t) {
        editSegment(t, index, [&](auto& s) {
            s.position = position;
            if (std::abs(scale - 1.0) > 0.01) {
                s.size = std::clamp(s.size * scale, SnapTray::OverlaySegment::kMinSize, SnapTray::OverlaySegment::kMaxSize);
            }
        });
    });
}

void RecordingPreviewBackend::editKeyOverlay(const std::function<void(SnapTray::KeyOverlaySettings&)>& change)
{
    if (isProcessing()) return; // a running export keeps what it started with
    SnapTray::KeyOverlaySettings settings = m_keyOverlay;
    change(settings);
    if (settings == m_keyOverlay) return;
    m_keyOverlay = settings;
    RecordingSettingsManager::instance().setKeyOverlay(settings.toJson());
    emit keyOverlayChanged();
    recordEdit();
}

void RecordingPreviewBackend::setKeyOverlayEnabled(bool value)
{
    editKeyOverlay([value](SnapTray::KeyOverlaySettings& s) { s.enabled = value; });
}

void RecordingPreviewBackend::setKeyOverlayPosition(int value)
{
    const int clamped = qBound(0, value, int(SnapTray::KeyOverlaySettings::Position::TopCenter));
    editKeyOverlay([clamped](SnapTray::KeyOverlaySettings& s) { s.position = SnapTray::KeyOverlaySettings::Position(clamped); });
}

void RecordingPreviewBackend::setKeyOverlaySize(int value)
{
    const int clamped = qBound(0, value, int(SnapTray::KeyOverlaySettings::Size::Large));
    editKeyOverlay([clamped](SnapTray::KeyOverlaySettings& s) { s.size = SnapTray::KeyOverlaySettings::Size(clamped); });
}

void RecordingPreviewBackend::setKeyOverlayStyle(int value)
{
    const int clamped = qBound(0, value, int(SnapTray::KeyOverlaySettings::Style::Light));
    editKeyOverlay([clamped](SnapTray::KeyOverlaySettings& s) { s.style = SnapTray::KeyOverlaySettings::Style(clamped); });
}

void RecordingPreviewBackend::setKeyOverlayDisplayMs(int value)
{
    const auto& choices = SnapTray::KeyOverlaySettings::kDisplayChoices;
    if (std::find(std::begin(choices), std::end(choices), qint64(value)) == std::end(choices)) return;
    editKeyOverlay([value](SnapTray::KeyOverlaySettings& s) { s.displayMs = value; });
}

QVariantList RecordingPreviewBackend::timelineParts() const
{
    QVariantList list;
    if (m_duration <= 0) return list;
    for (int i = 0; i < int(m_timeline.parts.size()); ++i) {
        const auto& part = m_timeline.parts[size_t(i)];
        list.append(QVariantMap{{QStringLiteral("start"), m_timeline.partStartMs(i)},
                                {QStringLiteral("end"), m_timeline.partEndMs(i, m_duration)},
                                {QStringLiteral("deleted"), part.deleted},
                                {QStringLiteral("speed"), part.speed}});
    }
    return list;
}

QVariantList RecordingPreviewBackend::timelineSplits() const
{
    QVariantList list;
    for (const qint64 split : m_timeline.splits) list.append(split);
    return list;
}

void RecordingPreviewBackend::editTimeline(const std::function<bool(SnapTray::TimelineEdit&)>& change, bool record)
{
    if (isProcessing()) return; // a running export keeps what it started with
    SnapTray::TimelineEdit timeline = m_timeline;
    if (!change(timeline) || timeline == m_timeline) return;
    m_timeline = timeline;
    emit timelineChanged();
    if (record) recordEdit();
}

bool RecordingPreviewBackend::splitAt(qint64 sourceMs)
{
    if (m_duration <= 0) return false;
    bool split = false;
    editTimeline([&](SnapTray::TimelineEdit& t) { return split = t.splitAt(sourceMs, m_duration); });
    return split;
}

void RecordingPreviewBackend::removeSplit(int index)
{
    editTimeline([index](SnapTray::TimelineEdit& t) { return t.removeSplit(index); });
}

bool RecordingPreviewBackend::moveSplit(int index, qint64 sourceMs)
{
    if (m_duration <= 0) return false;
    bool moved = false;
    editTimeline([&](SnapTray::TimelineEdit& t) { return moved = t.moveSplit(index, sourceMs, m_duration); }, false);
    return moved;
}

qint64 RecordingPreviewBackend::legalSplitPosition(int index, qint64 sourceMs) const
{
    if (m_duration <= 0 || index < 0 || index >= int(m_timeline.splits.size())) return -1;
    const qint64 lower = m_timeline.partStartMs(index) + SnapTray::TimelineEdit::kMinPartMs;
    const qint64 upper = m_timeline.partEndMs(index + 1, m_duration) - SnapTray::TimelineEdit::kMinPartMs;
    return lower > upper ? -1 : qBound(lower, sourceMs, upper);
}

void RecordingPreviewBackend::commitTimelineEdit()
{
    recordEdit();
}

void RecordingPreviewBackend::amendTimelineEdit()
{
    if (m_editHistory.amend(currentEditState())) {
        emit editHistoryChanged();
    }
}

void RecordingPreviewBackend::setPartDeleted(int part, bool deleted)
{
    const qint64 start = m_trimStart, end = trimEnd();
    editTimeline([part, deleted, start, end](SnapTray::TimelineEdit& t) {
        // Something must be left to export.
        return t.setDeleted(part, deleted) && !t.map(start, end).isEmpty();
    });
}

void RecordingPreviewBackend::setPartSpeed(int part, qreal speed)
{
    editTimeline([part, speed](SnapTray::TimelineEdit& t) { return t.setSpeed(part, speed); });
}

qint64 RecordingPreviewBackend::playbackTarget(qint64 positionMs) const
{
    if (m_timeline.isEmpty()) return positionMs;
    return timelineMap().nextShownSourceMs(positionMs);
}

qreal RecordingPreviewBackend::playbackSpeedAt(qint64 positionMs) const
{
    return m_timeline.isEmpty() ? 1.0 : timelineMap().speedAt(positionMs);
}

qint64 RecordingPreviewBackend::outputTimeAt(qint64 sourceMs) const
{
    const auto out = timelineMap().sourceToOutput(sourceMs);
    return out ? *out : -1;
}

void RecordingPreviewBackend::setSelectedFormat(int format)
{
    if (isProcessing()) return;
    format = qBound(0, format, 2);
    auto fmt = static_cast<OutputFormat>(format);
    if (m_selectedFormat != fmt) {
        m_selectedFormat = fmt;
        emit formatChanged();
    }
}

void RecordingPreviewBackend::setErrorMessage(const QString &msg)
{
    if (m_errorMessage != msg) {
        m_errorMessage = msg;
        emit errorMessageChanged();
    }
}

void RecordingPreviewBackend::clearError()
{
    setErrorMessage(QString());
}

void RecordingPreviewBackend::reportPlaybackError(const QString &message)
{
    if (message.isEmpty()) {
        return;
    }

    setErrorMessage(tr("Failed to load preview video: %1").arg(message));
}

// ---------- QML state updates ----------
void RecordingPreviewBackend::updatePosition(qint64 ms)
{
    if (m_position != ms) {
        m_position = ms;
        emit positionChanged();
    }
}

void RecordingPreviewBackend::updateDuration(qint64 ms)
{
    if (m_duration != ms) {
        m_duration = ms;
        // If trimEnd was -1 (unset), it now means the full duration
        emit durationChanged();
        emit trimRangeChanged();
        applyPendingProjectRecipe();
    }
}

void RecordingPreviewBackend::updatePlayingState(bool playing)
{
    if (m_isPlaying != playing) {
        m_isPlaying = playing;
        emit stateChanged();
    }
}

// ---------- Actions ----------

QString RecordingPreviewBackend::formatTime(qint64 ms) const
{
    int totalSeconds = static_cast<int>(ms / 1000);
    int minutes = totalSeconds / 60;
    int seconds = totalSeconds % 60;
    return QString("%1:%2")
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

void RecordingPreviewBackend::save()
{
    qDebug() << "RecordingPreviewBackend: Save requested";
    if (isProcessing() || m_closeHandled) {
        qDebug() << "RecordingPreviewBackend: Save ignored while processing";
        return;
    }
    // An opened project's edits (masks, crops, cuts) are applied once the
    // player has reported the picture: saving before would export without
    // them (PR #203 review).
    // Nor before the area chosen before recording is the crop: the export
    // would show the whole screen, outside the area (PR #203 review).
    if (projectLoading()) {
        qDebug() << "RecordingPreviewBackend: Save ignored until the project's edits are applied";
        return;
    }
    // Saving answers a pending "discard?" question with "keep".
    cancelDiscard();
    m_canRetrySave = false;
    clearError();

    // Output quality contract: snapshot the user's setting when Save begins;
    // every MP4 re-encode below uses it. Quality 80 is only the legacy default.
    m_outputQuality = outputQualityOverride() ? outputQualityOverride()()
                                              : RecordingSettingsManager::instance().quality();

    // An overlay the user added is part of the export: never leave it out quietly.
    if (!m_overlays.isEmpty() && !overlaysAvailable()) {
        setErrorMessage(tr("Text and image overlays cannot be drawn into MP4 on this system yet. Export as GIF "
                           "or WebP, or remove them to export MP4. Nothing was exported."));
        return;
    }

    // Masks are only kept off the export by drawing them; an export that
    // cannot draw them must not happen at all.
    if (!m_redaction.isEmpty() && !masksAvailable()) {
        setErrorMessage(tr("Masks cannot be applied to MP4 on this system yet. Export as GIF or WebP, "
                           "or remove the masks to export MP4. Nothing was exported."));
        return;
    }

    // Cuts are only kept out of the export by joining the kept parts. (Cutting
    // only the first or last part is a trim, which every format can do.)
    if (hasTimelineEdits() && timelineMap().isEmpty()) {
        setErrorMessage(tr("Every part is cut out: there is nothing to export."));
        return;
    }
    if (needsSegments() && !timelineEditsAvailable()) {
        setErrorMessage(tr("Cuts and speed changes cannot be applied to MP4 on this system yet. Export as GIF "
                           "or WebP, or remove them to export MP4. Nothing was exported."));
        return;
    }

    // An export writes a second file next to the recording. Check the room
    // for it first (an unedited MP4 can be moved as is, so it needs none).
    // MP4 options that may change the stream need room for an export too.
    // Whether they really do (a smaller size, a lower rate, another codec
    // than the source's) is decided by the export job, which probes the
    // source off the GUI thread.
    // Once the source is probed (it starts when the preview opens), only
    // options that really change this stream count: a 1080p choice for a
    // 720p recording is still a move (PR #197 review).
    const bool mp4OptionsMayApply = m_selectedFormat == MP4
        && (sourceProbeReady() ? mp4OptionsActive() : effectiveMp4Options().mayChangeStream());
    const bool writesNewFile = m_selectedFormat != MP4 || hasTrim() || hasCrop() || effectsActive()
        || hasTimelineEdits() || mp4OptionsMayApply || audioMixActive();
    // Only MP4 options, with the source not probed yet: whether they change
    // anything is not known here. The export job decides after its probe,
    // moving the recording if they do not and checking the room if they do,
    // so a save that only needs a move is never refused (PR #197 review).
    m_exportSpaceCheckDeferred = mp4OptionsMayApply && !sourceProbeReady() && m_selectedFormat == MP4
        && !hasTrim() && !hasCrop() && !effectsActive() && !hasTimelineEdits() && !audioMixActive();
    if (writesNewFile && !m_exportSpaceCheckDeferred) {
        const QString directory = QFileInfo(m_videoPath).absolutePath();
        const qint64 freeBytes = freeBytesForPathOverride() ? freeBytesForPathOverride()(directory)
                                                            : QStorageInfo(directory).bytesAvailable();
        const qint64 sourceBytes = qMax<qint64>(0, QFileInfo(m_videoPath).size());
        // Only the kept part is exported: a short clip from a long recording
        // needs room for that clip, not the whole recording (#171 review).
        // A crop is not counted down; the estimate stays on the safe side.
        const qint64 keptBytes = m_duration > 0 && hasTrim()
            ? qint64(std::ceil(double(sourceBytes) * double(trimmedDuration()) / double(m_duration)))
            : sourceBytes;
        // MP4 edits are at most about the source's size; GIF and WebP can be
        // larger, so allow twice as much. Unknown free space does not block.
        const qint64 needed = SnapTray::DiskSpaceGuard::kFloorBytes
            + (m_selectedFormat == MP4 ? keptBytes : 2 * keptBytes);
        if (freeBytes >= 0 && freeBytes < needed) {
            qWarning() << "RecordingPreviewBackend: not enough space to export:" << freeBytes << "<" << needed;
            m_canRetrySave = true;
            setErrorMessage(tr("Not enough free disk space to export (%1 free, about %2 needed). "
                               "Free up space and try again; your recording is kept.")
                                .arg(QLocale().formattedDataSize(freeBytes),
                                     QLocale().formattedDataSize(needed)));
            return;
        }
    }

    // Animated exports share the same offline extraction path, including trims.
    if (m_selectedFormat != MP4) {
        performFormatConversion(m_selectedFormat);
        return;
    }

    // Drawing the pointer or a zoom is an edit: the unedited file cannot just be moved.
    // So is a new mix of the separate audio tracks (#189).
    if (hasTrim() || hasCrop() || effectsActive() || hasTimelineEdits() || audioMixActive()) {
        performTranscode(false);
        return;
    }

    // MP4 with no edits: move the intermediate if it already meets the
    // user's quality, otherwise re-encode it to that quality. MP4 options
    // that change the stream make it an export (decided after the probe).
    performTranscode(true);
}

bool RecordingPreviewBackend::canCancelExport() const
{
    // Both export kinds check the token between frames. Once the file is
    // handed over for saving, the save dialog decides instead.
    return m_isProcessing && !m_awaitingSave
        && (m_exportKind == ExportKind::MP4 || m_exportKind == ExportKind::Animated)
        && !m_exportCancelToken->load();
}

void RecordingPreviewBackend::finishProcessing()
{
    m_exportKind = ExportKind::None;
    m_isProcessing = false;
    m_processIndeterminate = false;
    if (m_exportCloseQuestionPending) {
        m_exportCloseQuestionPending = false;
        emit exportCloseQuestionChanged();
    }
    emit processingChanged();
    emit processProgressChanged();
}

void RecordingPreviewBackend::setProcessPhase(const QString &status, bool indeterminate)
{
    m_processStatus = status;
    m_processIndeterminate = indeterminate;
    emit processStatusChanged();
    emit processProgressChanged();
}

void RecordingPreviewBackend::handOffForSave(const QString &outputPath, const QSize &croppedSize)
{
    // A kept project's source is never handed over (the save dialog moves
    // what it gets and drops its sidecars): an unedited save gets a copy.
    // Named like a recording, so a copy left by a crash is cleaned up.
    if (keepsProject() && outputPath == m_videoPath) {
        const QFileInfo source(m_videoPath);
        const QString copy = QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation))
                                 .filePath(QStringLiteral("SnapTray_Recording_%1_project.%2")
                                               .arg(QDateTime::currentMSecsSinceEpoch())
                                               .arg(source.suffix()));
        if (!QFile::copy(m_videoPath, copy)) {
            finishProcessing();
            m_canRetrySave = true;
            setErrorMessage(tr("The project's recording could not be copied for saving. Nothing was saved."));
            return;
        }
        handOffForSave(copy, croppedSize);
        return;
    }
    // The export is done; the save dialog (or auto-save) now owns the outcome.
    // Stay "processing" so the window cannot close or start another export.
    m_awaitingSave = true;
    m_pendingSaveOutput = outputPath;
    m_isProcessing = true;
    m_exportKind = ExportKind::None;
    setProcessPhase(tr("Saving..."), true);
    emit processingChanged();
    emit saveRequested(outputPath, croppedSize);
}

void RecordingPreviewBackend::completeSave(SaveOutcome outcome, const QString &savedPath)
{
    if (!m_awaitingSave) {
        return;
    }
    m_awaitingSave = false;
    const QString output = m_pendingSaveOutput;
    m_pendingSaveOutput.clear();
    const bool derived = output != m_videoPath;

    if (outcome == SaveOutcome::Saved && m_project) {
        // A project keeps its source; the save updates its recipe. The save
        // dialog took the export (or the copy) as with any save.
        if (!keepAsProject(savedPath)) {
            // The file is saved, but the project still has the old edits:
            // stay open so they are not lost (PR #203 review).
            finishProcessing();
            m_canRetrySave = true;
            setErrorMessage(tr("Saved to %1, but the project could not be updated. Save again to keep these "
                               "edits in the project.").arg(QDir::toNativeSeparators(savedPath)));
            return;
        }
        finishProcessing();
        m_saved = true;
        close();
        return;
    }
    if (outcome == SaveOutcome::Saved) {
        // Only now is the source no longer needed, unless the export was
        // saved over it: then that path is the user's saved file.
        const auto samePath = [](const QString& a, const QString& b) {
            const QFileInfo fa(a), fb(b);
            const QString ca = fa.canonicalFilePath(), cb = fb.canonicalFilePath();
            return !ca.isEmpty() ? ca == cb : fa.absoluteFilePath() == fb.absoluteFilePath();
        };
        const bool savedOverSource = !savedPath.isEmpty() && samePath(savedPath, m_videoPath);
        // Kept as a project (#191): the source and its sidecars move into it
        // (an unedited save was handed a copy, so the source is still here).
        // When that fails nothing is deleted: the recording stays open
        // here, so it is not lost with the project (PR #203 review).
        if (keepsProject() && !savedOverSource && !keepAsProject(savedPath)) {
            finishProcessing();
            m_canRetrySave = true;
            setErrorMessage(tr("Saved to %1, but the recording could not be kept as an editable project. "
                               "It is still open here: save again, or close it to discard it.")
                                .arg(QDir::toNativeSeparators(savedPath)));
            return;
        }
        if (derived && !savedOverSource) {
            QFile::remove(m_videoPath);
        }
        SnapTray::WindowTimelineSidecar::remove(m_videoPath);
        SnapTray::CursorTimelineSidecar::remove(m_videoPath);
        SnapTray::KeyTimelineSidecar::remove(m_videoPath);
        SnapTray::AudioTracks::remove(m_videoPath);
        SnapTray::RecordingSession::remove(m_videoPath);
        finishProcessing();
        m_saved = true;
        close();
        return;
    }

    // Not saved: drop what this export produced, keep the recording, its
    // window timeline and the crop/trim so the user can save again.
    if (derived) {
        QFile::remove(output);
    }
    finishProcessing();
    if (outcome == SaveOutcome::Failed) {
        m_canRetrySave = true;
        setErrorMessage(tr("The file could not be saved. Your recording is kept; "
                           "try Save again or choose another location."));
    }
}

void RecordingPreviewBackend::keepExporting()
{
    if (!m_exportCloseQuestionPending) return;
    m_exportCloseQuestionPending = false;
    emit exportCloseQuestionChanged();
}

void RecordingPreviewBackend::cancelExportFromClose()
{
    keepExporting();
    cancelExport();
}

void RecordingPreviewBackend::cancelExport()
{
    if (!canCancelExport()) {
        return;
    }
    qDebug() << "RecordingPreviewBackend: Export cancel requested";
    m_exportCancelToken->store(true);
    m_processStatus = tr("Cancelling...");
    emit processStatusChanged();
    emit processingChanged();
}

void RecordingPreviewBackend::discard()
{
    qDebug() << "RecordingPreviewBackend: Discard requested";
    if (isProcessing() || m_closeHandled) {
        qDebug() << "RecordingPreviewBackend: Discard ignored while processing";
        return;
    }
    m_saved = false;
    if (m_discardConfirmationPending) {
        m_discardConfirmationPending = false;
        emit discardConfirmationChanged();
    }
    close(); // All close paths share the same discard/teardown notification.
}

void RecordingPreviewBackend::requestDiscard()
{
    if (isProcessing() || m_closeHandled || m_discardConfirmationPending) {
        return;
    }
    // A reopened project closed as it was opened loses nothing: the library
    // keeps it, so there is no "Close without saving your changes?" to ask.
    if (isProject() && !hasUnsavedProjectChanges()) {
        discard();
        return;
    }
    m_discardConfirmationPending = true;
    emit discardConfirmationChanged();
    if (m_view) {
        // A title-bar close can come while another app is active.
        m_view->raise();
        m_view->requestActivate();
    }
}

void RecordingPreviewBackend::cancelDiscard()
{
    if (!m_discardConfirmationPending) {
        return;
    }
    m_discardConfirmationPending = false;
    emit discardConfirmationChanged();
}

void RecordingPreviewBackend::toggleTrim()
{
    constexpr qint64 kMinTrimSpanMs = 100;

    if (m_duration <= kMinTrimSpanMs) {
        return;
    }

    if (hasTrim()) {
        bool changed = false;
        if (m_trimStart != 0) {
            m_trimStart = 0;
            changed = true;
        }
        if (m_trimEnd != -1) {
            m_trimEnd = -1;
            changed = true;
        }
        if (changed) {
            emit trimRangeChanged();
            recordEdit();
        }
        return;
    }

    // Establish an initial editable trim region so handles become available.
    qint64 marginMs = qMin<qint64>(1000, m_duration / 10);
    qint64 start = marginMs;
    qint64 end = m_duration - marginMs;

    if (end - start < kMinTrimSpanMs) {
        start = 0;
        end = m_duration - 1;
    }

    if (end <= start || end >= m_duration) {
        return;
    }

    m_trimStart = start;
    m_trimEnd = end;
    emit trimRangeChanged();
    recordEdit();
}

// ---------- Crop ----------

void RecordingPreviewBackend::updateVideoSize(const QSize &size)
{
    if (m_videoSize == size) {
        return;
    }
    m_videoSize = size;
    emit videoSizeChanged();
    applyPendingProjectRecipe();
    applyPendingInitialCrop();
    if (m_windowTimeline && m_windowTimeline->frameSize() != size) {
        qWarning() << "RecordingPreviewBackend: window timeline frame size" << m_windowTimeline->frameSize()
                   << "does not match the video" << size << "- snapping disabled";
        m_windowTimeline.reset();
        emit windowTimelineChanged();
    }
    if (m_cursorTimeline && m_cursorTimeline->info.frameSize != size) {
        qWarning() << "RecordingPreviewBackend: pointer data frame size" << m_cursorTimeline->info.frameSize
                   << "does not match the video" << size << "- pointer effects disabled";
        m_cursorTimeline.reset();
        emit cursorEffectsChanged();
    }
    setCropRect(m_cropRect);
}

void RecordingPreviewBackend::setCropRect(const QRect &videoRect)
{
    const QRect normalized = SnapTray::VideoCropGeometry::normalizeCropRect(videoRect, m_videoSize);
    if (normalized == m_cropRect) {
        return;
    }
    m_cropRect = normalized;
    emit cropRectChanged();
}

void RecordingPreviewBackend::setCropFromView(const QRectF &viewRect, const QRectF &contentRect)
{
    setCropRect(legalCropForView(viewRect, contentRect));
    recordEdit();
}

namespace {

// Aspect presets, index-aligned with cropAspectIndex. {-1, -1} = the video's own aspect.
constexpr QSize kAspectPresets[] = {
    {0, 0}, {-1, -1}, {16, 9}, {4, 3}, {1, 1}, {9, 16},
};
constexpr int kAspectPresetCount = int(sizeof(kAspectPresets) / sizeof(kAspectPresets[0]));
constexpr qint64 kMsPerSecond = 1000;
constexpr qint64 kSecondsPerMinute = 60;
constexpr qint64 kMinTrimSpanMs = 100;

} // namespace

QSize RecordingPreviewBackend::aspectUnits() const
{
    const QSize preset = kAspectPresets[m_cropAspectIndex];
    if (preset.width() < 0) {
        return m_videoSize.isEmpty() ? QSize() : m_videoSize;
    }
    return preset;
}

void RecordingPreviewBackend::setCropAspectIndex(int index)
{
    index = qBound(0, index, kAspectPresetCount - 1);
    if (m_cropAspectIndex == index) {
        return;
    }
    m_cropAspectIndex = index;
    emit cropAspectChanged();
}

qreal RecordingPreviewBackend::cropAspectRatio() const
{
    const QSize units = aspectUnits();
    return units.isEmpty() ? 0.0 : qreal(units.width()) / units.height();
}

QRect RecordingPreviewBackend::legalCropForView(const QRectF &viewRect, const QRectF &contentRect) const
{
    const QRect video = SnapTray::VideoCropGeometry::viewToVideo(viewRect, contentRect, m_videoSize);
    const QSize units = aspectUnits();
    const QRect legal = SnapTray::VideoCropGeometry::fitAspect(video, m_videoSize, units.width(), units.height());
    return legal.isEmpty() && !m_videoSize.isEmpty() && !video.isEmpty()
        ? QRect(QPoint(0, 0), m_videoSize) : legal;
}

QRectF RecordingPreviewBackend::legalViewRectForVideo(int x, int y, int width, int height,
                                                      const QRectF &contentRect) const
{
    const QSize units = aspectUnits();
    QRect legal = SnapTray::VideoCropGeometry::fitAspect(QRect(x, y, width, height), m_videoSize,
                                                         units.width(), units.height());
    if (legal.isEmpty()) {
        legal = QRect(QPoint(0, 0), m_videoSize);
    }
    return SnapTray::VideoCropGeometry::videoToView(legal, contentRect, m_videoSize);
}

QRectF RecordingPreviewBackend::fitViewRectToAspect(const QRectF &viewRect, const QRectF &contentRect) const
{
    const QRectF source = viewRect.isEmpty() ? contentRect : viewRect;
    return SnapTray::VideoCropGeometry::videoToView(legalCropForView(source, contentRect),
                                                    contentRect, m_videoSize);
}

QString RecordingPreviewBackend::formatPreciseTime(qint64 ms) const
{
    ms = qMax<qint64>(0, ms);
    const qint64 minutes = ms / (kSecondsPerMinute * kMsPerSecond);
    const qint64 seconds = (ms / kMsPerSecond) % kSecondsPerMinute;
    const qint64 millis = ms % kMsPerSecond;
    return QStringLiteral("%1:%2.%3")
        .arg(minutes)
        .arg(seconds, 2, 10, QLatin1Char('0'))
        .arg(millis, 3, 10, QLatin1Char('0'));
}

qint64 RecordingPreviewBackend::parseTime(const QString &text) const
{
    const QStringList fields = text.trimmed().split(QLatin1Char(':'));
    if (fields.isEmpty() || fields.size() > 3) {
        return -1;
    }
    // Last field: seconds with optional fraction; the others: whole units.
    bool ok = false;
    const QString secondsText = fields.last();
    const double seconds = secondsText.toDouble(&ok);
    if (!ok || seconds < 0 || secondsText.contains(QLatin1Char('e'), Qt::CaseInsensitive)
        || (fields.size() > 1 && seconds >= kSecondsPerMinute)) {
        return -1;
    }
    qint64 total = qRound64(seconds * kMsPerSecond);
    qint64 unitMs = kSecondsPerMinute * kMsPerSecond;
    for (int i = fields.size() - 2; i >= 0; --i) {
        const int value = fields.at(i).toInt(&ok);
        if (!ok || value < 0 || (i > 0 && value >= kSecondsPerMinute)) {
            return -1;
        }
        total += value * unitMs;
        unitMs *= kSecondsPerMinute;
    }
    return total;
}

bool RecordingPreviewBackend::setTrimFromInput(qint64 startMs, qint64 endMs)
{
    if (m_duration <= 0 || startMs < 0 || endMs > m_duration || endMs - startMs < kMinTrimSpanMs) {
        return false;
    }
    m_trimStart = startMs;
    m_trimEnd = endMs >= m_duration ? -1 : endMs;
    emit trimRangeChanged();
    recordEdit();
    return true;
}

void RecordingPreviewBackend::commitTrimEdit()
{
    recordEdit();
}

void RecordingPreviewBackend::cancelTrimEdit()
{
    const SnapTray::PreviewEditState& recorded = m_editHistory.current();
    if (m_trimStart == recorded.trimStart && m_trimEnd == recorded.trimEnd) return;
    m_trimStart = recorded.trimStart;
    m_trimEnd = recorded.trimEnd;
    emit trimRangeChanged();
}

SnapTray::PreviewEditState RecordingPreviewBackend::currentEditState() const
{
    SnapTray::PreviewEditState state;
    state.crop = m_cropRect;
    state.cursor = m_cursorEffects;
    state.zoom = m_zoom;
    state.redaction = m_redaction;
    state.appearance = m_appearance;
    state.timeline = m_timeline;
    state.keys = m_keyOverlay;
    state.overlays = m_overlays;
    state.audio = m_audioMix;
    state.trimStart = m_trimStart;
    // A trim end at the duration is the same as no trim end.
    state.trimEnd = (m_trimEnd >= 0 && m_trimEnd < m_duration) ? m_trimEnd : -1;
    if (state.trimStart == 0 && state.trimEnd < 0) {
        state.trimEnd = -1;
    }
    return state;
}

void RecordingPreviewBackend::applyEditState(const SnapTray::PreviewEditState &state)
{
    if (m_cropRect != state.crop) {
        m_cropRect = SnapTray::VideoCropGeometry::normalizeCropRect(state.crop, m_videoSize);
        emit cropRectChanged();
    }
    if (m_trimStart != state.trimStart || m_trimEnd != state.trimEnd) {
        m_trimStart = state.trimStart;
        m_trimEnd = state.trimEnd;
        emit trimRangeChanged();
    }
    if (state.redaction != m_redaction) {
        m_redaction = state.redaction;
        if (m_selectedMask >= int(m_redaction.masks.size())) setSelectedMask(-1);
        emit masksChanged();
    }
    if (state.zoom != m_zoom) {
        m_zoom = state.zoom;
        if (m_selectedZoom >= int(m_zoom.segments().size())) setSelectedZoom(-1);
        emit zoomChanged();
    }
    if (state.audio != m_audioMix) {
        m_audioMix = state.audio;
        emit audioMixChanged();
    }
    if (state.overlays != m_overlays) {
        m_overlays = state.overlays;
        if (m_selectedOverlay >= int(m_overlays.segments.size())) setSelectedOverlay(-1);
        emit overlaysChanged();
    }
    if (state.keys != m_keyOverlay) {
        m_keyOverlay = state.keys;
        if (!m_restoringProject) RecordingSettingsManager::instance().setKeyOverlay(m_keyOverlay.toJson());
        emit keyOverlayChanged();
    }
    if (state.timeline != m_timeline) {
        m_timeline = state.timeline;
        emit timelineChanged();
    }
    // Restoring a project's recipe is not a choice for the next recording:
    // its looks are not remembered (PR #203 review).
    if (state.appearance != m_appearance) {
        setAppearance(state.appearance);
        if (!m_restoringProject) rememberAppearance(state.appearance);
    }
    if (state.cursor != m_cursorEffects) {
        setCursorEffects(state.cursor);
        if (!m_restoringProject) RecordingSettingsManager::instance().setPointerEffects(state.cursor.preferences().toJson());
    }
    emit editHistoryChanged();
}

void RecordingPreviewBackend::recordEdit()
{
    if (m_editHistory.record(currentEditState())) {
        emit editHistoryChanged();
    }
}

void RecordingPreviewBackend::undo()
{
    if (isProcessing() || !m_editHistory.canUndo()) {
        return;
    }
    applyEditState(m_editHistory.undo());
}

void RecordingPreviewBackend::redo()
{
    if (isProcessing() || !m_editHistory.canRedo()) {
        return;
    }
    applyEditState(m_editHistory.redo());
}

void RecordingPreviewBackend::resetEdits()
{
    if (isProcessing() || !hasEdits()) {
        return;
    }
    // Crop and trim only; the pointer look stays as chosen.
    SnapTray::PreviewEditState unedited;
    unedited.cursor = m_cursorEffects;
    unedited.zoom = m_zoom;
    unedited.redaction = m_redaction;
    unedited.appearance = m_appearance;
    unedited.keys = m_keyOverlay;
    unedited.overlays = m_overlays;
    unedited.audio = m_audioMix;
    applyEditState(unedited);
    recordEdit();
}

QRectF RecordingPreviewBackend::cropRectInView(const QRectF &contentRect) const
{
    if (!hasCrop()) {
        return contentRect;
    }
    return SnapTray::VideoCropGeometry::videoToView(m_cropRect, contentRect, m_videoSize);
}

std::optional<SnapTray::WindowSample> RecordingPreviewBackend::windowAt(const QPointF &viewPoint,
                                                                         const QRectF &contentRect,
                                                                         qint64 positionMs) const
{
    if (!m_windowTimeline || m_videoSize.isEmpty()) {
        return std::nullopt;
    }
    const QPoint videoPoint = SnapTray::VideoCropGeometry::viewPointToVideo(viewPoint, contentRect, m_videoSize);
    if (videoPoint.x() < 0) {
        return std::nullopt;
    }
    return m_windowTimeline->hitTest(videoPoint, positionMs);
}

QRectF RecordingPreviewBackend::windowRectInViewAt(const QPointF &viewPoint, const QRectF &contentRect,
                                                   qint64 positionMs) const
{
    const auto window = windowAt(viewPoint, contentRect, positionMs);
    return window ? SnapTray::VideoCropGeometry::videoToView(window->rect, contentRect, m_videoSize) : QRectF();
}

QString RecordingPreviewBackend::windowAppAt(const QPointF &viewPoint, const QRectF &contentRect,
                                             qint64 positionMs) const
{
    const auto window = windowAt(viewPoint, contentRect, positionMs);
    return window ? window->ownerApp : QString();
}

void RecordingPreviewBackend::clearCrop()
{
    if (m_cropRect.isNull()) {
        return;
    }
    m_cropRect = QRect();
    emit cropRectChanged();
    recordEdit();
}

// ---------- Format conversion (runs on background thread) ----------

void RecordingPreviewBackend::performFormatConversion(OutputFormat format)
{
    QString extension;
    switch (format) {
    case GIF:  extension = ".gif";  break;
    case WebP: extension = ".webp"; break;
    default: return;
    }

    // Create output path
    QString outputPath = m_videoPath;
    int dotIndex = outputPath.lastIndexOf('.');
    if (dotIndex > 0)
        outputPath = outputPath.left(dotIndex) + extension;
    else
        outputPath += extension;

    // Never overwrite an earlier conversion. A unique final path also makes
    // it impossible to mistake a stale file for this conversion's output.
    if (QFileInfo::exists(outputPath)) {
        const int outputDot = outputPath.lastIndexOf('.');
        const QString suffix = QStringLiteral("_converted_")
            + QString::number(QDateTime::currentMSecsSinceEpoch());
        outputPath = outputDot > 0
            ? outputPath.left(outputDot) + suffix + extension
            : outputPath + suffix + extension;
    }

    qDebug() << "RecordingPreviewBackend: Converting to" << extension;

    // A new job and token before notifying QML: stale callbacks of an earlier
    // (cancelled) job compare tokens and never touch this job's UI.
    m_exportCancelToken = std::make_shared<std::atomic_bool>(false);
    const auto cancelToken = m_exportCancelToken;
    m_exportKind = ExportKind::Animated;
    m_isProcessing = true;
    m_processProgress = 0;
    m_processIndeterminate = false;
    m_processStatus = format == GIF ? tr("Converting to GIF...") : tr("Converting to WebP...");
    emit processingChanged();
    emit processProgressChanged();
    emit processStatusChanged();
    const QString writingStatus = tr("Writing file...");

    // Capture values needed by the worker thread
    const QString videoPath = m_videoPath;
    const qint64 requestedStartMs = m_trimStart;
    const qint64 requestedEndMs = m_trimEnd;
    const QRect cropRect = m_cropRect;
    const auto cursorFilter = effectsActive() ? effectsFrameFilter()
                                              : std::function<void(QImage&, qint64)>();
    // The filter frames the picture (#184): frames come out at this size.
    const QSize framedSize = cursorFilter ? framedExportSize() : QSize();
    // Cuts and speed changes (#185): frames are taken at output times.
    const SnapTray::TimelineMap timeline = hasTimelineEdits() ? timelineMap() : SnapTray::TimelineMap();
    const int selectedFormat = static_cast<int>(format);
    const QString createPlayerError = tr("Failed to create video player for conversion");
    const QString loadVideoError = tr("Failed to load video for conversion");
    const QString invalidVideoError = tr("Video not loaded properly for conversion");
    const QString startEncoderError = tr("Failed to start encoder");
    const QString encodingFailedError = tr("Conversion failed: no video frames were encoded");
    const QString frameExtractionError = tr("Conversion failed while extracting video frames");
    const QString finalizeOutputError = tr("Conversion failed while saving the output file");
    const QString outputMissingError = tr("Conversion failed: output file is missing or empty");
    QPointer<RecordingPreviewBackend> weakThis(this);

    (void)QtConcurrent::run([weakThis,
                             cancelToken,
                             writingStatus,
                             videoPath,
                             requestedStartMs,
                             requestedEndMs,
                             cropRect,
                             cursorFilter,
                             framedSize,
                             timeline,
                             outputPath,
                             selectedFormat,
                             createPlayerError,
                             loadVideoError,
                             invalidVideoError,
                             startEncoderError,
                             encodingFailedError,
                             frameExtractionError,
                             finalizeOutputError,
                             outputMissingError]() {
#ifdef Q_OS_WIN
        // Initialize COM for Media Foundation on this worker thread.
        // RAII ensures CoUninitialize is called on every return path.
        struct ComGuard {
            HRESULT hr;
            ComGuard() : hr(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
            ~ComGuard() { if (SUCCEEDED(hr)) CoUninitialize(); }
        } comGuard;
#endif

        // True while this job is still the preview's current job.
        const auto isCurrent = [weakThis, cancelToken]() {
            return weakThis && weakThis->m_exportCancelToken == cancelToken;
        };
        auto postFailure = [weakThis, cancelToken, isCurrent](const QString& errorMessage) {
            QCoreApplication* app = QCoreApplication::instance();
            if (!app) {
                return;
            }

            QMetaObject::invokeMethod(app, [weakThis, cancelToken, isCurrent, errorMessage]() {
                if (!isCurrent()) {
                    return;
                }
                weakThis->finishProcessing();
                // A cancelled job ends quietly; the recording and edits are kept.
                if (!cancelToken->load()) {
                    weakThis->m_canRetrySave = true;
                    weakThis->setErrorMessage(errorMessage);
                }
            }, Qt::QueuedConnection);
        };
        // Cooperative cancellation: checked between frames and before each
        // file step. The encoder is aborted, never killed with its thread.
        auto stopIfCancelled = [cancelToken, postFailure](NativeGifEncoder* gif, WebPAnimationEncoder* webp,
                                                          const QString& workingPath) {
            if (!cancelToken->load()) return false;
            if (gif) gif->abort();
            if (webp) webp->abort();
            QFile::remove(workingPath);
            postFailure(QString());
            return true;
        };

        auto frameReader = IVideoFrameReader::create();
        std::unique_ptr<IVideoPlayer> player;
        if (frameReader) {
            if (!frameReader->load(videoPath)) {
                qWarning() << "RecordingPreviewBackend: Failed to open video reader:"
                           << frameReader->lastError();
                postFailure(loadVideoError);
                return;
            }
        } else {
            player.reset(IVideoPlayer::create(nullptr));
            if (!player) {
                qWarning() << "RecordingPreviewBackend: Failed to create video player for conversion";
                postFailure(createPlayerError);
                return;
            }

            bool mediaLoaded = false;
            {
                QEventLoop loop;
                QTimer timer;
                timer.setSingleShot(true);
                auto mediaLoadedConn = connect(player.get(), &IVideoPlayer::mediaLoaded, &loop, [&]() {
                    mediaLoaded = true;
                    loop.quit();
                });
                connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

                if (!player->load(videoPath)) {
                    disconnect(mediaLoadedConn);
                    qWarning() << "RecordingPreviewBackend: Failed to load video for conversion";
                    postFailure(loadVideoError);
                    return;
                }

                if (!mediaLoaded) {
                    timer.start(kVideoLoadTimeoutMs);
                    loop.exec();
                    timer.stop();
                }
                disconnect(mediaLoadedConn);
            }
        }

        const QSize vidSize = frameReader ? frameReader->videoSize() : player->videoSize();
        int frameRateInt = static_cast<int>(frameReader ? frameReader->frameRate() : player->frameRate());
        if (frameRateInt <= 0) frameRateInt = 30;
        const qint64 dur = frameReader ? frameReader->duration() : player->duration();
        const qint64 startMs = qMax<qint64>(0, requestedStartMs);
        const qint64 endMs = requestedEndMs < 0 ? dur : qMin(requestedEndMs, dur);
        const QSize outputSize = !framedSize.isEmpty() ? framedSize : cropRect.isEmpty() ? vidSize : cropRect.size();

        if (dur <= 0 || vidSize.isEmpty() || startMs >= endMs) {
            qWarning() << "RecordingPreviewBackend: Video not loaded properly for conversion";
            postFailure(invalidVideoError);
            return;
        }

        // Create encoder
        const QString workingOutputPath = outputPath + QStringLiteral(".part-")
            + QUuid::createUuid().toString(QUuid::WithoutBraces);
        bool encoderStarted = false;
        std::unique_ptr<NativeGifEncoder> gifEncoder;
        std::unique_ptr<WebPAnimationEncoder> webpEncoder;

        if (selectedFormat == GIF) {
            gifEncoder = std::make_unique<NativeGifEncoder>(nullptr);
            gifEncoder->setMaxBitDepth(16);
            encoderStarted = gifEncoder->start(workingOutputPath, outputSize, frameRateInt);
        } else {
            webpEncoder = std::make_unique<WebPAnimationEncoder>(nullptr);
            webpEncoder->setQuality(80);
            webpEncoder->setLooping(true);
            encoderStarted = webpEncoder->start(workingOutputPath, outputSize, frameRateInt);
        }

        if (!encoderStarted) {
            qWarning() << "RecordingPreviewBackend: Failed to start encoder";
            postFailure(startEncoderError);
            return;
        }

        bool encoderFinishedSuccessfully = false;
        QString encoderFinishedPath;
        if (gifEncoder) {
            connect(gifEncoder.get(), &NativeGifEncoder::finished,
                    [&encoderFinishedSuccessfully, &encoderFinishedPath](bool success,
                                                                          const QString &path) {
                encoderFinishedSuccessfully = success;
                encoderFinishedPath = path;
            });
        } else if (webpEncoder) {
            connect(webpEncoder.get(), &WebPAnimationEncoder::finished,
                    [&encoderFinishedSuccessfully, &encoderFinishedPath](bool success,
                                                                          const QString &path) {
                encoderFinishedSuccessfully = success;
                encoderFinishedPath = path;
            });
        }

        // Extract and encode frames
        if (player) {
            player->pause();
        }
        bool frameExtractionFailed = false;

        const qint64 outputDurationMs = timeline.isEmpty() ? endMs - startMs : timeline.outputDurationMs();
        for (qint64 frameIndex = 0; ; ++frameIndex) {
            // The frame shown at this output time, from the source time the
            // edit maps it to (a plain trim: start + output time).
            const qint64 outputMs = frameIndex * 1000 / frameRateInt;
            if (outputMs >= outputDurationMs) {
                break;
            }
            const qint64 timeMs = timeline.isEmpty() ? startMs + outputMs : timeline.outputToSource(outputMs);
            if (timeMs >= endMs) {
                break;
            }
            if (stopIfCancelled(gifEncoder.get(), webpEncoder.get(), workingOutputPath)) {
                return;
            }
            QImage capturedFrame;
            if (frameReader) {
                capturedFrame = frameReader->frameAt(timeMs);
                if (capturedFrame.isNull()) {
                    qWarning() << "RecordingPreviewBackend: Offline frame extraction failed:"
                               << frameReader->lastError();
                }
            } else {
                bool frameReceived = false;
                QEventLoop loop;
                QTimer timer;
                timer.setSingleShot(true);

                auto conn = connect(player.get(), &IVideoPlayer::frameReady,
                                    &loop, [&capturedFrame, &frameReceived, &loop](const QImage &frame) {
                    capturedFrame = frame.copy();
                    frameReceived = true;
                    loop.quit();
                });
                connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);

                timer.start(kFrameExtractionTimeoutMs);
                player->seek(timeMs);
                if (!frameReceived) {
                    loop.exec();
                }
                timer.stop();
                disconnect(conn);
            }

            if (!capturedFrame.isNull()) {
                if (!cropRect.isEmpty()) {
                    const QRect frameRect(QPoint(0, 0), capturedFrame.size());
                    if (!frameRect.contains(cropRect)) {
                        qWarning() << "RecordingPreviewBackend: Crop rectangle" << cropRect
                                   << "exceeds frame bounds" << frameRect;
                        frameExtractionFailed = true;
                        break;
                    }
                    capturedFrame = capturedFrame.copy(cropRect);
                }
                if (cursorFilter) {
                    cursorFilter(capturedFrame, timeMs);
                }
                const qint64 framesBefore = gifEncoder
                    ? gifEncoder->framesWritten()
                    : webpEncoder->framesWritten();
                if (gifEncoder)
                    gifEncoder->writeFrame(capturedFrame, outputMs);
                else if (webpEncoder)
                    webpEncoder->writeFrame(capturedFrame, outputMs);

                const qint64 framesAfter = gifEncoder
                    ? gifEncoder->framesWritten()
                    : webpEncoder->framesWritten();
                if (framesAfter != framesBefore + 1) {
                    frameExtractionFailed = true;
                    break;
                }
            } else {
                qWarning() << "RecordingPreviewBackend: Frame extraction failed at"
                           << timeMs << "ms";
                frameExtractionFailed = true;
                break;
            }

            int percent = static_cast<int>((outputMs * 100) / qMax<qint64>(1, outputDurationMs));
            QCoreApplication* app = QCoreApplication::instance();
            if (app) {
                QMetaObject::invokeMethod(app, [weakThis, isCurrent, cancelToken, percent]() {
                    if (!isCurrent() || cancelToken->load()) {
                        return;
                    }
                    if (percent != weakThis->m_processProgress) {
                        weakThis->m_processProgress = percent;
                        emit weakThis->processProgressChanged();
                    }
                }, Qt::QueuedConnection);
            }
        }

        if (frameExtractionFailed) {
            if (gifEncoder) gifEncoder->abort();
            if (webpEncoder) webpEncoder->abort();
            QFile::remove(workingOutputPath);
            postFailure(frameExtractionError);
            return;
        }

        const qint64 encodedFrameCount = gifEncoder
            ? gifEncoder->framesWritten()
            : webpEncoder->framesWritten();

        if (stopIfCancelled(gifEncoder.get(), webpEncoder.get(), workingOutputPath)) {
            return;
        }
        // Writing out the file has no frame count to report: show it as its own phase.
        if (QCoreApplication* app = QCoreApplication::instance()) {
            QMetaObject::invokeMethod(app, [weakThis, isCurrent, cancelToken, writingStatus]() {
                if (isCurrent() && !cancelToken->load()) {
                    weakThis->setProcessPhase(writingStatus, true);
                }
            }, Qt::QueuedConnection);
        }

        // Finish encoding. Both encoders complete synchronously and report the
        // actual result through their finished signal.
        if (gifEncoder) gifEncoder->finish();
        if (webpEncoder) webpEncoder->finish();

        if (encodedFrameCount <= 0 || !encoderFinishedSuccessfully
            || encoderFinishedPath != workingOutputPath) {
            qWarning() << "RecordingPreviewBackend: Conversion encoder failed; keeping original";
            QFile::remove(workingOutputPath);
            postFailure(encodingFailedError);
            return;
        }

        if (QFileInfo::exists(outputPath)
            || !QFile::rename(workingOutputPath, outputPath)) {
            qWarning() << "RecordingPreviewBackend: Failed to promote conversion output";
            QFile::remove(workingOutputPath);
            postFailure(finalizeOutputError);
            return;
        }

        // Post results back to main thread
        QCoreApplication* app = QCoreApplication::instance();
        if (!app) {
            return;
        }

        const QSize croppedSize = cropRect.isEmpty() && framedSize.isEmpty() ? QSize() : outputSize;
        QMetaObject::invokeMethod(app, [weakThis, isCurrent, cancelToken, outputPath, outputMissingError,
                                        croppedSize]() {
            // The output is still only a temporary next to the recording, so a
            // cancel that arrives now still wins; so does a newer job.
            if (!isCurrent() || cancelToken->load()) {
                QFile::remove(outputPath);
                if (isCurrent()) weakThis->finishProcessing();
                return;
            }

            weakThis->m_processProgress = 100;
            emit weakThis->processProgressChanged();

            QFileInfo outInfo(outputPath);
            if (outInfo.exists() && outInfo.size() > 0) {
                // The source stays until the save dialog has committed the output.
                weakThis->handOffForSave(outputPath, croppedSize);
            } else {
                qWarning() << "RecordingPreviewBackend: Conversion output missing or empty, keeping original";
                weakThis->finishProcessing();
                weakThis->m_canRetrySave = true;
                weakThis->setErrorMessage(outputMissingError);
            }
        }, Qt::QueuedConnection);
    });
}

// ---------- MP4 trim / crop (runs on background thread) ----------

void RecordingPreviewBackend::performTranscode(bool smartSave)
{
    const int dotIndex = m_videoPath.lastIndexOf('.');
    const QString base = dotIndex > 0 ? m_videoPath.left(dotIndex) : m_videoPath;
    const QString stamp = QString::number(QDateTime::currentMSecsSinceEpoch());
    const QString outputPath = base + QStringLiteral("_edited_") + stamp + QStringLiteral(".mp4");
    const QString workingPath = base + QStringLiteral("_edited_") + stamp + QStringLiteral(".part-")
        + QUuid::createUuid().toString(QUuid::WithoutBraces) + QStringLiteral(".mp4");

    if (smartSave) {
        qDebug() << "RecordingPreviewBackend: Finalizing unedited recording";
    } else {
        qDebug() << "RecordingPreviewBackend: Exporting MP4 edits, trim:" << hasTrim()
                 << "crop:" << m_cropRect;
    }

    // Install the job and token before notifying QML or synchronous observers.
    m_exportCancelToken = std::make_shared<std::atomic_bool>(false);
    const auto cancelToken = m_exportCancelToken;
    m_exportKind = ExportKind::MP4;
    m_isProcessing = true;
    m_processProgress = 0;
    m_processStatus = tr("Exporting video...");
    emit processingChanged();
    emit processProgressChanged();
    emit processStatusChanged();

    VideoTranscodeRequest request;
    request.inputPath = m_videoPath;
    request.outputPath = workingPath;
    request.startMs = m_trimStart;
    request.endMs = m_trimEnd;
    request.cropRect = m_cropRect;
    if (audioMixActive()) {
        request.audioTracksVideoPath = m_videoPath;
        request.audioMix = m_audioMix;
    }
    if (hasTimelineEdits()) {
        // The kept range; joined parts when there is more than one.
        const SnapTray::TimelineMap map = timelineMap();
        request.startMs = map.sourceStartMs();
        request.endMs = map.sourceEndMs();
        if (!map.isPlainTrim()) request.segments = map.segments();
    }
    if (effectsActive()) {
        // A snapshot: later changes in the preview do not reach this job.
        request.frameFilter = effectsFrameFilter();
        request.filterOutputSize = framedExportSize();
    }
    QSize croppedSize = m_cropRect.isEmpty() ? QSize() : m_cropRect.size(); // the output size, set by the job
    const QString unsupportedError = tr("Video export is not supported on this platform");
    const QString failedTemplate = tr("Export failed: %1");
    // Transcoder and validation failures carry English diagnostics for the
    // log; the user sees this translated summary instead.
    const QString keptMessage = tr("Export failed; the original recording was kept.");
    const int outputQuality = m_outputQuality;
    // A snapshot, like the effects: changes in the preview do not reach this job.
    const SnapTray::Mp4ExportOptions mp4Options = effectiveMp4Options();
    const bool recordedAsIntermediate = m_recordedAsIntermediate;
    const bool checkExportSpace = std::exchange(m_exportSpaceCheckDeferred, false);
    const TranscoderFactory createTranscoder = transcoderFactoryOverride()
        ? transcoderFactoryOverride()
        : TranscoderFactory(&IVideoTranscoder::create);
    QPointer<RecordingPreviewBackend> weakThis(this);

    (void)QtConcurrent::run([weakThis, request, outputPath, croppedSize, unsupportedError, failedTemplate,
                             keptMessage, cancelToken, createTranscoder, smartSave, outputQuality,
                             mp4Options, recordedAsIntermediate, checkExportSpace]() mutable {
        // Queued onto the GUI thread; never blocks, so it is safe from the
        // transcoder's worker threads (see IVideoTranscoder::ProgressCallback).
        auto post = [](auto fn) {
            if (QCoreApplication* app = QCoreApplication::instance()) {
                QMetaObject::invokeMethod(app, fn, Qt::QueuedConnection);
                return true;
            }
            return false;
        };
        auto transcoder = createTranscoder();
        // Never export without drawing what was asked for: masks left out
        // would leave what they cover visible.
        // Nor with a size, frame rate or codec other than the one asked for.
        const bool unsupported = !transcoder || (request.frameFilter && !transcoder->supportsFrameFilter())
            || (!request.segments.empty() && !transcoder->supportsSegments())
            || (!request.audioTracksVideoPath.isEmpty() && !transcoder->supportsAudioMix())
            || ((mp4Options.resolution != SnapTray::Mp4ExportOptions::Resolution::Original || mp4Options.frameRate > 0)
                && !transcoder->supportsOutputOptions())
            || !transcoder->supportsVideoCodec(mp4Options.codecId());
        VideoTranscodeResult result;
        VideoFileProbe sourceProbe;
        bool moveInstead = false;
        const char* moveReason = "";
        if (unsupported) {
            // Without a transcoder an unedited save still works as before.
            moveInstead = smartSave;
            moveReason = "no transcoder on this platform";
            if (!moveInstead) result.errorMessage = unsupportedError;
        } else if (smartSave && !recordedAsIntermediate && !mp4Options.mayChangeStream()
                   && mp4Options.quality == SnapTray::Mp4ExportOptions::Quality::Default) {
            // Already encoded at the user's quality; a re-encode would only lose detail.
            moveInstead = true;
            moveReason = "not an intermediate: recorded at the selected quality; saving as recorded";
        } else {
            sourceProbe = transcoder->probe(request.inputPath);
            // A smaller size, a lower frame rate or another codec than the
            // source's is an export like any edit: never a move, and a failure
            // is reported instead of saving the recording as it is.
            if (smartSave && sourceProbe.valid
                && mp4Options.changesStream(request.cropRect.isEmpty() ? sourceProbe.videoSize
                                                                       : request.cropRect.size(),
                                            sourceProbe.frameRate, sourceProbe.videoCodec)) {
                smartSave = false;
                // The room check Save left to this job: the options do change
                // the stream, so this is an export (PR #197 review).
                if (checkExportSpace) {
                    const QString directory = QFileInfo(request.inputPath).absolutePath();
                    const qint64 freeBytes = freeBytesForPathOverride() ? freeBytesForPathOverride()(directory)
                                                                        : QStorageInfo(directory).bytesAvailable();
                    const qint64 needed = SnapTray::DiskSpaceGuard::kFloorBytes
                        + qMax<qint64>(0, QFileInfo(request.inputPath).size());
                    if (freeBytes >= 0 && freeBytes < needed) {
                        qWarning() << "RecordingPreviewBackend: not enough space to export:" << freeBytes << "<" << needed;
                        const QString message = tr("Not enough free disk space to export (%1 free, about %2 needed). "
                                                   "Free up space and try again; your recording is kept.")
                                                    .arg(QLocale().formattedDataSize(freeBytes),
                                                         QLocale().formattedDataSize(needed));
                        post([weakThis, message, cancelToken]() {
                            if (!weakThis) return;
                            weakThis->finishProcessing();
                            if (cancelToken->load()) return;
                            weakThis->m_canRetrySave = true;
                            weakThis->setErrorMessage(message);
                        });
                        return;
                    }
                }
            }
            // An explicit quality is judged by its bitrate like an intermediate:
            // Low re-encodes a recording above Low's target (PR #197 review).
            if (smartSave && !recordedAsIntermediate
                && mp4Options.quality == SnapTray::Mp4ExportOptions::Quality::Default) {
                moveInstead = true;
                moveReason = "not an intermediate: recorded at the selected quality; saving as recorded";
            } else if (smartSave && !sourceProbe.valid) {
                // A re-encode would only fail validation and leave the user
                // with Discard; keep the recording as recorded.
                qWarning() << "RecordingPreviewBackend: unedited recording could not be probed; saving it as recorded";
                moveInstead = true;
                moveReason = "unprobeable";
            } else if (smartSave && SnapTray::IntermediateQuality::smartSaveShouldMove(
                                        sourceProbe, QFileInfo(request.inputPath).size(),
                                        mp4Options.outputQuality(outputQuality))) {
                moveInstead = true;
                moveReason = "meets the output quality target";
            }
        }
        if (smartSave && !moveInstead && !unsupported) {
            // Re-encoding an unedited recording writes a second file. Without
            // room for it, save the recording as recorded (a move needs no
            // space) instead of filling the disk with an export.
            const QString directory = QFileInfo(request.outputPath).absolutePath();
            const qint64 freeBytes = freeBytesForPathOverride() ? freeBytesForPathOverride()(directory)
                                                                : QStorageInfo(directory).bytesAvailable();
            const qint64 needed = SnapTray::DiskSpaceGuard::kFloorBytes
                + qMax<qint64>(0, QFileInfo(request.inputPath).size());
            if (freeBytes >= 0 && freeBytes < needed) {
                moveInstead = true;
                moveReason = "not enough free space to re-encode";
            }
        }
        if (moveInstead) {
            qDebug() << "RecordingPreviewBackend: moving the unedited recording;" << moveReason;
            const QString inputPath = request.inputPath;
            post([weakThis, inputPath, cancelToken]() {
                if (!weakThis) return;
                // GUI commit boundary: cancellation wins until this callback starts.
                const bool cancelled = cancelToken->load();
                weakThis->finishProcessing();
                if (cancelled) return;
                weakThis->handOffForSave(inputPath, QSize());
            });
            return;
        }
        if (!unsupported) {
            const IVideoTranscoder::ProgressCallback progressCallback =
                [weakThis, cancelToken, post](int percent) {
                    post([weakThis, percent, cancelToken]() {
                        if (weakThis && weakThis->m_isProcessing && weakThis->m_exportCancelToken == cancelToken
                            && !cancelToken->load() && weakThis->m_processProgress != percent) {
                            weakThis->m_processProgress = percent;
                            emit weakThis->processProgressChanged();
                        }
                    });
                    return !cancelToken->load();
                };
            const QSize contentSize = request.cropRect.isEmpty() ? sourceProbe.videoSize : request.cropRect.size();
            const QSize baseSize = request.filterOutputSize.isEmpty() ? contentSize : request.filterOutputSize;
            const QSize outputSize = mp4Options.outputSize(baseSize);
            if (outputSize != baseSize) request.outputSize = outputSize;
            const int outputFps = mp4Options.outputFrameRate(sourceProbe.frameRate);
            if (mp4Options.frameRate > 0) request.frameRate = outputFps;
            if (mp4Options.codec != SnapTray::Mp4ExportOptions::Codec::H264) request.videoCodec = mp4Options.codecId();
            request.videoBitrate = mp4Options.videoBitrate(outputSize, outputFps, outputQuality);
            // The file name's {w}x{h} is the picture's real size.
            if (!request.cropRect.isEmpty() || outputSize != contentSize) croppedSize = outputSize;
            qDebug() << "RecordingPreviewBackend: exporting at" << request.videoBitrate << "bps for"
                     << outputSize << "at" << outputFps << "fps," << mp4Options.codecId()
                     << "quality" << mp4Options.outputQuality(outputQuality);
            result = transcoder->transcode(request, progressCallback);
        }
        // Do not trust the transcoder's claim alone: the source is deleted on
        // success, so the output must be a playable file that kept whatever
        // audio the selected range had. A range the source audio does not
        // reach (within kAudioCoverageToleranceMs) exports video only, as the
        // source is there; that is not lost audio.
        if (result.success) {
            const VideoFileProbe outputProbe = transcoder->probe(request.outputPath);
            const QFileInfo outputInfo(request.outputPath);
            const qint64 rangeStartMs = qMax<qint64>(0, request.startMs);
            const qint64 rangeEndMs = request.endMs < 0 ? sourceProbe.durationMs
                                                        : qMin(request.endMs, sourceProbe.durationMs);
            qint64 audioCoverageMs = 0;
            if (sourceProbe.hasAudio && request.segments.empty()) {
                audioCoverageMs = qMax<qint64>(0, qMin(sourceProbe.audioEndMs, rangeEndMs)
                                                      - qMax(sourceProbe.audioStartMs, rangeStartMs));
            } else if (sourceProbe.hasAudio) {
                // Joined parts keep the audio of the parts at normal speed (#185).
                for (const auto& segment : request.segments) {
                    if (!qFuzzyCompare(segment.speed, 1.0)) continue;
                    audioCoverageMs += qMax<qint64>(0, qMin(sourceProbe.audioEndMs, segment.endMs)
                                                           - qMax(sourceProbe.audioStartMs, segment.startMs));
                }
            }
            const bool expectAudio = audioCoverageMs > kAudioCoverageToleranceMs;
            // The picture must be exactly what was asked for: a crop the
            // transcoder had to shrink or ignore is a different export (and
            // would be named by the requested size).
            const QSize expectedSize = !request.outputSize.isEmpty() ? request.outputSize
                : !request.filterOutputSize.isEmpty() ? request.filterOutputSize
                : (request.cropRect.isEmpty() ? sourceProbe.videoSize : request.cropRect.size());
            const QString expectedCodec = request.videoCodec.isEmpty() ? QString::fromLatin1(kVideoCodecH264)
                                                                        : request.videoCodec;
            if (cancelToken->load() || !sourceProbe.valid || !outputProbe.valid
                || !outputInfo.exists() || outputInfo.size() <= 0 || outputProbe.durationMs <= 0
                || outputProbe.videoSize != expectedSize
                || (!outputProbe.videoCodec.isEmpty() && outputProbe.videoCodec != expectedCodec)
                || (expectAudio && (!result.audioCopied || !outputProbe.hasAudio))
                || (result.audioCopied && !outputProbe.hasAudio)) {
                qWarning() << "RecordingPreviewBackend: MP4 export output failed validation;"
                           << "cancelled:" << cancelToken->load()
                           << "source valid:" << sourceProbe.valid
                           << "output valid:" << outputProbe.valid
                           << "output size:" << outputProbe.videoSize << "expected:" << expectedSize
                           << "output codec:" << outputProbe.videoCodec << "expected:" << expectedCodec
                           << "source audio:" << sourceProbe.hasAudio
                           << "audio coverage (ms):" << audioCoverageMs
                           << "audio copied:" << result.audioCopied
                           << "output audio:" << outputProbe.hasAudio;
                QFile::remove(request.outputPath);
                result.success = false;
                result.errorMessage = QStringLiteral("Output validation failed; source retained");
            }
        } else {
            qWarning() << "RecordingPreviewBackend: MP4 export failed:" << result.errorMessage;
            // The transcoder removes its own output on failure; a fake or
            // misbehaving one must not leave a partial file behind either.
            QFile::remove(request.outputPath);
        }
        if (result.success && !QFile::rename(request.outputPath, outputPath)) {
            qWarning() << "RecordingPreviewBackend: Failed to promote MP4 export output";
            QFile::remove(request.outputPath);
            result.success = false;
            result.errorMessage = QStringLiteral("rename failed");
        }
        // A cancelled export keeps the source and shows no error. Cancellation
        // may still arrive after promotion, before the GUI commits the result.
        const bool posted = post([weakThis, result, outputPath, croppedSize, failedTemplate, keptMessage,
                                  unsupported, unsupportedError, inputPath = request.inputPath, cancelToken,
                                  smartSave]() {
            if (!weakThis) {
                // The preview went away mid-export; keep the source, drop the output.
                QFile::remove(outputPath);
                return;
            }
            // Read on the GUI thread, not before posting: the user may have
            // cancelled while this completion was waiting in the event queue.
            const bool cancelled = cancelToken->load();
            if (cancelled) QFile::remove(outputPath);
            weakThis->finishProcessing();
            if (cancelled) {
                qDebug() << "RecordingPreviewBackend: export cancelled; the original recording was kept";
                return;
            }
            if (!result.success) {
                if (smartSave) {
                    // An unedited recording is still a valid recording: save
                    // it as recorded rather than leave the user with Discard.
                    qWarning() << "RecordingPreviewBackend: moving the unedited recording; re-encode failed:"
                               << result.errorMessage;
                    weakThis->handOffForSave(inputPath, QSize());
                    return;
                }
                weakThis->m_canRetrySave = true;
                weakThis->setErrorMessage(unsupported ? failedTemplate.arg(unsupportedError) : keptMessage);
                return;
            }
            // The source stays until the save dialog has committed the output.
            weakThis->handOffForSave(outputPath, croppedSize);
        });
        if (!posted && result.success) {
            QFile::remove(outputPath);
        }
    });
}
