#include <QtTest/QtTest>

#include "qml/QmlOverlayManager.h"
#include "qml/LongshotController.h"
#include "utils/VideoCropGeometry.h"

#include <QImage>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickView>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

#include <memory>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

class PreviewFrameSource final : public SnapTray::Longshot::LongshotFrameSource {
public:
    PreviewFrameSource() : m_frame(320, 480, QImage::Format_RGB32) {
        for (int y = 0; y < m_frame.height(); ++y)
            for (int x = 0; x < m_frame.width(); ++x)
                m_frame.setPixel(x, y, qRgb((x * 13 + y * 7) % 240, (x * 3 + y * 11) % 240, (x + y * 3) % 240));
    }
    bool open(const QString&, qint64, qint64, const QRect&) override { m_index = 0; return true; }
    std::optional<QImage> next(qint64* time) override {
        if (m_index >= 3) return {};
        *time = m_index++ * 50; return m_frame;
    }
    QSize frameSize() const override { return m_frame.size(); }
    QSize videoSize() const override { return m_frame.size(); }
    double frameRate() const override { return 20; }
    int expectedFrameCount() const override { return 3; }
    QString lastError() const override { return {}; }
private:
    QImage m_frame;
    int m_index = 0;
};

constexpr int kEdgeLeft = 1;
constexpr int kEdgeRight = 2;
constexpr int kEdgeTop = 4;
constexpr int kEdgeBottom = 8;
constexpr qreal kMinViewSide = 8.0;

constexpr int kPreviewWidth = 800;
constexpr int kPreviewHeight = 600;
constexpr int kPreviewMinWidth = 640;
constexpr int kPreviewMinHeight = 480;
constexpr int kDragSteps = 5;

QVariant invokeQml(QObject* object, const char* name, const QVariantList& args = {})
{
    QVariant ret;
    bool ok = false;
    switch (args.size()) {
    case 0:
        ok = QMetaObject::invokeMethod(object, name, Q_RETURN_ARG(QVariant, ret));
        break;
    case 1:
        ok = QMetaObject::invokeMethod(object, name, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, args.at(0)));
        break;
    case 2:
        ok = QMetaObject::invokeMethod(object, name, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, args.at(0)),
                                       Q_ARG(QVariant, args.at(1)));
        break;
    case 3:
        ok = QMetaObject::invokeMethod(object, name, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, args.at(0)),
                                       Q_ARG(QVariant, args.at(1)), Q_ARG(QVariant, args.at(2)));
        break;
    case 4:
        ok = QMetaObject::invokeMethod(object, name, Q_RETURN_ARG(QVariant, ret), Q_ARG(QVariant, args.at(0)),
                                       Q_ARG(QVariant, args.at(1)), Q_ARG(QVariant, args.at(2)),
                                       Q_ARG(QVariant, args.at(3)));
        break;
    default:
        break;
    }
    if (!ok) {
        qWarning() << "invoke failed:" << name;
    }
    return ret;
}

} // namespace

/**
 * Minimal stand-in for RecordingPreviewBackend: the same QML surface, with the
 * crop state driven by the real VideoCropGeometry helpers, and save()/discard()
 * recorded instead of exporting or closing.
 */
class StubPreviewBackend : public QObject
{
    Q_OBJECT
    Q_PROPERTY(LongshotController* longshot READ longshot CONSTANT)
    Q_PROPERTY(QString videoPath READ videoPath CONSTANT)
    Q_PROPERTY(qint64 trimStart READ trimStart WRITE setTrimStart NOTIFY trimRangeChanged)
    Q_PROPERTY(qint64 trimEnd READ trimEnd WRITE setTrimEnd NOTIFY trimRangeChanged)
    Q_PROPERTY(bool hasTrim READ hasTrim NOTIFY trimRangeChanged)
    Q_PROPERTY(QRect cropRect READ cropRect NOTIFY cropRectChanged)
    Q_PROPERTY(bool hasCrop READ hasCrop NOTIFY cropRectChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(int minCropSide READ minCropSide CONSTANT)
    Q_PROPERTY(int selectedFormat READ selectedFormat WRITE setSelectedFormat NOTIFY formatChanged)
    Q_PROPERTY(bool isProcessing READ isProcessing WRITE setProcessing NOTIFY processingChanged)
    Q_PROPERTY(bool canCancelExport READ canCancelExport NOTIFY processingChanged)
    Q_PROPERTY(int processProgress READ processProgress CONSTANT)
    Q_PROPERTY(QString processStatus READ processStatus CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage CONSTANT)
    Q_PROPERTY(bool hasWindowTimeline READ hasWindowTimeline NOTIFY windowTimelineChanged)

public:
    LongshotController* longshot() { return &m_longshot; }
private:
    LongshotController m_longshot{nullptr, [] { return std::make_unique<PreviewFrameSource>(); }};
public:
    bool hasWindowTimeline() const { return !m_stubWindowRect.isEmpty(); }
    // The one "window" of the stub timeline, in video pixels; empty = no timeline.
    void setStubWindow(const QRect& videoRect, const QString& app)
    {
        m_stubWindowRect = videoRect;
        m_stubWindowApp = app;
        emit windowTimelineChanged();
    }
    Q_INVOKABLE QRectF windowRectInViewAt(const QPointF& viewPoint, const QRectF& contentRect, qint64 positionMs) const
    {
        lookupCount++;
        lastLookupPositionMs = positionMs;
        const QPoint video = SnapTray::VideoCropGeometry::viewPointToVideo(viewPoint, contentRect, m_videoSize);
        if (m_stubWindowRect.isEmpty() || video.x() < 0 || !m_stubWindowRect.contains(video)) {
            return {};
        }
        return SnapTray::VideoCropGeometry::videoToView(m_stubWindowRect, contentRect, m_videoSize);
    }
    Q_INVOKABLE QString windowAppAt(const QPointF& viewPoint, const QRectF& contentRect, qint64 positionMs) const
    {
        return windowRectInViewAt(viewPoint, contentRect, positionMs).isEmpty() ? QString() : m_stubWindowApp;
    }
    mutable int lookupCount = 0;
    mutable qint64 lastLookupPositionMs = -1;

    QString videoPath() const { return {}; }
    qint64 trimStart() const { return 0; }
    qint64 trimEnd() const { return 0; }
    void setTrimStart(qint64) {}
    void setTrimEnd(qint64) {}
    bool hasTrim() const { return false; }
    QRect cropRect() const { return m_cropRect; }
    bool hasCrop() const { return !m_cropRect.isEmpty(); }
    QSize videoSize() const { return m_videoSize; }
    int minCropSide() const { return SnapTray::VideoCropGeometry::kMinCropSide; }
    int selectedFormat() const { return m_selectedFormat; }
    void setSelectedFormat(int format)
    {
        m_selectedFormat = format;
        emit formatChanged();
    }
    bool isProcessing() const { return m_isProcessing; }
    bool canCancelExport() const { return m_isProcessing && m_activeFormat == 0 && !m_cancelRequested; }
    void setProcessing(bool processing)
    {
        m_isProcessing = processing;
        m_activeFormat = m_selectedFormat;
        m_cancelRequested = false;
        emit processingChanged();
    }
    Q_INVOKABLE void cancelExport()
    {
        if (!canCancelExport()) return;
        ++cancelCount;
        m_cancelRequested = true;
        emit processingChanged();
    }
    int cancelCount = 0;
    int processProgress() const { return 0; }
    QString processStatus() const { return {}; }
    QString errorMessage() const { return {}; }

    Q_INVOKABLE void save()
    {
        ++saveCount;
        cropAtSave = m_cropRect;
    }
    Q_INVOKABLE void discard() { ++discardCount; }
    Q_INVOKABLE void toggleTrim() {}
    // Same "mm:ss" shape as the real backend, so toolbar widths are realistic.
    Q_INVOKABLE QString formatTime(qint64 ms) const
    {
        const int totalSeconds = static_cast<int>(ms / 1000);
        return QStringLiteral("%1:%2")
            .arg(totalSeconds / 60, 2, 10, QLatin1Char('0'))
            .arg(totalSeconds % 60, 2, 10, QLatin1Char('0'));
    }
    Q_INVOKABLE void clearError() {}
    Q_INVOKABLE void reportPlaybackError(const QString&) {}
    Q_INVOKABLE void updatePosition(qint64) {}
    Q_INVOKABLE void updateDuration(qint64) {}
    Q_INVOKABLE void updatePlayingState(bool) {}

    Q_INVOKABLE void updateVideoSize(const QSize& size)
    {
        if (m_videoSize == size) {
            return;
        }
        m_videoSize = size;
        emit videoSizeChanged();
        setCropRect(m_cropRect);
    }
    Q_INVOKABLE void setCropRect(const QRect& videoRect)
    {
        const QRect normalized = SnapTray::VideoCropGeometry::normalizeCropRect(videoRect, m_videoSize);
        if (normalized == m_cropRect) {
            return;
        }
        m_cropRect = normalized;
        emit cropRectChanged();
    }
    Q_INVOKABLE void setCropFromView(const QRectF& viewRect, const QRectF& contentRect)
    {
        ++setCropFromViewCount;
        setCropRect(SnapTray::VideoCropGeometry::viewToVideo(viewRect, contentRect, m_videoSize));
    }
    Q_INVOKABLE QRectF cropRectInView(const QRectF& contentRect) const
    {
        if (!hasCrop()) {
            return contentRect;
        }
        return SnapTray::VideoCropGeometry::videoToView(m_cropRect, contentRect, m_videoSize);
    }
    Q_INVOKABLE void clearCrop()
    {
        if (m_cropRect.isNull()) {
            return;
        }
        m_cropRect = QRect();
        emit cropRectChanged();
    }

    int saveCount = 0;
    int discardCount = 0;
    int setCropFromViewCount = 0;
    QRect cropAtSave;

signals:
    void trimRangeChanged();
    void cropRectChanged();
    void videoSizeChanged();
    void formatChanged();
    void processingChanged();
    void windowTimelineChanged();

private:
    QRect m_stubWindowRect;
    QString m_stubWindowApp;
    QRect m_cropRect;
    QSize m_videoSize;
    int m_selectedFormat = 0;
    int m_activeFormat = 0;
    bool m_cancelRequested = false;
    bool m_isProcessing = false;
};

class tst_RecordingCropOverlay : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    // Overlay helpers (no window).
    void beginEditingStartsEmpty();
    void enterStartsHoverWithoutMove();
    void createRectClampsToContent();
    void moveRectStaysInside();
    void resizeRectRespectsEdges();
    void snapValueSnapsWithinDistance();
    void applyEmitsDraft();
    void cancelLeavesEditing();
    void beginEditingNeedsContent();
    void contentChangeMovesDraft();

    // Bounded snapping.
    void moveSnapNeverLeavesContentX();
    void moveSnapNeverLeavesContentY();
    void moveSweepStaysInside();
    void resizeNearCentreKeepsMinimum_data();
    void resizeNearCentreKeepsMinimum();
    void resizeSweepKeepsMinimumAndBounds();
    void createSweepStaysInside();

    // Edge hit-testing on small selections (#116).
    void edgesAtSmallRectKeepsMoveZone();
    void edgesAtSweepSmallRects();

    // Draft minimum follows the backend's minimum crop size (#117).
    void minViewSizeFollowsBackendMinimum();
    void resizeRectKeepsBackendMinimum_data();
    void resizeRectKeepsBackendMinimum();
    void createRectKeepsBackendMinimum_data();
    void createRectKeepsBackendMinimum();
    void createSweepKeepsBackendMinimum();

    // Full RecordingPreview in a real window with mouse/key events.
    void previewCreatesFirstSelection_data();
    void previewCreatesFirstSelection();
    void previewMoveResizeReplace();
    void previewEscapeRestoresCommitted();
    void previewEmptyDraftApplyIsNoop();
    void previewSaveButtonExportsDraft();
    void previewCtrlSExportsDraft();
    void previewEnterSavesWhenNotEditing();
    void previewProcessingBlocksSave();
    void previewCancellationFollowsActiveJob();
    void previewGeometryChangeKeepsDraft();
    void previewSizeChipAndClear();
    void previewToolbarFitsAtMinimumWidth();
    void previewLongshotResultKeepsRecording();
    void previewLongshotImageEdits();
    void previewCursorOverVideo();
    void previewSmallSelectionMoves();
    void previewReleaseCommitsFinalPosition();
    void previewTinyDraftAppliesWithoutJump();
    void previewCropUnavailableAfterFrameCleared();

    // The draft follows the content when the window is resized (#11).
    void draftFollowsContentRect();

    // Window snapping (Phase 2).
    void snapToRectClampsAndGrows_data();
    void snapToRectClampsAndGrows();
    void previewHoverHighlightsWindow();
    void previewClickSnapsToWindow();
    void previewDragStillDrawsFreeRect();
    void previewNoTimelineNoHoverNoHint();
    void previewDragAndReturnDoesNotSnap();
    void previewClickInsideDraftKeepsDraft();
    void previewNoHighlightOverExistingDraft();
    void previewEndEditingClearsHover();

private:
    QVariant call(const char* name, const QVariantList& args = {});
    QRectF overlayRect(const char* property) const;

    // Unavailable: the environment cannot show the window (skip). Failed: the
    // preview itself is broken (fail); `reason` says why.
    enum class PreviewOpen { Ready, Unavailable, Failed };
    PreviewOpen openPreview(const QSize& frameSize, QString* reason);
    QObject* previewOverlay() const;
    QQuickItem* previewItem(const char* objectName) const;
    QRectF previewContentRect() const;
    QPoint scenePoint(QQuickItem* item, const QPointF& local) const;
    QPoint overlayPoint(qreal x, qreal y) const;
    void click(QQuickItem* item);
    void drag(const QPoint& from, const QPoint& to);
    void sendKey(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier);
    Qt::CursorShape cursorAt(const QPoint& scenePos);
    bool editing() const;
    QRect expectedVideoCrop(const QRectF& viewRect) const;

    std::unique_ptr<QObject> m_overlay;
    std::unique_ptr<StubPreviewBackend> m_backend;
    std::unique_ptr<QQuickView> m_view;
};

void tst_RecordingCropOverlay::init()
{
    QQmlComponent component(SnapTray::QmlOverlayManager::instance().engine(),
                            QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingCropOverlay.qml")));
    m_overlay.reset(component.create());
    QVERIFY2(m_overlay, qPrintable(component.errorString()));
    m_overlay->setProperty("width", 400);
    m_overlay->setProperty("height", 300);
    m_overlay->setProperty("contentRect", QRectF(0, 50, 400, 200));
    m_overlay->setProperty("videoSize", QSize(800, 400));
}

void tst_RecordingCropOverlay::cleanup()
{
    m_view.reset();
    m_backend.reset();
    m_overlay.reset();
}

QVariant tst_RecordingCropOverlay::call(const char* name, const QVariantList& args)
{
    return invokeQml(m_overlay.get(), name, args);
}

QRectF tst_RecordingCropOverlay::overlayRect(const char* property) const
{
    return m_overlay->property(property).toRectF();
}

// ---------- Overlay helpers ----------

void tst_RecordingCropOverlay::beginEditingStartsEmpty()
{
    call("beginEditing");
    QVERIFY(m_overlay->property("editing").toBool());
    QCOMPARE(m_overlay->property("draftRect").toRectF(), QRectF(0, 0, 0, 0));
}

void tst_RecordingCropOverlay::enterStartsHoverWithoutMove()
{
    call("beginEditing");
    auto* mouseArea = m_overlay->findChild<QObject*>(QStringLiteral("cropMouseArea"));
    QVERIFY(mouseArea);
    QSignalSpy hoverChanged(m_overlay.get(), SIGNAL(hoverChanged()));
    // Enter can arrive without positionChanged, including when editing makes
    // the crop MouseArea visible beneath a resting pointer.
    QVERIFY(QMetaObject::invokeMethod(mouseArea, "entered"));
    QVERIFY(m_overlay->property("hovering").toBool());
    QCOMPARE(hoverChanged.count(), 1);
    QCOMPARE(m_overlay->property("hoverPoint").toPointF(),
             QPointF(mouseArea->property("mouseX").toReal(), mouseArea->property("mouseY").toReal()));
    QVERIFY(QMetaObject::invokeMethod(mouseArea, "exited"));
    QVERIFY(!m_overlay->property("hovering").toBool());
    QCOMPARE(hoverChanged.count(), 2);

    call("cancel");
    const int count = hoverChanged.count();
    QVERIFY(QMetaObject::invokeMethod(mouseArea, "entered"));
    QVERIFY(!m_overlay->property("hovering").toBool());
    QCOMPARE(hoverChanged.count(), count);
}

void tst_RecordingCropOverlay::createRectClampsToContent()
{
    // Drag from inside the content to beyond its bottom-right corner.
    const QRectF r = call("createRect", {100, 100, 500, 400}).toRectF();
    QCOMPARE(r, QRectF(100, 100, 300, 150));
}

void tst_RecordingCropOverlay::moveRectStaysInside()
{
    const QRectF r = call("moveRect", {QRectF(300, 200, 80, 40), 100, 100}).toRectF();
    QCOMPARE(r, QRectF(320, 210, 80, 40));
}

void tst_RecordingCropOverlay::resizeRectRespectsEdges()
{
    const QRectF r = call("resizeRect", {QRectF(100, 100, 100, 50), kEdgeRight | kEdgeBottom, 20, 10}).toRectF();
    QCOMPARE(r, QRectF(100, 100, 120, 60));
}

void tst_RecordingCropOverlay::snapValueSnapsWithinDistance()
{
    QCOMPARE(call("snapValue", {203, QVariantList{0, 200, 400}}).toReal(), 200.0);
    QCOMPARE(call("snapValue", {190, QVariantList{0, 200, 400}}).toReal(), 190.0);
}

void tst_RecordingCropOverlay::applyEmitsDraft()
{
    call("beginEditing");
    m_overlay->setProperty("draftRect", QRectF(10, 60, 100, 50));
    QSignalSpy spy(m_overlay.get(), SIGNAL(applyRequested(QRectF)));
    call("apply");
    QCOMPARE(spy.count(), 1);
    QCOMPARE(spy.first().at(0).toRectF(), QRectF(10, 60, 100, 50));
    QVERIFY(!m_overlay->property("editing").toBool());
}

void tst_RecordingCropOverlay::cancelLeavesEditing()
{
    call("beginEditing");
    QSignalSpy spy(m_overlay.get(), SIGNAL(cancelRequested()));
    call("cancel");
    QCOMPARE(spy.count(), 1);
    QVERIFY(!m_overlay->property("editing").toBool());
}

void tst_RecordingCropOverlay::beginEditingNeedsContent()
{
    m_overlay->setProperty("videoSize", QSize(0, 0));
    call("beginEditing");
    QVERIFY(!m_overlay->property("editing").toBool());
}

void tst_RecordingCropOverlay::contentChangeMovesDraft()
{
    m_overlay->setProperty("committedRect", QRectF(50, 100, 100, 50));
    call("beginEditing");
    m_overlay->setProperty("draftRect", QRectF(10, 60, 100, 50));
    QSignalSpy applySpy(m_overlay.get(), SIGNAL(applyRequested(QRectF)));
    QSignalSpy cancelSpy(m_overlay.get(), SIGNAL(cancelRequested()));
    // The content grows 10% taller and moves up: the draft keeps framing the
    // same video pixels, nothing is applied or cancelled.
    m_overlay->setProperty("contentRect", QRectF(0, 40, 400, 220));
    QVERIFY(m_overlay->property("editing").toBool());
    QCOMPARE(cancelSpy.count(), 0);
    QCOMPARE(applySpy.count(), 0);
    QCOMPARE(overlayRect("draftRect"), QRectF(10, 51, 100, 55));
    QCOMPARE(overlayRect("shownRect"), QRectF(10, 51, 100, 55));
}

// ---------- Bounded snapping (content x 0..400, y 50..250) ----------

void tst_RecordingCropOverlay::moveSnapNeverLeavesContentX()
{
    // Width 204: the last legal x is 196. The centre line (200) is not a legal target.
    const QRectF atLeft(0, 50, 204, 40);
    for (int dx = 190; dx <= 210; ++dx) {
        const QRectF r = call("moveRect", {atLeft, dx, 0}).toRectF();
        QVERIFY2(r.x() <= 196.0, qPrintable(QStringLiteral("dx=%1 x=%2").arg(dx).arg(r.x())));
        QVERIFY(r.right() <= 400.0);
    }
    QCOMPARE(call("moveRect", {atLeft, 194, 0}).toRectF().x(), 196.0); // snaps right edge to 400
    QCOMPARE(call("moveRect", {atLeft, 198, 0}).toRectF().x(), 196.0); // unbounded snap would give 200

    // Mirror: moving left never passes x=0.
    const QRectF atRight(196, 50, 204, 40);
    for (int dx = -210; dx <= -190; ++dx) {
        const QRectF r = call("moveRect", {atRight, dx, 0}).toRectF();
        QVERIFY2(r.x() >= 0.0, qPrintable(QStringLiteral("dx=%1 x=%2").arg(dx).arg(r.x())));
    }
    QCOMPARE(call("moveRect", {atRight, -194, 0}).toRectF().x(), 0.0);
    // Left edge near the centre-line-minus-width target (-4) is not legal either.
    QCOMPARE(call("moveRect", {atRight, -199, 0}).toRectF().x(), 0.0);
}

void tst_RecordingCropOverlay::moveSnapNeverLeavesContentY()
{
    // Height 104: the last legal y is 146. The centre line (150) is not a legal target.
    const QRectF atTop(0, 50, 40, 104);
    for (int dy = 90; dy <= 110; ++dy) {
        const QRectF r = call("moveRect", {atTop, 0, dy}).toRectF();
        QVERIFY2(r.y() <= 146.0, qPrintable(QStringLiteral("dy=%1 y=%2").arg(dy).arg(r.y())));
        QVERIFY(r.bottom() <= 250.0);
    }
    QCOMPARE(call("moveRect", {atTop, 0, 94}).toRectF().y(), 146.0);
    QCOMPARE(call("moveRect", {atTop, 0, 98}).toRectF().y(), 146.0);

    const QRectF atBottom(0, 146, 40, 104);
    for (int dy = -110; dy <= -90; ++dy) {
        const QRectF r = call("moveRect", {atBottom, 0, dy}).toRectF();
        QVERIFY2(r.y() >= 50.0, qPrintable(QStringLiteral("dy=%1 y=%2").arg(dy).arg(r.y())));
    }
    QCOMPARE(call("moveRect", {atBottom, 0, -94}).toRectF().y(), 50.0);
}

void tst_RecordingCropOverlay::moveSweepStaysInside()
{
    const QRectF content(0, 50, 400, 200);
    const QRectF start(98, 98, 204, 104);
    for (int d = -320; d <= 320; d += 1) {
        const QRectF r = call("moveRect", {start, d, d}).toRectF();
        QVERIFY2(content.contains(r), qPrintable(QStringLiteral("d=%1 r=%2,%3").arg(d).arg(r.x()).arg(r.y())));
        QCOMPARE(r.size(), start.size());
    }
}

void tst_RecordingCropOverlay::resizeNearCentreKeepsMinimum_data()
{
    QTest::addColumn<QRectF>("start");
    QTest::addColumn<int>("edges");
    QTest::addColumn<int>("dx");
    QTest::addColumn<int>("dy");
    QTest::addColumn<QRectF>("expected");
    // Centre lines are x=200 and y=150. Snapping to them would leave < kMinViewSide.
    QTest::newRow("left") << QRectF(195, 50, 10, 40) << kEdgeLeft << 4 << 0 << QRectF(197, 50, 8, 40);
    QTest::newRow("right") << QRectF(195, 50, 10, 40) << kEdgeRight << -4 << 0 << QRectF(195, 50, 8, 40);
    QTest::newRow("top") << QRectF(0, 145, 40, 10) << kEdgeTop << 0 << 4 << QRectF(0, 147, 40, 8);
    QTest::newRow("bottom") << QRectF(0, 145, 40, 10) << kEdgeBottom << 0 << -4 << QRectF(0, 145, 40, 8);
}

void tst_RecordingCropOverlay::resizeNearCentreKeepsMinimum()
{
    QFETCH(QRectF, start);
    QFETCH(int, edges);
    QFETCH(int, dx);
    QFETCH(int, dy);
    QFETCH(QRectF, expected);
    const QRectF r = call("resizeRect", {start, edges, dx, dy}).toRectF();
    QCOMPARE(r, expected);
}

void tst_RecordingCropOverlay::resizeSweepKeepsMinimumAndBounds()
{
    constexpr int kGarbageCollectionBatchSize = 128;
    int callsSinceCollection = 0;
    const QRectF content(0, 50, 400, 200);
    const QList<QRectF> starts{QRectF(195, 145, 10, 10), QRectF(0, 50, 20, 20), QRectF(380, 230, 20, 20),
                               QRectF(100, 100, 150, 100)};
    const QList<int> edgeSets{kEdgeLeft, kEdgeRight, kEdgeTop, kEdgeBottom, kEdgeLeft | kEdgeTop,
                              kEdgeRight | kEdgeBottom, kEdgeLeft | kEdgeBottom, kEdgeRight | kEdgeTop};
    for (const QRectF& start : starts) {
        for (int edges : edgeSets) {
            for (int d = -420; d <= 420; d += 1) {
                const QRectF r = call("resizeRect", {start, edges, d, d}).toRectF();
                const QString where = QStringLiteral("edges=%1 d=%2 r=(%3,%4 %5x%6)")
                                          .arg(edges).arg(d).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
                QVERIFY2(r.width() >= kMinViewSide && r.height() >= kMinViewSide, qPrintable(where));
                QVERIFY2(content.contains(r), qPrintable(where));
                // This synchronous sweep never yields to QML's event loop.
                // Reclaim temporary JS wrappers in bounded batches; retaining
                // the entire sweep makes Qt's debug cleanup quadratic.
                if (++callsSinceCollection == kGarbageCollectionBatchSize) {
                    SnapTray::QmlOverlayManager::instance().engine()->collectGarbage();
                    callsSinceCollection = 0;
                }
            }
        }
    }
    SnapTray::QmlOverlayManager::instance().engine()->collectGarbage();
}

void tst_RecordingCropOverlay::createSweepStaysInside()
{
    const QRectF content(0, 50, 400, 200);
    for (int a = -40; a <= 440; a += 7) {
        for (int b = -40; b <= 440; b += 11) {
            const QRectF r = call("createRect", {a, a / 2 + 20, b, b / 2 + 30}).toRectF();
            const QString where = QStringLiteral("a=%1 b=%2 r=(%3,%4 %5x%6)")
                                      .arg(a).arg(b).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
            QVERIFY2(r.width() >= 0 && r.height() >= 0, qPrintable(where));
            QVERIFY2(r.left() >= content.left() && r.right() <= content.right(), qPrintable(where));
            QVERIFY2(r.top() >= content.top() && r.bottom() <= content.bottom(), qPrintable(where));
        }
    }
    // Both ends near the centre line snap there but never invert.
    const QRectF nearCentre = call("createRect", {198, 100, 202, 120}).toRectF();
    QCOMPARE(nearCentre.x(), 200.0);
    QVERIFY(nearCentre.width() >= 0);
}

// ---------- The draft follows the content (#11) ----------
// init(): 800x400 video in a 400x200 content rect at (0, 50).

void tst_RecordingCropOverlay::draftFollowsContentRect()
{
    call("beginEditing");
    m_overlay->setProperty("draftRect", QRectF(100, 100, 100, 50));

    // Twice the scale and the letterbox gone: the same video pixels, so the
    // draft keeps framing them and editing continues.
    m_overlay->setProperty("contentRect", QRectF(0, 0, 800, 400));
    QVERIFY(m_overlay->property("editing").toBool());
    QCOMPARE(m_overlay->property("draftRect").toRectF(), QRectF(200, 100, 200, 100));

    // No content left to show (window collapsed, frame cleared): nothing to
    // follow, the draft is dropped.
    m_overlay->setProperty("contentRect", QRectF(0, 0, 0, 0));
    QVERIFY(!m_overlay->property("editing").toBool());
    QCOMPARE(m_overlay->property("draftRect").toRectF(), QRectF(0, 0, 0, 0));

    // A different video is a different picture: the draft is dropped too.
    m_overlay->setProperty("contentRect", QRectF(0, 50, 400, 200));
    call("beginEditing");
    m_overlay->setProperty("draftRect", QRectF(100, 100, 100, 50));
    m_overlay->setProperty("videoSize", QSize(400, 200));
    QVERIFY(!m_overlay->property("editing").toBool());
}

// ---------- Edge hit-testing on small selections ----------

void tst_RecordingCropOverlay::edgesAtSmallRectKeepsMoveZone()
{
    // 16x16: narrower than two handle bands, yet its centre must still move it.
    m_overlay->setProperty("draftRect", QRectF(100, 100, 16, 16));
    QCOMPARE(call("edgesAt", {108, 108}).toInt(), 0);
    QCOMPARE(call("edgesAt", {101, 108}).toInt(), kEdgeLeft);
    QCOMPARE(call("edgesAt", {115, 108}).toInt(), kEdgeRight);
    QCOMPARE(call("edgesAt", {108, 101}).toInt(), kEdgeTop);
    QCOMPARE(call("edgesAt", {108, 115}).toInt(), kEdgeBottom);
    // Outside the rect the full handle band applies, one axis at a time:
    // vertically centred to the left is the left edge, not a corner.
    QCOMPARE(call("edgesAt", {95, 108}).toInt(), kEdgeLeft);
    QCOMPARE(call("edgesAt", {95, 95}).toInt(), kEdgeLeft | kEdgeTop);
    QCOMPARE(call("edgesAt", {121, 121}).toInt(), kEdgeRight | kEdgeBottom);
    QCOMPARE(call("edgesAt", {80, 108}).toInt(), 0);
    QCOMPARE(call("cursorFor", {108, 108}).toInt(), static_cast<int>(Qt::SizeAllCursor));

    // Large rects keep the full band inside too.
    m_overlay->setProperty("draftRect", QRectF(100, 100, 100, 100));
    QCOMPARE(call("edgesAt", {108, 150}).toInt(), kEdgeLeft);
    QCOMPARE(call("edgesAt", {150, 150}).toInt(), 0);
}

void tst_RecordingCropOverlay::edgesAtSweepSmallRects()
{
    for (int w = 1; w <= 60; ++w) {
        for (int h = 1; h <= 60; ++h) {
            const QRectF r(100, 100, w, h);
            m_overlay->setProperty("draftRect", r);
            const QString where = QStringLiteral("rect %1x%2").arg(w).arg(h);
            QVERIFY2(call("edgesAt", {r.center().x(), r.center().y()}).toInt() == 0, qPrintable(where));
            QVERIFY2(call("edgesAt", {r.left() - 3, r.center().y()}).toInt() == kEdgeLeft, qPrintable(where));
            QVERIFY2(call("edgesAt", {r.center().x(), r.bottom() + 3}).toInt() == kEdgeBottom, qPrintable(where));
            QVERIFY2(call("edgesAt", {r.right() + 3, r.top() - 3}).toInt() == (kEdgeRight | kEdgeTop),
                     qPrintable(where));
        }
    }
}

// ---------- Draft minimum follows the backend's minimum crop size ----------
// init(): 800x400 video in a 400x200 content rect, so 64 video px = 32 view px.

void tst_RecordingCropOverlay::minViewSizeFollowsBackendMinimum()
{
    QCOMPARE(m_overlay->property("minViewWidth").toReal(), kMinViewSide);
    QCOMPARE(m_overlay->property("minViewHeight").toReal(), kMinViewSide);

    m_overlay->setProperty("minVideoSide", 64);
    QCOMPARE(m_overlay->property("minViewWidth").toReal(), 32.0);
    QCOMPARE(m_overlay->property("minViewHeight").toReal(), 32.0);

    // Never below the pointer-friendly floor, never above the content.
    m_overlay->setProperty("minVideoSide", 4);
    QCOMPARE(m_overlay->property("minViewWidth").toReal(), kMinViewSide);
    m_overlay->setProperty("minVideoSide", 64);
    m_overlay->setProperty("videoSize", QSize(40, 20));
    QCOMPARE(m_overlay->property("minViewWidth").toReal(), 400.0);
    QCOMPARE(m_overlay->property("minViewHeight").toReal(), 200.0);
}

void tst_RecordingCropOverlay::resizeRectKeepsBackendMinimum_data()
{
    QTest::addColumn<QRectF>("start");
    QTest::addColumn<int>("edges");
    QTest::addColumn<int>("dx");
    QTest::addColumn<int>("dy");
    QTest::addColumn<QRectF>("expected");
    // The dragged edge stops at 32 view px; the opposite edge never moves.
    const QRectF start(100, 100, 100, 50);
    QTest::newRow("left") << start << kEdgeLeft << 200 << 0 << QRectF(168, 100, 32, 50);
    QTest::newRow("right") << start << kEdgeRight << -200 << 0 << QRectF(100, 100, 32, 50);
    QTest::newRow("top") << start << kEdgeTop << 0 << 200 << QRectF(100, 118, 100, 32);
    QTest::newRow("bottom") << start << kEdgeBottom << 0 << -200 << QRectF(100, 100, 100, 32);
    QTest::newRow("top-left") << start << (kEdgeLeft | kEdgeTop) << 200 << 200 << QRectF(168, 118, 32, 32);
}

void tst_RecordingCropOverlay::resizeRectKeepsBackendMinimum()
{
    QFETCH(QRectF, start);
    QFETCH(int, edges);
    QFETCH(int, dx);
    QFETCH(int, dy);
    QFETCH(QRectF, expected);
    m_overlay->setProperty("minVideoSide", 64);
    QCOMPARE(call("resizeRect", {start, edges, dx, dy}).toRectF(), expected);
}

void tst_RecordingCropOverlay::createRectKeepsBackendMinimum_data()
{
    QTest::addColumn<QPointF>("press");
    QTest::addColumn<QPointF>("pointer");
    QTest::addColumn<QRectF>("expected");
    // The press point is the anchor; the pointer side grows to the minimum.
    QTest::newRow("down-right") << QPointF(100, 100) << QPointF(104, 104) << QRectF(100, 100, 32, 32);
    QTest::newRow("up-left") << QPointF(100, 100) << QPointF(90, 90) << QRectF(68, 68, 32, 32);
    QTest::newRow("right only") << QPointF(100, 100) << QPointF(160, 100) << QRectF(100, 100, 60, 32);
    // Only when the minimum does not fit does the anchor retreat into the content.
    QTest::newRow("bottom-right corner") << QPointF(398, 248) << QPointF(399, 249) << QRectF(368, 218, 32, 32);
    QTest::newRow("top-left corner") << QPointF(2, 52) << QPointF(1, 51) << QRectF(0, 50, 32, 32);
    // Large drags are unchanged.
    QTest::newRow("large") << QPointF(100, 100) << QPointF(500, 400) << QRectF(100, 100, 300, 150);
}

void tst_RecordingCropOverlay::createRectKeepsBackendMinimum()
{
    QFETCH(QPointF, press);
    QFETCH(QPointF, pointer);
    QFETCH(QRectF, expected);
    m_overlay->setProperty("minVideoSide", 64);
    QCOMPARE(call("createRect", {press.x(), press.y(), pointer.x(), pointer.y()}).toRectF(), expected);
}

void tst_RecordingCropOverlay::createSweepKeepsBackendMinimum()
{
    m_overlay->setProperty("minVideoSide", 64);
    const QRectF content(0, 50, 400, 200);
    for (int a = -40; a <= 440; a += 7) {
        for (int b = -40; b <= 440; b += 11) {
            const QRectF r = call("createRect", {a, a / 2 + 20, b, b / 2 + 30}).toRectF();
            const QString where = QStringLiteral("a=%1 b=%2 r=(%3,%4 %5x%6)")
                                      .arg(a).arg(b).arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
            QVERIFY2(r.width() >= 32.0 && r.height() >= 32.0, qPrintable(where));
            QVERIFY2(content.contains(r), qPrintable(where));
        }
    }
}

// ---------- Full preview ----------

tst_RecordingCropOverlay::PreviewOpen tst_RecordingCropOverlay::openPreview(const QSize& frameSize,
                                                                           QString* reason)
{
    if (QGuiApplication::screens().isEmpty()) {
        *reason = QStringLiteral("RecordingPreview interaction tests need a screen");
        return PreviewOpen::Unavailable;
    }
    m_backend = std::make_unique<StubPreviewBackend>();
    m_view = std::make_unique<QQuickView>(SnapTray::QmlOverlayManager::instance().engine(), nullptr);
    m_backend->longshot()->installImageProvider(m_view->engine());
    m_view->rootContext()->setContextProperty(QStringLiteral("backend"), m_backend.get());
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setMinimumSize(QSize(kPreviewMinWidth, kPreviewMinHeight));
    m_view->resize(kPreviewWidth, kPreviewHeight);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingPreview.qml")));
    if (m_view->status() != QQuickView::Ready || !m_view->rootObject()) {
        QStringList errors;
        for (const auto& error : m_view->errors()) {
            errors << error.toString();
        }
        *reason = QStringLiteral("RecordingPreview.qml failed to load: %1")
                      .arg(errors.isEmpty() ? QStringLiteral("no root object") : errors.join(QLatin1String("; ")));
        return PreviewOpen::Failed;
    }
    m_view->show();
    if (!QTest::qWaitForWindowExposed(m_view.get())) {
        *reason = QStringLiteral("RecordingPreview interaction tests need a real, exposable window");
        return PreviewOpen::Unavailable;
    }
    m_view->requestActivate();
    if (!QTest::qWaitForWindowActive(m_view.get(), 1000)) {
        qDebug() << "Preview window not active; keys go straight to the root item";
    }

    // Stand in for a decoded frame: the item derives contentRect from it, and the
    // preview reports the video size to the backend when the media loads.
    QQuickItem* video = previewItem("previewVideoPlayer");
    if (!video) {
        *reason = QStringLiteral("RecordingPreview has no item named previewVideoPlayer");
        return PreviewOpen::Failed;
    }
    if (video->width() <= 0 || video->height() <= 0) {
        *reason = QStringLiteral("previewVideoPlayer has no area (%1x%2) in an exposed window")
                      .arg(video->width())
                      .arg(video->height());
        return PreviewOpen::Failed;
    }
    for (const char* name : {"previewCropOverlay", "previewCropButton", "previewVideoClickArea"}) {
        if (!m_view->rootObject()->findChild<QObject*>(QString::fromLatin1(name))) {
            *reason = QStringLiteral("RecordingPreview has no object named %1").arg(QString::fromLatin1(name));
            return PreviewOpen::Failed;
        }
    }
    QImage frame(frameSize, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::darkGray);
    if (!QMetaObject::invokeMethod(video, "onFrameReady", Qt::DirectConnection, Q_ARG(QImage, frame))) {
        *reason = QStringLiteral("Cannot inject a frame: previewVideoPlayer.onFrameReady(QImage) is not invokable");
        return PreviewOpen::Failed;
    }
    m_backend->updateVideoSize(frameSize);
    const QRectF content = previewContentRect();
    if (content.isEmpty()) {
        *reason = QStringLiteral("previewVideoPlayer.contentRect is empty after a %1x%2 frame")
                      .arg(frameSize.width())
                      .arg(frameSize.height());
        return PreviewOpen::Failed;
    }
    return PreviewOpen::Ready;
}

QObject* tst_RecordingCropOverlay::previewOverlay() const
{
    return m_view->rootObject()->findChild<QObject*>(QStringLiteral("previewCropOverlay"));
}

QQuickItem* tst_RecordingCropOverlay::previewItem(const char* objectName) const
{
    return m_view->rootObject()->findChild<QQuickItem*>(QString::fromLatin1(objectName));
}

QRectF tst_RecordingCropOverlay::previewContentRect() const
{
    return previewItem("previewVideoPlayer")->property("contentRect").toRectF();
}

QPoint tst_RecordingCropOverlay::scenePoint(QQuickItem* item, const QPointF& local) const
{
    return item->mapToScene(local).toPoint();
}

QPoint tst_RecordingCropOverlay::overlayPoint(qreal x, qreal y) const
{
    return scenePoint(qobject_cast<QQuickItem*>(previewOverlay()), QPointF(x, y));
}

void tst_RecordingCropOverlay::click(QQuickItem* item)
{
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier,
                      scenePoint(item, QPointF(item->width() / 2, item->height() / 2)));
}

void tst_RecordingCropOverlay::drag(const QPoint& from, const QPoint& to)
{
    QTest::mousePress(m_view.get(), Qt::LeftButton, Qt::NoModifier, from);
    for (int i = 1; i <= kDragSteps; ++i) {
        const QPoint step = from + (to - from) * i / kDragSteps;
        QTest::mouseMove(m_view.get(), step);
    }
    QTest::mouseRelease(m_view.get(), Qt::LeftButton, Qt::NoModifier, to);
}

void tst_RecordingCropOverlay::sendKey(Qt::Key key, Qt::KeyboardModifiers modifiers)
{
    if (m_view->activeFocusItem()) {
        QTest::keyClick(m_view.get(), key, modifiers);
        return;
    }
    // Headless sessions may never activate the window; deliver to the focused root item instead.
    QKeyEvent press(QEvent::KeyPress, key, modifiers);
    QCoreApplication::sendEvent(m_view->rootObject(), &press);
    QKeyEvent release(QEvent::KeyRelease, key, modifiers);
    QCoreApplication::sendEvent(m_view->rootObject(), &release);
}

Qt::CursorShape tst_RecordingCropOverlay::cursorAt(const QPoint& scenePos)
{
    // Two hover moves: the first may only make a newly shown MouseArea track the pointer.
    QTest::mouseMove(m_view.get(), scenePos + QPoint(1, 1));
    QTest::mouseMove(m_view.get(), scenePos);
    return m_view->cursor().shape();
}

bool tst_RecordingCropOverlay::editing() const
{
    return previewOverlay()->property("editing").toBool();
}

QRect tst_RecordingCropOverlay::expectedVideoCrop(const QRectF& viewRect) const
{
    return SnapTray::VideoCropGeometry::normalizeCropRect(
        SnapTray::VideoCropGeometry::viewToVideo(viewRect, previewContentRect(), m_backend->videoSize()),
        m_backend->videoSize());
}

// Skips only when the environment cannot show the window; a broken preview
// (load error, missing object, no content area) fails the test.
#define OPEN_PREVIEW_OR_FAIL(frameSize)                                                    \
    do {                                                                                   \
        QString openReason;                                                                \
        const PreviewOpen opened = openPreview(frameSize, &openReason);                    \
        if (opened == PreviewOpen::Unavailable) {                                          \
            QSKIP(qPrintable(openReason));                                                 \
        }                                                                                  \
        if (opened != PreviewOpen::Ready) {                                                \
            QFAIL(qPrintable(openReason));                                                 \
        }                                                                                  \
    } while (false)

void tst_RecordingCropOverlay::previewCreatesFirstSelection_data()
{
    QTest::addColumn<bool>("letterboxed");
    QTest::newRow("fills item") << false;
    QTest::newRow("letterboxed") << true;
}

void tst_RecordingCropOverlay::previewCreatesFirstSelection()
{
    QFETCH(bool, letterboxed);
    // Probe the video area size first so the "fills item" frame matches it exactly.
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    QQuickItem* video = previewItem("previewVideoPlayer");
    const QSize area(qRound(video->width()), qRound(video->height()));
    if (!letterboxed) {
        m_view.reset();
        OPEN_PREVIEW_OR_FAIL(area);
    }
    const QRectF content = previewContentRect();
    if (letterboxed) {
        QVERIFY2(content.height() < video->height(), "frame should be letterboxed");
    } else {
        QCOMPARE(content, QRectF(QPointF(0, 0), QSizeF(area)));
    }

    QObject* overlay = previewOverlay();
    QVERIFY(overlay);
    QVERIFY(!m_backend->hasCrop());
    QVERIFY(previewItem("previewVideoClickArea")->property("enabled").toBool());

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    QCOMPARE(overlay->property("draftRect").toRectF(), QRectF(0, 0, 0, 0));
    QVERIFY(!overlay->property("hasShownRect").toBool()); // no handles, no frame
    QVERIFY(!previewItem("previewVideoClickArea")->property("enabled").toBool());

    if (letterboxed) {
        // A drag starting in the letterbox (outside the video) does not create a selection.
        drag(overlayPoint(content.x() + content.width() * 0.3, content.y() / 2),
             overlayPoint(content.x() + content.width() * 0.6, content.y() + content.height() * 0.4));
        QCOMPARE(overlay->property("draftRect").toRectF(), QRectF(0, 0, 0, 0));
    }

    // Away from snap targets (edges and centre lines).
    const QPointF from(qRound(content.x() + content.width() * 0.25), qRound(content.y() + content.height() * 0.25));
    const QPointF to(qRound(content.x() + content.width() * 0.65), qRound(content.y() + content.height() * 0.70));
    drag(overlayPoint(from.x(), from.y()), overlayPoint(to.x(), to.y()));
    const QRectF draft = overlay->property("draftRect").toRectF();
    QCOMPARE(draft, QRectF(from, to));
    QVERIFY(overlay->property("hasShownRect").toBool());
    QVERIFY(editing());

    // Enter while editing applies only; it does not save.
    sendKey(Qt::Key_Return);
    QVERIFY(!editing());
    QCOMPARE(m_backend->saveCount, 0);
    QVERIFY(m_backend->hasCrop());
    QCOMPARE(m_backend->cropRect(), expectedVideoCrop(draft));
    QVERIFY(previewItem("previewVideoClickArea")->property("enabled").toBool());
}

void tst_RecordingCropOverlay::previewMoveResizeReplace()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    QQuickItem* video = previewItem("previewVideoPlayer");
    const QSize area(qRound(video->width()), qRound(video->height()));
    m_view.reset();
    // A frame that fills the item 1:1, so view and video pixels coincide.
    OPEN_PREVIEW_OR_FAIL(area);
    QCOMPARE(previewContentRect(), QRectF(QPointF(0, 0), QSizeF(area)));

    m_backend->setCropRect(QRect(100, 100, 200, 100));
    QObject* overlay = previewOverlay();
    QCOMPARE(overlay->property("committedRect").toRectF(), QRectF(100, 100, 200, 100));

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    QCOMPARE(overlay->property("draftRect").toRectF(), QRectF(100, 100, 200, 100));

    // Move: drag from inside the selection.
    drag(overlayPoint(200, 150), overlayPoint(230, 170));
    QTRY_COMPARE(overlay->property("draftRect").toRectF(), QRectF(130, 120, 200, 100));

    // Resize away from the centre snap line (the format row reduces the video area).
    drag(overlayPoint(330, 220), overlayPoint(350, 260));
    QTRY_COMPARE(overlay->property("draftRect").toRectF(), QRectF(130, 120, 220, 140));

    // Resize: drag the left-edge handle.
    drag(overlayPoint(130, 190), overlayPoint(110, 190));
    QTRY_COMPARE(overlay->property("draftRect").toRectF(), QRectF(110, 120, 240, 140));

    // Replace: drag outside the selection.
    const QPoint start = overlayPoint(area.width() * 0.6, area.height() * 0.6);
    const QPoint end = overlayPoint(area.width() * 0.6 + 100, area.height() * 0.6 + 80);
    drag(start, end);
    QTRY_VERIFY_WITH_TIMEOUT(overlay->property("draftRect").toRectF() != QRectF(110, 120, 240, 140), 2000);
    const QRectF replaced = overlay->property("draftRect").toRectF();
    QCOMPARE(replaced, QRectF(QPointF(start), QPointF(end)));

    // A plain click outside the selection keeps it (no sliver replacement).
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier, overlayPoint(50, 50));
    QCOMPARE(overlay->property("draftRect").toRectF(), replaced);

    sendKey(Qt::Key_Enter);
    QVERIFY(!editing());
    QCOMPARE(m_backend->saveCount, 0);
    QCOMPARE(m_backend->cropRect(), expectedVideoCrop(replaced));
}

void tst_RecordingCropOverlay::previewEscapeRestoresCommitted()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    const QRect committed(400, 100, 400, 200);
    m_backend->setCropRect(committed);
    QObject* overlay = previewOverlay();
    const QRectF committedView = overlay->property("committedRect").toRectF();
    QVERIFY(!committedView.isEmpty());

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    drag(overlayPoint(committedView.center().x(), committedView.center().y()),
         overlayPoint(committedView.center().x() + 40, committedView.center().y() + 10));
    QTRY_VERIFY_WITH_TIMEOUT(overlay->property("draftRect").toRectF() != committedView, 2000);
    QVERIFY(overlay->property("draftRect").toRectF() != committedView);

    sendKey(Qt::Key_Escape);
    QVERIFY(!editing());
    QCOMPARE(m_backend->discardCount, 0); // the preview stays open
    QCOMPARE(m_backend->cropRect(), committed);
    QCOMPARE(overlay->property("shownRect").toRectF(), committedView);
    QCOMPARE(m_backend->setCropFromViewCount, 0);

    // Without a committed crop, Escape drops a drawn draft and leaves no crop.
    m_backend->clearCrop();
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    drag(overlayPoint(content.x() + 100, content.y() + 30), overlayPoint(content.x() + 250, content.y() + 90));
    QVERIFY(overlay->property("hasShownRect").toBool());
    sendKey(Qt::Key_Escape);
    QVERIFY(!editing());
    QVERIFY(!m_backend->hasCrop());
    QVERIFY(!overlay->property("hasShownRect").toBool());
    QCOMPARE(m_backend->discardCount, 0);

    // Outside editing, Escape keeps its existing meaning.
    sendKey(Qt::Key_Escape);
    QCOMPARE(m_backend->discardCount, 1);
}

void tst_RecordingCropOverlay::previewEmptyDraftApplyIsNoop()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    QQuickItem* cropButton = previewItem("previewCropButton");

    // Toolbar apply of an empty draft.
    click(cropButton);
    QVERIFY(editing());
    click(cropButton);
    QVERIFY(!editing());
    QVERIFY(!m_backend->hasCrop());
    QCOMPARE(m_backend->setCropFromViewCount, 0);

    // Enter apply of an empty draft.
    click(cropButton);
    QVERIFY(editing());
    sendKey(Qt::Key_Return);
    QVERIFY(!editing());
    QVERIFY(!m_backend->hasCrop());
    QCOMPARE(m_backend->setCropFromViewCount, 0);
    QCOMPARE(m_backend->saveCount, 0);
}

void tst_RecordingCropOverlay::previewSaveButtonExportsDraft()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    drag(overlayPoint(content.x() + 100, content.y() + 30), overlayPoint(content.x() + 300, content.y() + 130));
    QTRY_VERIFY_WITH_TIMEOUT(!previewOverlay()->property("draftRect").toRectF().isEmpty(), 2000);
    const QRectF draft = previewOverlay()->property("draftRect").toRectF();
    QVERIFY(!draft.isEmpty());
    QVERIFY(!m_backend->hasCrop());

    click(previewItem("previewSaveButton"));
    QCOMPARE(m_backend->saveCount, 1);
    QCOMPARE(m_backend->cropAtSave, expectedVideoCrop(draft));
    QVERIFY(!m_backend->cropAtSave.isEmpty());
    QVERIFY(!editing());
}

void tst_RecordingCropOverlay::previewCtrlSExportsDraft()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    drag(overlayPoint(content.x() + 120, content.y() + 40), overlayPoint(content.x() + 320, content.y() + 140));
    QTRY_VERIFY_WITH_TIMEOUT(!previewOverlay()->property("draftRect").toRectF().isEmpty(), 2000);
    const QRectF draft = previewOverlay()->property("draftRect").toRectF();
    QVERIFY(!draft.isEmpty());

    sendKey(Qt::Key_S, Qt::ControlModifier);
    QCOMPARE(m_backend->saveCount, 1);
    QCOMPARE(m_backend->cropAtSave, expectedVideoCrop(draft));
    QVERIFY(!editing());
}

void tst_RecordingCropOverlay::previewEnterSavesWhenNotEditing()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setCropRect(QRect(200, 100, 400, 200));
    sendKey(Qt::Key_Return);
    QCOMPARE(m_backend->saveCount, 1);
    QCOMPARE(m_backend->cropAtSave, QRect(200, 100, 400, 200));
    sendKey(Qt::Key_Enter);
    QCOMPARE(m_backend->saveCount, 2);
}

void tst_RecordingCropOverlay::previewProcessingBlocksSave()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    drag(overlayPoint(content.x() + 100, content.y() + 30), overlayPoint(content.x() + 300, content.y() + 130));
    QTRY_VERIFY_WITH_TIMEOUT(!previewOverlay()->property("draftRect").toRectF().isEmpty(), 2000);
    const QRectF draft = previewOverlay()->property("draftRect").toRectF();
    QVERIFY(!draft.isEmpty());

    m_backend->setProcessing(true);
    for (auto* item : m_view->rootObject()->findChildren<QQuickItem*>()) {
        if ((item->property("text").toString() == "GIF" || item->property("text").toString() == "WebP")
            && item->property("selected").isValid()) {
            click(item);
            QCOMPARE(m_backend->selectedFormat(), 0);
            QVERIFY(previewItem("previewCancelExportButton")->isVisible());
        }
    }
    click(previewItem("previewSaveButton"));
    sendKey(Qt::Key_S, Qt::ControlModifier);
    sendKey(Qt::Key_Return);
    sendKey(Qt::Key_Enter);
    invokeQml(m_view->rootObject(), "saveWithCrop");
    QCOMPARE(m_backend->saveCount, 0);
    QCOMPARE(m_backend->setCropFromViewCount, 0);
    QVERIFY(!m_backend->hasCrop());

    // Pointer edits are blocked too, and the crop controls are disabled.
    drag(overlayPoint(draft.center().x(), draft.center().y()),
         overlayPoint(draft.center().x() + 30, draft.center().y() + 10));
    QCOMPARE(previewOverlay()->property("draftRect").toRectF(), draft);
    QVERIFY(!previewItem("previewCropButton")->isEnabled());

    m_backend->setProcessing(false);
    click(previewItem("previewSaveButton"));
    QCOMPARE(m_backend->saveCount, 1);
    QCOMPARE(m_backend->cropAtSave, expectedVideoCrop(draft));
}

void tst_RecordingCropOverlay::previewCancellationFollowsActiveJob()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    auto* cancel = previewItem("previewCancelExportButton");
    QVERIFY(cancel);
    for (int format : {0, 1, 2}) {
        m_backend->setSelectedFormat(format);
        m_backend->setProcessing(true);
        QCOMPARE(cancel->isVisible(), format == 0);
        // Even an external property change must not alter the running job's capability.
        m_backend->setSelectedFormat(format == 0 ? 1 : 0);
        QCOMPARE(cancel->isVisible(), format == 0);
        if (format == 0) {
            // The newly visible Column must finish positioning its children
            // before we calculate a real mouse-click coordinate.
            QSignalSpy rendered(m_view.get(), &QQuickWindow::afterAnimating);
            m_view->requestUpdate();
            QVERIFY(rendered.wait(2000));
            click(cancel);
            QCOMPARE(m_backend->cancelCount, 1);
            QVERIFY(!cancel->isVisible());
        }
        m_backend->setProcessing(false);
        click(previewItem("previewSaveButton"));
        QCOMPARE(m_backend->saveCount, format + 1);
    }
}

void tst_RecordingCropOverlay::previewGeometryChangeKeepsDraft()
{
    const QSize frameSize(1600, 400);
    OPEN_PREVIEW_OR_FAIL(frameSize);
    const QRect committed(400, 100, 400, 200);
    m_backend->setCropRect(committed);
    QObject* overlay = previewOverlay();
    const QRectF oldContent = previewContentRect();

    click(previewItem("previewCropButton"));
    const QRectF committedView = overlay->property("committedRect").toRectF();
    drag(overlayPoint(committedView.center().x(), committedView.center().y()),
         overlayPoint(committedView.center().x() + 40, committedView.center().y() + 10));
    QTRY_VERIFY_WITH_TIMEOUT(overlay->property("draftRect").toRectF() != committedView, 2000);
    QVERIFY(editing());
    const QRectF draftBefore = overlay->property("draftRect").toRectF();
    const QRect videoCropBefore = SnapTray::VideoCropGeometry::normalizeCropRect(
        SnapTray::VideoCropGeometry::viewToVideo(draftBefore, oldContent, frameSize), frameSize);
    QVERIFY(videoCropBefore != committed);

    // Resizing the window re-fits the video; the draft moves with it and the
    // user keeps editing. Nothing is committed by the resize itself.
    m_view->resize(kPreviewWidth + 100, kPreviewHeight + 60);
    QTRY_VERIFY(previewContentRect() != oldContent);
    QVERIFY(editing());
    QCOMPARE(m_backend->cropRect(), committed);
    QCOMPARE(m_backend->setCropFromViewCount, 0);
    QCOMPARE(overlay->property("committedRect").toRectF(), m_backend->cropRectInView(previewContentRect()));

    // Applying now commits the same video pixels the draft framed before.
    sendKey(Qt::Key_Return);
    QVERIFY(!editing());
    const QRect videoCropAfter = m_backend->cropRect();
    const QString where = QStringLiteral("before (%1,%2 %3x%4) after (%5,%6 %7x%8)")
                              .arg(videoCropBefore.x()).arg(videoCropBefore.y())
                              .arg(videoCropBefore.width()).arg(videoCropBefore.height())
                              .arg(videoCropAfter.x()).arg(videoCropAfter.y())
                              .arg(videoCropAfter.width()).arg(videoCropAfter.height());
    QVERIFY2(qAbs(videoCropAfter.left() - videoCropBefore.left()) <= 2
                 && qAbs(videoCropAfter.top() - videoCropBefore.top()) <= 2
                 && qAbs(videoCropAfter.right() - videoCropBefore.right()) <= 2
                 && qAbs(videoCropAfter.bottom() - videoCropBefore.bottom()) <= 2,
             qPrintable(where));
}

void tst_RecordingCropOverlay::previewSizeChipAndClear()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    QQuickItem* chip = previewItem("previewCropSizeLabel");
    QQuickItem* clear = previewItem("previewClearCropButton");
    QVERIFY(chip && clear);
    QVERIFY(!chip->isVisible());
    QVERIFY(!clear->isVisible());

    // An odd request is normalized; the chip shows the backend's value.
    m_backend->setCropRect(QRect(401, 101, 401, 201));
    QCOMPARE(m_backend->cropRect(), QRect(400, 100, 402, 202));
    QVERIFY(chip->isVisible());
    QCOMPARE(chip->property("text").toString(), QStringLiteral("402 × 202"));
    QVERIFY(clear->isVisible());

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    QVERIFY(!chip->isVisible());
    QVERIFY(!clear->isVisible());
    sendKey(Qt::Key_Escape);
    QVERIFY(chip->isVisible());

    click(clear);
    QVERIFY(!m_backend->hasCrop());
    QVERIFY(!chip->isVisible());
    QVERIFY(!clear->isVisible());
}

void tst_RecordingCropOverlay::previewToolbarFitsAtMinimumWidth()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    // Widest realistic chip text for this frame.
    m_backend->setCropRect(QRect(2, 2, 1596, 396));
    QVERIFY(m_backend->hasCrop());
    m_view->resize(kPreviewMinWidth, kPreviewMinHeight);
    QTRY_COMPARE(qRound(m_view->rootObject()->width()), kPreviewMinWidth);
    QQuickItem* save = previewItem("previewSaveButton");
    QQuickItem* clear = previewItem("previewClearCropButton");
    QVERIFY(save->isVisible() && clear->isVisible());
    const auto rightEdge = [](QQuickItem* item) {
        return item->mapRectToScene(QRectF(0, 0, item->width(), item->height())).right();
    };
    QTest::qWait(100); // let the RowLayout polish after the resize
    QVERIFY2(rightEdge(save) <= m_view->rootObject()->width(),
             qPrintable(QStringLiteral("save button right edge %1 > %2")
                            .arg(rightEdge(save)).arg(m_view->rootObject()->width())));
}

void tst_RecordingCropOverlay::previewLongshotResultKeepsRecording()
{
    OPEN_PREVIEW_OR_FAIL(QSize(320, 480));
    m_view->resize(kPreviewMinWidth, kPreviewMinHeight);
    QTest::qWait(100); // apply the resized toolbar geometry before delivering input
    click(previewItem("longshotFormatButton"));
    QCOMPARE(m_backend->selectedFormat(), 3);
    auto* controller = m_backend->longshot();
    QSignalSpy pinned(controller, &LongshotController::pinRequested);
    controller->start("fixture", 0, -1, {});
    QTRY_VERIFY_WITH_TIMEOUT(controller->hasResult(), 10000);
    QTRY_VERIFY(previewItem("longshotResult")->isVisible());
    auto* pin = previewItem("longshotPin");
    QTRY_VERIFY(pin->isVisible());
    QVERIFY(pin->mapRectToScene(QRectF(0, 0, pin->width(), pin->height())).right() <= kPreviewMinWidth);
    click(pin);
    QCOMPARE(pinned.count(), 1);
    QCOMPARE(m_backend->saveCount, 0);
    QCOMPARE(m_backend->discardCount, 0);
    if (qEnvironmentVariableIsSet("SNAPTRAY_LONGSHOT_PREVIEW_ARTIFACT")) {
        QTest::qWait(150);
        QVERIFY(m_view->grabWindow().save(qEnvironmentVariable("SNAPTRAY_LONGSHOT_PREVIEW_ARTIFACT")));
    }
    sendKey(Qt::Key_Escape);
    QVERIFY(!previewItem("longshotResult")->isVisible());
    QCOMPARE(m_backend->discardCount, 0);
    QVERIFY(controller->hasResult());
}

void tst_RecordingCropOverlay::previewLongshotImageEdits()
{
    OPEN_PREVIEW_OR_FAIL(QSize(320,480));
    m_view->resize(kPreviewMinWidth,kPreviewMinHeight);
    QTest::qWait(100);
    auto* controller=m_backend->longshot();
    QSignalSpy pinned(controller,&LongshotController::pinRequested);
    QSignalSpy annotated(controller,&LongshotController::annotateRequested);
    m_backend->setSelectedFormat(3);
    controller->start("editing-fixture",0,-1,{});
    QTRY_VERIFY_WITH_TIMEOUT(controller->hasResult(),10000);
    QTRY_VERIFY(previewItem("longshotResult")->isVisible());
    controller->pin();
    const auto original=qvariant_cast<QImage>(pinned[0][0]);
    click(previewItem("longshotEdit"));
    QTRY_VERIFY(previewItem("longshotRowSelector")->isVisible());
    QTest::qWait(100); // let the edit tools and viewport settle before mapping input
    auto* selector=previewItem("longshotRowSelector");
    QVERIFY(selector->height()>50);
    drag(scenePoint(selector,QPointF(20,10)),scenePoint(selector,QPointF(20,50)));
    const int first=m_view->rootObject()->property("firstSelectedRow").toInt();
    const int end=m_view->rootObject()->property("endSelectedRow").toInt();
    QVERIFY(end>first);
    click(previewItem("longshotKeepRows"));
    QCOMPARE(controller->imageSize().height(),end-first);
    controller->pin();
    QCOMPARE(qvariant_cast<QImage>(pinned.last()[0]),original.copy(0,first,original.width(),end-first));
    QTRY_VERIFY(previewItem("longshotViewport")->property("contentHeight").toReal() < selector->height());
    QTest::mouseClick(m_view.get(),Qt::LeftButton,Qt::NoModifier,scenePoint(selector,QPointF(20,selector->height()-1)));
    QVERIFY(!m_view->rootObject()->property("hasRowSelection").toBool());
    click(previewItem("longshotUndo"));
    QCOMPARE(controller->imageSize(),original.size());
    QTest::qWait(100);
    drag(scenePoint(selector,QPointF(20,10)),scenePoint(selector,QPointF(20,50)));
    const int removed=m_view->rootObject()->property("endSelectedRow").toInt()-m_view->rootObject()->property("firstSelectedRow").toInt();
    click(previewItem("longshotDeleteRows"));
    QCOMPARE(controller->imageSize().height(),original.height()-removed);
    click(previewItem("longshotAnnotate")); QCOMPARE(annotated.count(),1);
    QCOMPARE(qvariant_cast<QImage>(annotated[0][0]).size(),controller->imageSize());
    QCOMPARE(m_backend->discardCount,0);
    const QSize editedSize=controller->imageSize();
    sendKey(Qt::Key_Escape); // leave row-selection mode
    sendKey(Qt::Key_Escape); // return to recording
    QVERIFY(!previewItem("longshotResult")->isVisible());
    click(previewItem("longshotViewResult"));
    QVERIFY(previewItem("longshotResult")->isVisible());
    QCOMPARE(controller->imageSize(),editedSize); QVERIFY(controller->canUndo());
    click(previewItem("longshotEdit"));
    m_view->rootObject()->setProperty("rowAnchor",0);
    m_view->rootObject()->setProperty("rowEnd",controller->imageSize().height()-1);
    QVERIFY(!previewItem("longshotDeleteRows")->isEnabled());
    if (qEnvironmentVariableIsSet("SNAPTRAY_LONGSHOT_EDIT_ARTIFACT")) {
        QTest::qWait(150);
        QVERIFY(m_view->grabWindow().save(qEnvironmentVariable("SNAPTRAY_LONGSHOT_EDIT_ARTIFACT")));
    }
}

void tst_RecordingCropOverlay::previewCursorOverVideo()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    const QRectF content = previewContentRect();
    const QPoint inVideo = overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4);

    // Not editing: the click-to-play hand, with or without a committed crop.
    QTRY_COMPARE(cursorAt(inVideo), Qt::PointingHandCursor);
    m_backend->setCropRect(QRect(400, 100, 400, 200));
    QTRY_COMPARE(cursorAt(inVideo), Qt::PointingHandCursor);
    QQuickItem* chipLabel = previewItem("previewCropSizeLabel");
    QVERIFY(chipLabel->isVisible());
    QTRY_COMPARE(cursorAt(scenePoint(chipLabel, QPointF(chipLabel->width() / 2, chipLabel->height() / 2))),
                 Qt::PointingHandCursor);
    m_backend->clearCrop();

    // Editing: the crop editor owns the cursor.
    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    QTRY_COMPARE(cursorAt(inVideo), Qt::CrossCursor);
    drag(overlayPoint(content.x() + 100, content.y() + 30), overlayPoint(content.x() + 300, content.y() + 130));
    QTRY_COMPARE(cursorAt(overlayPoint(content.x() + 200, content.y() + 80)), Qt::SizeAllCursor);

    // Back out of editing: the hand returns.
    sendKey(Qt::Key_Escape);
    QVERIFY(!editing());
    QTRY_COMPARE(cursorAt(inVideo), Qt::PointingHandCursor);
}

// A cleared frame (stop, or a new source) takes the crop editor with it: the
// overlay is placed by contentRect, which must be empty again once no frame
// is shown, whatever the backend still reports as the video size.
void tst_RecordingCropOverlay::previewCropUnavailableAfterFrameCleared()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    QQuickItem* cropButton = previewItem("previewCropButton");
    QQuickItem* overlay = previewItem("previewCropOverlay");
    QVERIFY(cropButton && overlay);
    QVERIFY(cropButton->isEnabled());
    QVERIFY(overlay->isVisible());

    QQuickItem* video = previewItem("previewVideoPlayer");
    QVERIFY(QMetaObject::invokeMethod(video, "stop", Qt::DirectConnection));
    QTRY_VERIFY(!cropButton->isEnabled());
    QVERIFY(!overlay->isVisible());
    QVERIFY(previewContentRect().isEmpty());
}

void tst_RecordingCropOverlay::previewReleaseCommitsFinalPosition()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setCropRect(QRect(200, 100, 200, 100));
    click(previewItem("previewCropButton"));
    const QRectF initial = previewOverlay()->property("draftRect").toRectF();
    const QPoint from = overlayPoint(initial.center().x(), initial.center().y());
    // A final release carries meaningful position even when no move is delivered.
    QTest::mousePress(m_view.get(), Qt::LeftButton, Qt::NoModifier, from);
    QTest::mouseRelease(m_view.get(), Qt::LeftButton, Qt::NoModifier, from + QPoint(20, 10));
    QTRY_COMPARE(previewOverlay()->property("draftRect").toRectF(), initial.translated(20, 10));
}

void tst_RecordingCropOverlay::previewSmallSelectionMoves()
{
    // A 4000px-wide video shown 800px wide: the minimum crop is 12.8 view px.
    OPEN_PREVIEW_OR_FAIL(QSize(4000, 1000));
    m_backend->setCropRect(QRect(1000, 200, 64, 64));
    QObject* overlay = previewOverlay();
    const QRectF committedView = overlay->property("committedRect").toRectF();
    QVERIFY(committedView.width() < 20.0);

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    const QPoint centre = overlayPoint(committedView.center().x(), committedView.center().y());
    QTRY_COMPARE(cursorAt(centre), Qt::SizeAllCursor);

    drag(centre, centre + QPoint(20, 10));
    QTRY_VERIFY_WITH_TIMEOUT(overlay->property("draftRect").toRectF() != committedView, 2000);
    const QRectF moved = overlay->property("draftRect").toRectF();
    const QRectF expected = committedView.translated(20, 10);
    QVERIFY2(qAbs(moved.x() - expected.x()) < 0.01 && qAbs(moved.y() - expected.y()) < 0.01
                 && qAbs(moved.width() - expected.width()) < 0.01
                 && qAbs(moved.height() - expected.height()) < 0.01,
             qPrintable(QStringLiteral("draft (%1,%2 %3x%4) expected (%5,%6 %7x%8)")
                            .arg(moved.x()).arg(moved.y()).arg(moved.width()).arg(moved.height())
                            .arg(expected.x()).arg(expected.y()).arg(expected.width()).arg(expected.height())));
}

void tst_RecordingCropOverlay::previewTinyDraftAppliesWithoutJump()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1920, 1080));
    QObject* overlay = previewOverlay();
    const QRectF content = previewContentRect();
    const qreal minView = SnapTray::VideoCropGeometry::kMinCropSide * content.width() / 1920.0;
    QCOMPARE(overlay->property("minViewWidth").toReal(), minView);

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    // A tiny drag draws a draft that is already as large as the backend will make it.
    drag(overlayPoint(content.x() + 100, content.y() + 100), overlayPoint(content.x() + 104, content.y() + 104));
    QTRY_VERIFY_WITH_TIMEOUT(!overlay->property("draftRect").toRectF().isEmpty(), 2000);
    const QRectF draft = overlay->property("draftRect").toRectF();
    QVERIFY(!draft.isEmpty());
    QVERIFY2(draft.width() >= minView - 0.01 && draft.height() >= minView - 0.01,
             qPrintable(QStringLiteral("draft %1x%2 < %3").arg(draft.width()).arg(draft.height()).arg(minView)));

    sendKey(Qt::Key_Return);
    QVERIFY(!editing());
    QCOMPARE(m_backend->cropRect(), expectedVideoCrop(draft));
    QCOMPARE(m_backend->cropRect().size(), QSize(64, 64));
    QVERIFY(!QTest::currentTestFailed());

    // The committed rect redraws where the draft was: no visible jump.
    const QRectF committed = overlay->property("committedRect").toRectF();
    const QString where = QStringLiteral("draft (%1,%2 %3x%4) committed (%5,%6 %7x%8)")
                              .arg(draft.x()).arg(draft.y()).arg(draft.width()).arg(draft.height())
                              .arg(committed.x()).arg(committed.y()).arg(committed.width()).arg(committed.height());
    QVERIFY2(qAbs(committed.left() - draft.left()) <= 1.0 && qAbs(committed.top() - draft.top()) <= 1.0
                 && qAbs(committed.right() - draft.right()) <= 1.0
                 && qAbs(committed.bottom() - draft.bottom()) <= 1.0,
             qPrintable(where));
}

void tst_RecordingCropOverlay::snapToRectClampsAndGrows_data()
{
    QTest::addColumn<QRectF>("input");
    QTest::addColumn<QRectF>("expected");
    QTest::newRow("inside") << QRectF(100, 100, 100, 50) << QRectF(100, 100, 100, 50);
    QTest::newRow("clamped to content") << QRectF(-20, 40, 100, 50) << QRectF(0, 50, 80, 40);
    QTest::newRow("grows around centre") << QRectF(200, 150, 2, 2) << QRectF(197, 147, 8, 8);
    QTest::newRow("grows inside the corner") << QRectF(398, 248, 2, 2) << QRectF(392, 242, 8, 8);
}

void tst_RecordingCropOverlay::snapToRectClampsAndGrows()
{
    QFETCH(QRectF, input);
    QFETCH(QRectF, expected);
    QCOMPARE(call("snapToRect", {input}).toRectF(), expected);
}

void tst_RecordingCropOverlay::previewHoverHighlightsWindow()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    QQuickItem* frame = previewItem("cropHoverFrame");
    QQuickItem* label = previewItem("cropHoverLabel");
    QVERIFY(frame && label);
    QVERIFY(!frame->isVisible()); // not editing

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    QVERIFY(previewItem("previewWindowSnapHint")->isVisible());
    const QRectF content = previewContentRect();
    const QRectF expected = m_backend->windowRectInViewAt(
        QPointF(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4), content, 0);
    QVERIFY(!expected.isEmpty());

    QTest::mouseMove(m_view.get(), overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4));
    QTRY_VERIFY(frame->isVisible());
    QObject* overlay = previewOverlay();
    QCOMPARE(overlay->property("hoverRect").toRectF(), expected);
    QCOMPARE(overlay->property("hoverLabel").toString(), QStringLiteral("Code"));
    QVERIFY(label->isVisible());
    // The lookup uses the player's playhead.
    QCOMPARE(m_backend->lastLookupPositionMs, previewItem("previewVideoPlayer")->property("position").toLongLong());

    // Off the window: highlight gone.
    QTest::mouseMove(m_view.get(), overlayPoint(content.x() + 5, content.y() + 5));
    QTRY_VERIFY(!frame->isVisible());
    QCOMPARE(overlay->property("hoverRect").toRectF(), QRectF());
}

void tst_RecordingCropOverlay::previewClickSnapsToWindow()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    const QPoint inside = overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4);
    const QRectF expected = m_backend->windowRectInViewAt(
        QPointF(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4), content, 0);

    QTest::mouseMove(m_view.get(), inside);
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier, inside);
    QObject* overlay = previewOverlay();
    QTRY_COMPARE(overlay->property("draftRect").toRectF(), expected);
    QVERIFY(editing());
    QVERIFY(!previewItem("previewWindowSnapHint")->isVisible()); // a draft exists now

    // Enter commits the snapped window as the crop (through the usual view-to-video
    // rounding and even alignment, like any other draft).
    sendKey(Qt::Key_Return);
    QVERIFY(!editing());
    QCOMPARE(m_backend->cropRect(), expectedVideoCrop(expected));
}

void tst_RecordingCropOverlay::previewDragStillDrawsFreeRect()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    // Start inside the window, drag well outside it: a free rect, not the window.
    drag(overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4),
         overlayPoint(content.x() + content.width() * 0.9, content.y() + content.height() * 0.9));
    QObject* overlay = previewOverlay();
    QTRY_VERIFY_WITH_TIMEOUT(!overlay->property("draftRect").toRectF().isEmpty(), 2000);
    const QRectF draft = overlay->property("draftRect").toRectF();
    QVERIFY(draft.width() > content.width() * 0.5);
    QVERIFY(!previewItem("cropHoverFrame")->isVisible());
}

void tst_RecordingCropOverlay::previewNoTimelineNoHoverNoHint()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    click(previewItem("previewCropButton"));
    QVERIFY(!previewItem("previewWindowSnapHint")->isVisible());
    const QRectF content = previewContentRect();
    const QPoint inside = overlayPoint(content.x() + 100, content.y() + 100);
    QTest::mouseMove(m_view.get(), inside);
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier, inside);
    QTest::qWait(50);
    QVERIFY(!previewItem("cropHoverFrame")->isVisible());
    QCOMPARE(previewOverlay()->property("draftRect").toRectF(), QRectF(0, 0, 0, 0)); // a click alone draws nothing
    QCOMPARE(m_backend->lookupCount, 0);
}

void tst_RecordingCropOverlay::previewDragAndReturnDoesNotSnap()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    const QPoint start = overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4);
    const QRectF window = m_backend->windowRectInViewAt(
        QPointF(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4), content, 0);
    QTest::mouseMove(m_view.get(), start);
    QTest::mousePress(m_view.get(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(m_view.get(), start + QPoint(30, 20));
    QTest::mouseMove(m_view.get(), start + QPoint(1, 1));
    QTest::mouseRelease(m_view.get(), Qt::LeftButton, Qt::NoModifier, start + QPoint(1, 1));
    QVERIFY(previewOverlay()->property("draftRect").toRectF() != window);
}

void tst_RecordingCropOverlay::previewClickInsideDraftKeepsDraft()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    // A draft well inside the stub window (x 25%-50%, y 25%-75% of the content).
    drag(overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.35),
         overlayPoint(content.x() + content.width() * 0.45, content.y() + content.height() * 0.65));
    QObject* overlay = previewOverlay();
    QTRY_VERIFY_WITH_TIMEOUT(!overlay->property("draftRect").toRectF().isEmpty(), 2000);
    const QRectF draft = overlay->property("draftRect").toRectF();
    QVERIFY(!draft.isEmpty());
    const QPoint inside = overlayPoint(draft.center().x(), draft.center().y());
    QTest::mouseMove(m_view.get(), inside);
    QTest::mouseClick(m_view.get(), Qt::LeftButton, Qt::NoModifier, inside);
    QTest::qWait(50);
    QCOMPARE(overlay->property("draftRect").toRectF(), draft);
}

void tst_RecordingCropOverlay::previewNoHighlightOverExistingDraft()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    // A draft well inside the stub window (x 30%-45%, y 35%-65% of the content).
    drag(overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.35),
         overlayPoint(content.x() + content.width() * 0.45, content.y() + content.height() * 0.65));
    QObject* overlay = previewOverlay();
    QTRY_VERIFY_WITH_TIMEOUT(!overlay->property("draftRect").toRectF().isEmpty(), 2000);
    const QRectF draft = overlay->property("draftRect").toRectF();
    QVERIFY(!draft.isEmpty());
    QQuickItem* frame = previewItem("cropHoverFrame");

    // Over the window but outside the draft: a click would snap, so the highlight shows.
    QTest::mouseMove(m_view.get(), overlayPoint(content.x() + content.width() * 0.49, content.y() + content.height() * 0.5));
    QTRY_VERIFY(overlay->property("showsHover").toBool());
    QVERIFY(frame->isVisible());

    // Inside the draft: the window is still found, but a click is a no-op, so no highlight.
    QTest::mouseMove(m_view.get(), overlayPoint(draft.center().x(), draft.center().y()));
    // Wait for the complete hover state, not the transient cleared state while
    // native enter/move events are being delivered.
    QTRY_VERIFY(!overlay->property("showsHover").toBool()
                && !overlay->property("hoverRect").toRectF().isEmpty());
    QVERIFY(!frame->isVisible());

    // On a handle (the draft's right edge): same.
    QTest::mouseMove(m_view.get(), overlayPoint(draft.right(), draft.center().y()));
    QTRY_VERIFY(!overlay->property("showsHover").toBool());
    QVERIFY(!frame->isVisible());
}

void tst_RecordingCropOverlay::previewEndEditingClearsHover()
{
    OPEN_PREVIEW_OR_FAIL(QSize(1600, 400));
    m_backend->setStubWindow(QRect(400, 100, 400, 200), QStringLiteral("Code"));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    QTest::mouseMove(m_view.get(), overlayPoint(content.x() + content.width() * 0.3, content.y() + content.height() * 0.4));
    QObject* overlay = previewOverlay();
    QTRY_VERIFY(!overlay->property("hoverRect").toRectF().isEmpty());

    sendKey(Qt::Key_Escape);
    QVERIFY(!editing());
    QCOMPARE(overlay->property("hoverRect").toRectF(), QRectF());
    QVERIFY(!overlay->property("hovering").toBool());
    const int lookups = m_backend->lookupCount;
    QVERIFY(QMetaObject::invokeMethod(m_view->rootObject(), "refreshWindowHover"));
    QCOMPARE(m_backend->lookupCount, lookups);
    QCOMPARE(overlay->property("hoverRect").toRectF(), QRectF());
}

QTEST_MAIN(tst_RecordingCropOverlay)
#include "tst_RecordingCropOverlay.moc"
