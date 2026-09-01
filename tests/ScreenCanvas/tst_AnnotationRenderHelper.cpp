#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>
#include <QtMath>

#include "ScreenCanvas.h"
#include "ScreenCanvasSession.h"
#include "TransformationGizmo.h"
#include "annotation/AnnotationRenderHelper.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/MarkerStroke.h"
#include "annotations/TextBoxAnnotation.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "region/TextAnnotationEditor.h"

namespace {

QImage renderArrowVisual(bool useDirtyRegionRendering)
{
    AnnotationLayer layer;
    auto arrow = std::make_unique<ArrowAnnotation>(
        QPoint(40, 50),
        QPoint(160, 100),
        Qt::green,
        4);
    arrow->setControlPoint(QPoint(110, 20));
    layer.addItem(std::move(arrow));
    layer.setSelectedIndex(0);

    QImage image(240, 180, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    snaptray::annotation::SelectedAnnotationItems selectedItems;
    selectedItems.arrow = static_cast<ArrowAnnotation*>(layer.selectedItem());
    snaptray::annotation::drawAnnotationVisuals(
        painter,
        &layer,
        image.size(),
        1.0,
        QPoint(10, 8),
        useDirtyRegionRendering,
        selectedItems);
    painter.end();

    return image;
}

QImage renderMarkerVisual(bool useDirtyRegionRendering)
{
    constexpr qreal dpr = 1.5;
    const QSize logicalSize(240, 160);
    AnnotationLayer layer;
    QVector<QPointF> points = {
        QPointF(40.25, 70.5),
        QPointF(75.5, 44.25),
        QPointF(112.75, 92.5),
        QPointF(158.5, 58.75)
    };
    layer.addItem(std::make_unique<MarkerStroke>(points, Qt::yellow, 20));
    layer.setSelectedIndex(0);

    QImage image(qCeil(logicalSize.width() * dpr),
                 qCeil(logicalSize.height() * dpr),
                 QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(dpr);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    snaptray::annotation::SelectedAnnotationItems selectedItems;
    snaptray::annotation::drawAnnotationVisuals(
        painter,
        &layer,
        logicalSize,
        dpr,
        QPoint(10, 8),
        useDirtyRegionRendering,
        selectedItems);
    painter.end();

    return image;
}

} // namespace

class TestScreenCanvasAnnotationRenderHelper : public QObject
{
    Q_OBJECT

private slots:
    void testArrowRenderMatchesCachedAndDirtyPaths();
    void testMarkerRenderMatchesCachedAndDirtyPaths();
    void testTextInteractionUsesDirtyRegionRendering_data();
    void testTextInteractionUsesDirtyRegionRendering();
};

void TestScreenCanvasAnnotationRenderHelper::testArrowRenderMatchesCachedAndDirtyPaths()
{
    const QImage cachedImage = renderArrowVisual(false);
    const QImage dirtyImage = renderArrowVisual(true);

    QVERIFY(!cachedImage.isNull());
    QVERIFY(!dirtyImage.isNull());
    QCOMPARE(dirtyImage, cachedImage);
}

void TestScreenCanvasAnnotationRenderHelper::testMarkerRenderMatchesCachedAndDirtyPaths()
{
    const QImage cachedImage = renderMarkerVisual(false);
    const QImage dirtyImage = renderMarkerVisual(true);

    QVERIFY(!cachedImage.isNull());
    QVERIFY(!dirtyImage.isNull());
    QCOMPARE(dirtyImage, cachedImage);
}

void TestScreenCanvasAnnotationRenderHelper::testTextInteractionUsesDirtyRegionRendering_data()
{
    QTest::addColumn<int>("handleValue");

    QTest::newRow("position") << static_cast<int>(GizmoHandle::Body);
    QTest::newRow("rotation") << static_cast<int>(GizmoHandle::Rotation);
    QTest::newRow("scale") << static_cast<int>(GizmoHandle::TopRight);
}

void TestScreenCanvasAnnotationRenderHelper::testTextInteractionUsesDirtyRegionRendering()
{
    QFETCH(int, handleValue);
    const auto handle = static_cast<GizmoHandle>(handleValue);

    ScreenCanvasSession session;
    session.m_qmlToolbar.reset();
    session.m_qmlSubToolbar.reset();

    ScreenCanvas surface(&session);
    surface.resize(360, 240);
    surface.setSharedToolManager(session.m_toolManager);
    session.configureSurface(&surface);
    session.m_activeSurface = &surface;

    QFont font;
    font.setPointSize(24);
    font.setBold(true);
    session.m_annotationLayer->addItem(std::make_unique<TextBoxAnnotation>(
        QPointF(80, 100), QStringLiteral("cached text"), font, Qt::green));
    session.m_annotationLayer->setSelectedIndex(0);

    const qreal dpr = surface.devicePixelRatioF();
    const QSize physicalSize(
        qCeil(surface.width() * dpr),
        qCeil(surface.height() * dpr));
    const auto createFrame = [&]() {
        QImage frame(physicalSize, QImage::Format_ARGB32_Premultiplied);
        frame.setDevicePixelRatio(dpr);
        frame.fill(Qt::transparent);
        return frame;
    };
    const auto renderSessionFrame = [&]() {
        QImage frame = createFrame();
        QPainter painter(&frame);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        session.drawAnnotations(&surface, painter);
        painter.end();
        return frame;
    };

    // Prime the full layer cache with text at its original transform.
    const QImage initialFrame = renderSessionFrame();
    QVERIFY(!initialFrame.isNull());

    auto* textItem = dynamic_cast<TextBoxAnnotation*>(session.m_annotationLayer->selectedItem());
    QVERIFY(textItem != nullptr);
    TextAnnotationEditor* textEditor = surface.textAnnotationEditor();
    QVERIFY(textEditor != nullptr);
    const QPointF originalPosition = textItem->position();
    const std::uint64_t revisionBeforeInteraction = session.m_annotationLayer->revision();

    if (handle == GizmoHandle::Body) {
        const QPoint start = textItem->boundingRect().center();
        textEditor->startDragging(start);
        textEditor->updateDragging(start + QPoint(120, 20));
        QCOMPARE(textItem->position(), originalPosition + QPointF(120, 20));
    }
    else if (handle == GizmoHandle::Rotation) {
        const QPoint start = TransformationGizmo::rotationHandlePosition(textItem).toPoint();
        textEditor->startTransformation(start, handle);
        textEditor->updateTransformation(textItem->center().toPoint() + QPoint(70, 0));
        QVERIFY(qAbs(textItem->rotation() - 90.0) < 2.0);
    }
    else {
        const QPointF center = textItem->center();
        const QPointF start = TransformationGizmo::cornerHandlePositions(textItem).at(1);
        textEditor->startTransformation(start.toPoint(), handle);
        textEditor->updateTransformation((center + (start - center) * 1.7).toPoint());
        QVERIFY(textItem->scale() > 1.6 && textItem->scale() < 1.8);
    }

    QVERIFY(textEditor->isDragging() || textEditor->isTransforming());
    QCOMPARE(session.m_annotationLayer->revision(), revisionBeforeInteraction);

    // ScreenCanvasSession must select the dirty path while text is changing.
    // The expected frame explicitly takes that path, excluding stale cached
    // text and drawing the selected item at its current transform.
    const QImage actualFrame = renderSessionFrame();
    QImage expectedFrame = createFrame();
    {
        QPainter painter(&expectedFrame);
        painter.setRenderHint(QPainter::Antialiasing);
        painter.setRenderHint(QPainter::SmoothPixmapTransform);
        snaptray::annotation::SelectedAnnotationItems selectedItems;
        selectedItems.text = textItem;
        snaptray::annotation::drawAnnotationVisuals(
            painter,
            session.m_annotationLayer,
            surface.size(),
            dpr,
            surface.annotationOffset(),
            true,
            selectedItems);
    }

    QCOMPARE(actualFrame, expectedFrame);
    if (textEditor->isDragging()) {
        textEditor->finishDragging();
    }
    else {
        textEditor->finishTransformation();
    }
    QVERIFY(session.m_annotationLayer->revision() > revisionBeforeInteraction);
}

QTEST_MAIN(TestScreenCanvasAnnotationRenderHelper)
#include "tst_AnnotationRenderHelper.moc"
