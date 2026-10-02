#include <QtTest/QtTest>

#include "qml/QmlOverlayManager.h"
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
    Q_PROPERTY(QString videoPath READ videoPath CONSTANT)
    Q_PROPERTY(qint64 trimStart READ trimStart WRITE setTrimStart NOTIFY trimRangeChanged)
    Q_PROPERTY(qint64 trimEnd READ trimEnd WRITE setTrimEnd NOTIFY trimRangeChanged)
    Q_PROPERTY(bool hasTrim READ hasTrim NOTIFY trimRangeChanged)
    Q_PROPERTY(QRect cropRect READ cropRect NOTIFY cropRectChanged)
    Q_PROPERTY(bool hasCrop READ hasCrop NOTIFY cropRectChanged)
    Q_PROPERTY(QSize videoSize READ videoSize NOTIFY videoSizeChanged)
    Q_PROPERTY(int selectedFormat READ selectedFormat WRITE setSelectedFormat NOTIFY formatChanged)
    Q_PROPERTY(bool isProcessing READ isProcessing WRITE setProcessing NOTIFY processingChanged)
    Q_PROPERTY(int processProgress READ processProgress CONSTANT)
    Q_PROPERTY(QString processStatus READ processStatus CONSTANT)
    Q_PROPERTY(QString errorMessage READ errorMessage CONSTANT)

public:
    QString videoPath() const { return {}; }
    qint64 trimStart() const { return 0; }
    qint64 trimEnd() const { return 0; }
    void setTrimStart(qint64) {}
    void setTrimEnd(qint64) {}
    bool hasTrim() const { return false; }
    QRect cropRect() const { return m_cropRect; }
    bool hasCrop() const { return !m_cropRect.isEmpty(); }
    QSize videoSize() const { return m_videoSize; }
    int selectedFormat() const { return m_selectedFormat; }
    void setSelectedFormat(int format)
    {
        m_selectedFormat = format;
        emit formatChanged();
    }
    bool isProcessing() const { return m_isProcessing; }
    void setProcessing(bool processing)
    {
        m_isProcessing = processing;
        emit processingChanged();
    }
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

private:
    QRect m_cropRect;
    QSize m_videoSize;
    int m_selectedFormat = 0;
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
    void createRectClampsToContent();
    void moveRectStaysInside();
    void resizeRectRespectsEdges();
    void snapValueSnapsWithinDistance();
    void applyEmitsDraft();
    void cancelLeavesEditing();
    void beginEditingNeedsContent();
    void contentChangeCancelsDraft();

    // Bounded snapping.
    void moveSnapNeverLeavesContentX();
    void moveSnapNeverLeavesContentY();
    void moveSweepStaysInside();
    void resizeNearCentreKeepsMinimum_data();
    void resizeNearCentreKeepsMinimum();
    void resizeSweepKeepsMinimumAndBounds();
    void createSweepStaysInside();

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
    void previewGeometryChangeCancelsDraft();
    void previewSizeChipAndClear();
    void previewToolbarFitsAtMinimumWidth();
    void previewCursorOverVideo();

private:
    QVariant call(const char* name, const QVariantList& args = {});
    QRectF overlayRect(const char* property) const;

    bool openPreview(const QSize& frameSize);
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

void tst_RecordingCropOverlay::contentChangeCancelsDraft()
{
    m_overlay->setProperty("committedRect", QRectF(50, 100, 100, 50));
    call("beginEditing");
    m_overlay->setProperty("draftRect", QRectF(10, 60, 100, 50));
    QSignalSpy applySpy(m_overlay.get(), SIGNAL(applyRequested(QRectF)));
    QSignalSpy cancelSpy(m_overlay.get(), SIGNAL(cancelRequested()));
    m_overlay->setProperty("contentRect", QRectF(0, 40, 400, 220));
    QVERIFY(!m_overlay->property("editing").toBool());
    QCOMPARE(cancelSpy.count(), 1);
    QCOMPARE(applySpy.count(), 0);
    QCOMPARE(overlayRect("shownRect"), QRectF(50, 100, 100, 50));
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
            }
        }
    }
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

// ---------- Full preview ----------

bool tst_RecordingCropOverlay::openPreview(const QSize& frameSize)
{
    if (QGuiApplication::screens().isEmpty()) {
        return false;
    }
    m_backend = std::make_unique<StubPreviewBackend>();
    m_view = std::make_unique<QQuickView>(SnapTray::QmlOverlayManager::instance().engine(), nullptr);
    m_view->rootContext()->setContextProperty(QStringLiteral("backend"), m_backend.get());
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->setMinimumSize(QSize(kPreviewMinWidth, kPreviewMinHeight));
    m_view->resize(kPreviewWidth, kPreviewHeight);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingPreview.qml")));
    if (m_view->status() != QQuickView::Ready) {
        for (const auto& error : m_view->errors()) {
            qWarning() << error.toString();
        }
        return false;
    }
    m_view->show();
    if (!QTest::qWaitForWindowExposed(m_view.get())) {
        return false;
    }
    m_view->requestActivate();
    if (!QTest::qWaitForWindowActive(m_view.get(), 1000)) {
        qDebug() << "Preview window not active; keys go straight to the root item";
    }

    // Stand in for a decoded frame: the item derives contentRect from it, and the
    // preview reports the video size to the backend when the media loads.
    QQuickItem* video = previewItem("previewVideoPlayer");
    if (!video || video->width() <= 0 || video->height() <= 0) {
        return false;
    }
    QImage frame(frameSize, QImage::Format_ARGB32_Premultiplied);
    frame.fill(Qt::darkGray);
    if (!QMetaObject::invokeMethod(video, "onFrameReady", Qt::DirectConnection, Q_ARG(QImage, frame))) {
        return false;
    }
    m_backend->updateVideoSize(frameSize);
    return !previewContentRect().isEmpty();
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

#define OPEN_PREVIEW_OR_SKIP(frameSize)                                                    \
    do {                                                                                   \
        if (!openPreview(frameSize)) {                                                     \
            QSKIP("RecordingPreview interaction tests need a real, exposable window");     \
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
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
    QQuickItem* video = previewItem("previewVideoPlayer");
    const QSize area(qRound(video->width()), qRound(video->height()));
    if (!letterboxed) {
        m_view.reset();
        OPEN_PREVIEW_OR_SKIP(area);
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
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
    QQuickItem* video = previewItem("previewVideoPlayer");
    const QSize area(qRound(video->width()), qRound(video->height()));
    m_view.reset();
    // A frame that fills the item 1:1, so view and video pixels coincide.
    OPEN_PREVIEW_OR_SKIP(area);
    QCOMPARE(previewContentRect(), QRectF(QPointF(0, 0), QSizeF(area)));

    m_backend->setCropRect(QRect(100, 100, 200, 100));
    QObject* overlay = previewOverlay();
    QCOMPARE(overlay->property("committedRect").toRectF(), QRectF(100, 100, 200, 100));

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    QCOMPARE(overlay->property("draftRect").toRectF(), QRectF(100, 100, 200, 100));

    // Move: drag from inside the selection.
    drag(overlayPoint(200, 150), overlayPoint(230, 170));
    QCOMPARE(overlay->property("draftRect").toRectF(), QRectF(130, 120, 200, 100));

    // Resize: drag the bottom-right handle.
    drag(overlayPoint(330, 220), overlayPoint(350, 230));
    QCOMPARE(overlay->property("draftRect").toRectF(), QRectF(130, 120, 220, 110));

    // Resize: drag the left-edge handle.
    drag(overlayPoint(130, 175), overlayPoint(110, 175));
    QCOMPARE(overlay->property("draftRect").toRectF(), QRectF(110, 120, 240, 110));

    // Replace: drag outside the selection.
    const QPoint start = overlayPoint(area.width() * 0.6, area.height() * 0.6);
    const QPoint end = overlayPoint(area.width() * 0.6 + 100, area.height() * 0.6 + 80);
    drag(start, end);
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
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
    const QRect committed(400, 100, 400, 200);
    m_backend->setCropRect(committed);
    QObject* overlay = previewOverlay();
    const QRectF committedView = overlay->property("committedRect").toRectF();
    QVERIFY(!committedView.isEmpty());

    click(previewItem("previewCropButton"));
    QVERIFY(editing());
    drag(overlayPoint(committedView.center().x(), committedView.center().y()),
         overlayPoint(committedView.center().x() + 40, committedView.center().y() + 10));
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
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
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
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    drag(overlayPoint(content.x() + 100, content.y() + 30), overlayPoint(content.x() + 300, content.y() + 130));
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
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    drag(overlayPoint(content.x() + 120, content.y() + 40), overlayPoint(content.x() + 320, content.y() + 140));
    const QRectF draft = previewOverlay()->property("draftRect").toRectF();
    QVERIFY(!draft.isEmpty());

    sendKey(Qt::Key_S, Qt::ControlModifier);
    QCOMPARE(m_backend->saveCount, 1);
    QCOMPARE(m_backend->cropAtSave, expectedVideoCrop(draft));
    QVERIFY(!editing());
}

void tst_RecordingCropOverlay::previewEnterSavesWhenNotEditing()
{
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
    m_backend->setCropRect(QRect(200, 100, 400, 200));
    sendKey(Qt::Key_Return);
    QCOMPARE(m_backend->saveCount, 1);
    QCOMPARE(m_backend->cropAtSave, QRect(200, 100, 400, 200));
    sendKey(Qt::Key_Enter);
    QCOMPARE(m_backend->saveCount, 2);
}

void tst_RecordingCropOverlay::previewProcessingBlocksSave()
{
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
    click(previewItem("previewCropButton"));
    const QRectF content = previewContentRect();
    drag(overlayPoint(content.x() + 100, content.y() + 30), overlayPoint(content.x() + 300, content.y() + 130));
    const QRectF draft = previewOverlay()->property("draftRect").toRectF();
    QVERIFY(!draft.isEmpty());

    m_backend->setProcessing(true);
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

void tst_RecordingCropOverlay::previewGeometryChangeCancelsDraft()
{
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
    const QRect committed(400, 100, 400, 200);
    m_backend->setCropRect(committed);
    QObject* overlay = previewOverlay();
    const QRectF oldContent = previewContentRect();

    click(previewItem("previewCropButton"));
    const QRectF committedView = overlay->property("committedRect").toRectF();
    drag(overlayPoint(committedView.center().x(), committedView.center().y()),
         overlayPoint(committedView.center().x() + 40, committedView.center().y() + 10));
    QVERIFY(editing());

    m_view->resize(kPreviewWidth + 100, kPreviewHeight + 60);
    QTRY_VERIFY(previewContentRect() != oldContent);
    QVERIFY(!editing());
    QCOMPARE(m_backend->cropRect(), committed);
    QCOMPARE(m_backend->setCropFromViewCount, 0);
    QCOMPARE(overlay->property("committedRect").toRectF(), m_backend->cropRectInView(previewContentRect()));
}

void tst_RecordingCropOverlay::previewSizeChipAndClear()
{
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
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
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
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

void tst_RecordingCropOverlay::previewCursorOverVideo()
{
    OPEN_PREVIEW_OR_SKIP(QSize(1600, 400));
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

QTEST_MAIN(tst_RecordingCropOverlay)
#include "tst_RecordingCropOverlay.moc"
