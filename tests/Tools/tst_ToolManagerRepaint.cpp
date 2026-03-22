#include <QtTest/QtTest>
#include <QSignalSpy>

#include "TransformationGizmo.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotation/AnnotationSurfaceAdapter.h"
#include "region/ShapeAnnotationEditor.h"
#include "tools/ToolManager.h"
#include "tools/IToolHandler.h"
#include "tools/handlers/ArrowToolHandler.h"
#include "tools/handlers/EmojiStickerToolHandler.h"
#include "tools/handlers/PolylineToolHandler.h"
#include "tools/handlers/ShapeToolHandler.h"

class FakeRectPreviewHandler : public IToolHandler
{
public:
    ToolId toolId() const override
    {
        return ToolId::Pencil;
    }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override
    {
        Q_UNUSED(ctx);
        m_bounds = QRect(pos.x() - 5, pos.y() - 5, 10, 10);
    }

    void onMouseMove(ToolContext* ctx, const QPoint& pos) override
    {
        Q_UNUSED(ctx);
        m_bounds = QRect(pos.x() - 5, pos.y() - 5, 10, 10);
    }

    void onMouseRelease(ToolContext* ctx, const QPoint& pos) override
    {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
        m_bounds = QRect();
    }

    QRect previewBounds(const ToolContext* ctx) const override
    {
        Q_UNUSED(ctx);
        return m_bounds;
    }

private:
    QRect m_bounds;
};

class FakeFullRepaintHandler : public IToolHandler
{
public:
    ToolId toolId() const override
    {
        return ToolId::Marker;
    }

    void onMousePress(ToolContext* ctx, const QPoint& pos) override
    {
        Q_UNUSED(pos);
        ctx->repaint();
    }
};

class FakeAnnotationSurfaceAdapter : public AnnotationSurfaceAdapter
{
public:
    void requestFullAnnotationUpdate() override
    {
        ++fullUpdateCount;
    }

    void requestAnnotationUpdate(const QRect& annotationRect) override
    {
        ++rectUpdateCount;
        lastRequestedRect = annotationRect;
    }

    bool supportsAnnotationRectUpdate() const override
    {
        return supportsRectUpdate;
    }

    QRect mapAnnotationRectToHostUpdate(const QRect& annotationRect) const override
    {
        return annotationRect.translated(mapOffset);
    }

    QRect annotationViewport() const override
    {
        return viewport;
    }

    bool supportsRectUpdate = true;
    QPoint mapOffset;
    QRect viewport;
    int fullUpdateCount = 0;
    int rectUpdateCount = 0;
    QRect lastRequestedRect;
};

class FakeInteractionHandler : public IToolHandler
{
public:
    ToolId toolId() const override
    {
        return ToolId::Arrow;
    }

    bool handleInteractionPress(ToolContext* ctx,
                                const QPoint& pos,
                                Qt::KeyboardModifiers modifiers) override
    {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
        Q_UNUSED(modifiers);
        m_kind = AnnotationInteractionKind::SelectedDrag;
        return true;
    }

    bool handleInteractionRelease(ToolContext* ctx,
                                  const QPoint& pos,
                                  Qt::KeyboardModifiers modifiers) override
    {
        Q_UNUSED(ctx);
        Q_UNUSED(pos);
        Q_UNUSED(modifiers);
        m_kind = AnnotationInteractionKind::None;
        return true;
    }

    QRect interactionBounds(const ToolContext* ctx) const override
    {
        Q_UNUSED(ctx);
        return QRect(1, 2, 3, 4);
    }

    AnnotationInteractionKind activeInteractionKind(const ToolContext* ctx) const override
    {
        Q_UNUSED(ctx);
        return m_kind;
    }

private:
    AnnotationInteractionKind m_kind = AnnotationInteractionKind::None;
};

class TestToolManagerRepaint : public QObject
{
    Q_OBJECT

private slots:
    void testPreviewRepaintUsesDirtyUnionAcrossFrames();
    void testExplicitFullRepaintFallsBackToLegacySignal();
    void testAnnotationSurfaceRectRepaintMapsToHostViewport();
    void testAnnotationSurfaceWithoutRectSupportFallsBackToFullUpdate();
    void testAnnotationSurfaceInvalidMappedRectFallsBackToFullSignal();
    void testActiveInteractionStateUsesSharedHandlerState();
    void testShapeInteractionDirtyRectCoversOldRotationHandle();
    void testArrowInteractionDirtyRectCoversOldControlHandle();
    void testPolylineInteractionDirtyRectCoversOldVertexHandle();
    void testEmojiCommitRequestsRectRepaintOnRelease();
};

void TestToolManagerRepaint::testPreviewRepaintUsesDirtyUnionAcrossFrames()
{
    ToolManager manager;
    auto* handler = new FakeRectPreviewHandler();
    manager.registerHandler(std::unique_ptr<IToolHandler>(handler));
    manager.setCurrentTool(ToolId::Pencil);

    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));
    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    manager.handleMousePress(QPoint(10, 10));
    QCOMPARE(rectSpy.count(), 1);
    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.takeFirst().at(0).toRect(), QRect(5, 5, 10, 10));

    manager.handleMouseMove(QPoint(20, 10));
    QCOMPARE(rectSpy.count(), 1);
    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.takeFirst().at(0).toRect(), QRect(5, 5, 20, 10));

    manager.handleMouseRelease(QPoint(20, 10));
    QCOMPARE(rectSpy.count(), 1);
    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.takeFirst().at(0).toRect(), QRect(15, 5, 10, 10));
}

void TestToolManagerRepaint::testExplicitFullRepaintFallsBackToLegacySignal()
{
    ToolManager manager;
    auto* handler = new FakeFullRepaintHandler();
    manager.registerHandler(std::unique_ptr<IToolHandler>(handler));
    manager.setCurrentTool(ToolId::Marker);

    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));
    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    manager.handleMousePress(QPoint(10, 10));

    QCOMPARE(rectSpy.count(), 0);
    QCOMPARE(fullSpy.count(), 1);
}

void TestToolManagerRepaint::testAnnotationSurfaceRectRepaintMapsToHostViewport()
{
    ToolManager manager;
    FakeAnnotationSurfaceAdapter surfaceAdapter;
    surfaceAdapter.mapOffset = QPoint(7, 9);
    surfaceAdapter.viewport = QRect(0, 0, 40, 40);
    manager.setAnnotationSurfaceAdapter(&surfaceAdapter);

    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));
    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    manager.context()->repaint(QRect(10, 10, 30, 30));

    QCOMPARE(surfaceAdapter.rectUpdateCount, 1);
    QCOMPARE(surfaceAdapter.lastRequestedRect, QRect(10, 10, 30, 30));
    QCOMPARE(surfaceAdapter.fullUpdateCount, 0);
    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.count(), 0);
}

void TestToolManagerRepaint::testAnnotationSurfaceWithoutRectSupportFallsBackToFullUpdate()
{
    ToolManager manager;
    FakeAnnotationSurfaceAdapter surfaceAdapter;
    surfaceAdapter.supportsRectUpdate = false;
    manager.setAnnotationSurfaceAdapter(&surfaceAdapter);

    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));
    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    manager.context()->repaint(QRect(10, 10, 30, 30));

    QCOMPARE(surfaceAdapter.rectUpdateCount, 0);
    QCOMPARE(surfaceAdapter.fullUpdateCount, 1);
    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.count(), 0);
}

void TestToolManagerRepaint::testAnnotationSurfaceInvalidMappedRectFallsBackToFullSignal()
{
    ToolManager manager;
    FakeAnnotationSurfaceAdapter surfaceAdapter;
    surfaceAdapter.mapOffset = QPoint(100, 100);
    surfaceAdapter.viewport = QRect(0, 0, 40, 40);
    manager.setAnnotationSurfaceAdapter(&surfaceAdapter);

    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));
    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    manager.context()->repaint(QRect());

    QCOMPARE(rectSpy.count(), 0);
    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(surfaceAdapter.rectUpdateCount, 0);
    QCOMPARE(surfaceAdapter.fullUpdateCount, 1);
}

void TestToolManagerRepaint::testActiveInteractionStateUsesSharedHandlerState()
{
    ToolManager manager;
    manager.registerHandler(std::make_unique<FakeInteractionHandler>());

    QVERIFY(!manager.hasActiveInteraction());
    QCOMPARE(manager.activeInteractionKind(), AnnotationInteractionKind::None);

    QVERIFY(manager.handleArrowInteractionPress(QPoint(10, 10)));
    QVERIFY(manager.hasActiveInteraction());
    QCOMPARE(manager.activeInteractionTool(), ToolId::Arrow);
    QCOMPARE(manager.activeInteractionKind(), AnnotationInteractionKind::SelectedDrag);

    QVERIFY(manager.handleArrowInteractionRelease(QPoint(10, 10)));
    QVERIFY(!manager.hasActiveInteraction());
    QCOMPARE(manager.activeInteractionKind(), AnnotationInteractionKind::None);
}

void TestToolManagerRepaint::testShapeInteractionDirtyRectCoversOldRotationHandle()
{
    ToolManager manager;
    AnnotationLayer layer;
    ShapeAnnotationEditor editor;
    editor.setAnnotationLayer(&layer);

    manager.registerHandler(std::make_unique<ShapeToolHandler>());
    manager.setAnnotationLayer(&layer);
    manager.setShapeAnnotationEditor(&editor);

    auto shape = std::make_unique<ShapeAnnotation>(
        QRect(100, 120, 140, 80), ShapeType::Rectangle, Qt::red, 3, false);
    auto* shapePtr = shape.get();
    layer.addItem(std::move(shape));
    layer.setSelectedIndex(0);

    const QPointF oldRotationHandle = TransformationGizmo::rotationHandlePosition(shapePtr);
    const int handleMargin = TransformationGizmo::kRotationHandleRadius + 2;
    const QRect oldHandleRect(
        qFloor(oldRotationHandle.x()) - handleMargin,
        qFloor(oldRotationHandle.y()) - handleMargin,
        handleMargin * 2 + 1,
        handleMargin * 2 + 1);

    const QPoint pressPos = shapePtr->rect().center();
    QVERIFY(manager.handleShapeInteractionPress(pressPos));

    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));
    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    QVERIFY(manager.handleShapeInteractionMove(pressPos + QPoint(30, 18)));

    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.count(), 1);
    const QRect dirtyRect = rectSpy.takeFirst().at(0).toRect();
    QVERIFY2(
        dirtyRect.contains(oldHandleRect),
        qPrintable(QStringLiteral("dirtyRect=%1 expectedOldHandleRect=%2")
                       .arg(QString::fromLatin1("%1,%2 %3x%4")
                                .arg(dirtyRect.x())
                                .arg(dirtyRect.y())
                                .arg(dirtyRect.width())
                                .arg(dirtyRect.height()))
                       .arg(QString::fromLatin1("%1,%2 %3x%4")
                                .arg(oldHandleRect.x())
                                .arg(oldHandleRect.y())
                                .arg(oldHandleRect.width())
                                .arg(oldHandleRect.height()))));
}

void TestToolManagerRepaint::testArrowInteractionDirtyRectCoversOldControlHandle()
{
    ToolManager manager;
    AnnotationLayer layer;

    manager.registerHandler(std::make_unique<ArrowToolHandler>());
    manager.setAnnotationLayer(&layer);

    auto arrow = std::make_unique<ArrowAnnotation>(
        QPoint(100, 160), QPoint(260, 160), Qt::red, 3, LineEndStyle::EndArrow, LineStyle::Solid);
    arrow->setControlPoint(QPoint(180, 70));
    auto* arrowPtr = arrow.get();
    layer.addItem(std::move(arrow));
    layer.setSelectedIndex(0);

    const QPointF oldCurveMidpoint =
        0.25 * QPointF(arrowPtr->start()) +
        0.5 * QPointF(arrowPtr->controlPoint()) +
        0.25 * QPointF(arrowPtr->end());
    const int handleMargin = TransformationGizmo::kControlHandleRadius + 2;
    const QRect oldHandleRect(
        qFloor(oldCurveMidpoint.x()) - handleMargin,
        qFloor(oldCurveMidpoint.y()) - handleMargin,
        handleMargin * 2 + 1,
        handleMargin * 2 + 1);

    const QPoint pressPos = oldCurveMidpoint.toPoint();
    QVERIFY(manager.handleArrowInteractionPress(pressPos));

    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));
    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    QVERIFY(manager.handleArrowInteractionMove(pressPos + QPoint(24, 28)));

    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.count(), 1);
    const QRect dirtyRect = rectSpy.takeFirst().at(0).toRect();
    QVERIFY2(
        dirtyRect.contains(oldHandleRect),
        qPrintable(QStringLiteral("dirtyRect=%1 expectedOldHandleRect=%2")
                       .arg(QString::fromLatin1("%1,%2 %3x%4")
                                .arg(dirtyRect.x())
                                .arg(dirtyRect.y())
                                .arg(dirtyRect.width())
                                .arg(dirtyRect.height()))
                       .arg(QString::fromLatin1("%1,%2 %3x%4")
                                .arg(oldHandleRect.x())
                                .arg(oldHandleRect.y())
                                .arg(oldHandleRect.width())
                                .arg(oldHandleRect.height()))));
}

void TestToolManagerRepaint::testPolylineInteractionDirtyRectCoversOldVertexHandle()
{
    ToolManager manager;
    AnnotationLayer layer;

    manager.registerHandler(std::make_unique<PolylineToolHandler>());
    manager.setAnnotationLayer(&layer);

    auto polyline = std::make_unique<PolylineAnnotation>(
        QVector<QPoint>{QPoint(120, 120), QPoint(220, 150), QPoint(260, 240)},
        Qt::red,
        3,
        LineEndStyle::EndArrow,
        LineStyle::Solid);
    auto* polylinePtr = polyline.get();
    layer.addItem(std::move(polyline));
    layer.setSelectedIndex(0);

    const QPoint oldVertex = polylinePtr->points().at(1);
    const int handleMargin = TransformationGizmo::kArrowHandleRadius + 2;
    const QRect oldHandleRect(
        oldVertex.x() - handleMargin,
        oldVertex.y() - handleMargin,
        handleMargin * 2 + 1,
        handleMargin * 2 + 1);

    QVERIFY(manager.handlePolylineInteractionPress(oldVertex));

    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));
    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    QVERIFY(manager.handlePolylineInteractionMove(oldVertex + QPoint(28, 22)));

    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.count(), 1);
    const QRect dirtyRect = rectSpy.takeFirst().at(0).toRect();
    QVERIFY2(
        dirtyRect.contains(oldHandleRect),
        qPrintable(QStringLiteral("dirtyRect=%1 expectedOldHandleRect=%2")
                       .arg(QString::fromLatin1("%1,%2 %3x%4")
                                .arg(dirtyRect.x())
                                .arg(dirtyRect.y())
                                .arg(dirtyRect.width())
                                .arg(dirtyRect.height()))
                       .arg(QString::fromLatin1("%1,%2 %3x%4")
                                .arg(oldHandleRect.x())
                                .arg(oldHandleRect.y())
                                .arg(oldHandleRect.width())
                                .arg(oldHandleRect.height()))));
}

void TestToolManagerRepaint::testEmojiCommitRequestsRectRepaintOnRelease()
{
    ToolManager manager;
    AnnotationLayer layer;

    manager.registerDefaultHandlers();
    manager.setAnnotationLayer(&layer);
    manager.setCurrentTool(ToolId::EmojiSticker);

    auto* handler = dynamic_cast<EmojiStickerToolHandler*>(manager.handler(ToolId::EmojiSticker));
    QVERIFY(handler != nullptr);
    handler->setCurrentEmoji(QStringLiteral("🙂"));

    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));
    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    const QPoint releasePos(180, 140);
    manager.handleMouseRelease(releasePos);

    QCOMPARE(layer.itemCount(), size_t{1});
    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.count(), 1);

    const QRect dirtyRect = rectSpy.takeFirst().at(0).toRect();
    QVERIFY(dirtyRect.isValid());
    QVERIFY(dirtyRect.contains(releasePos));
}

QTEST_MAIN(TestToolManagerRepaint)
#include "tst_ToolManagerRepaint.moc"
