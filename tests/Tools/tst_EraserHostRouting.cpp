#include <QtTest>

#include <QMouseEvent>
#include <QScopeGuard>
#include <array>
#include <functional>

#include "InlineTextEditor.h"
#include "PinWindow.h"
#include "ScreenCanvas.h"
#include "ScreenCanvasSession.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"
#include "cursor/CursorManager.h"
#include "qml/QmlFloatingSubToolbar.h"
#include "qml/QmlFloatingToolbar.h"
#include "region/RegionInputHandler.h"
#include "region/SelectionStateManager.h"
#include "region/ShapeAnnotationEditor.h"
#include "region/TextAnnotationEditor.h"
#include "tools/ToolManager.h"

namespace {
struct AnnotationCase
{
    const char* name;
    std::function<std::unique_ptr<AnnotationItem>()> create;
};

const std::array<AnnotationCase, 5> kAnnotations = {{
    {"arrow", [] {
        return std::make_unique<ArrowAnnotation>(QPoint(60, 100), QPoint(220, 100), Qt::red, 4);
    }},
    {"polyline", [] {
        return std::make_unique<PolylineAnnotation>(
            QVector<QPoint>{QPoint(60, 100), QPoint(140, 100), QPoint(220, 100)}, Qt::red, 4);
    }},
    {"shape", [] {
        return std::make_unique<ShapeAnnotation>(
            QRect(60, 70, 160, 80), ShapeType::Rectangle, Qt::red, 4, true);
    }},
    {"text", [] {
        QFont font;
        font.setPointSize(24);
        return std::make_unique<TextBoxAnnotation>(
            QPointF(60, 70), QStringLiteral("Erase this"), font, Qt::red);
    }},
    {"emoji", [] {
        return std::make_unique<EmojiStickerAnnotation>(QPoint(140, 100), QStringLiteral("\u2605"));
    }}
}};

void addRoutingData()
{
    QTest::addColumn<int>("annotation");
    QTest::addColumn<bool>("selected");
    QTest::addColumn<bool>("blankStart");
    QTest::addColumn<bool>("eraser");
    for (int annotation = 0; annotation < static_cast<int>(kAnnotations.size()); ++annotation) {
        for (bool selected : {false, true}) {
            for (bool blankStart : {false, true}) {
                QTest::addRow("%s-%s-%s", kAnnotations[annotation].name,
                              selected ? "selected" : "unselected", blankStart ? "sweep" : "press")
                    << annotation << selected << blankStart << true;
            }
            QTest::addRow("%s-%s-pencil-edit", kAnnotations[annotation].name,
                          selected ? "selected" : "unselected")
                << annotation << selected << false << false;
        }
    }
}

using SendEvent = std::function<void(QMouseEvent*)>;

void verifyRouting(AnnotationLayer* layer, ToolManager* manager,
                   const SendEvent& press, const SendEvent& move, const SendEvent& release)
{
    QFETCH(int, annotation);
    QFETCH(bool, selected);
    QFETCH(bool, blankStart);
    QFETCH(bool, eraser);

    layer->addItem(kAnnotations[annotation].create());
    const QRect original = layer->itemAt(0)->boundingRect();
    const QPoint hit = original.center();
    layer->setSelectedIndex(selected ? 0 : -1);
    manager->setCurrentTool(eraser ? ToolId::Eraser : ToolId::Pencil);

    const QPoint start = blankStart ? QPoint(20, 20) : hit;
    QMouseEvent down(QEvent::MouseButtonPress, start, start, start,
                     Qt::LeftButton, Qt::LeftButton, Qt::NoModifier);
    press(&down);
    QCOMPARE(layer->itemCount(), size_t(eraser && !blankStart ? 0 : 1));
    QCOMPARE(manager->isDrawing(), eraser);
    QCOMPARE(layer->selectedIndex(), eraser ? -1 : 0);

    const QPoint end = blankStart ? hit : hit + QPoint(25, 20);
    QMouseEvent drag(QEvent::MouseMove, end, end, end,
                     Qt::NoButton, Qt::LeftButton, Qt::NoModifier);
    move(&drag);
    QMouseEvent up(QEvent::MouseButtonRelease, end, end, end,
                   Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    release(&up);
    QVERIFY(!manager->isDrawing());

    if (eraser) {
        QVERIFY(layer->isEmpty());
        layer->undo();
        QCOMPARE(layer->itemCount(), size_t(1));
        QCOMPARE(layer->itemAt(0)->boundingRect(), original);
        layer->redo();
        QVERIFY(layer->isEmpty());
    } else {
        QCOMPARE(layer->itemCount(), size_t(1));
        QVERIFY(layer->itemAt(0)->boundingRect() != original);
    }
}
} // namespace

class TestEraserHostRouting : public QObject
{
    Q_OBJECT

private slots:
    void region_data() { addRoutingData(); }
    void region();
    void pinWindow_data() { addRoutingData(); }
    void pinWindow();
    void screenCanvas_data() { addRoutingData(); }
    void screenCanvas();
};

void TestEraserHostRouting::region()
{
    QFETCH(bool, eraser);
    QWidget surface;
    surface.resize(400, 300);
    AnnotationLayer layer;
    ToolManager manager;
    manager.registerDefaultHandlers();
    manager.setAnnotationLayer(&layer);
    ShapeAnnotationEditor shapeEditor;
    shapeEditor.setAnnotationLayer(&layer);
    TextAnnotationEditor textEditor;
    InlineTextEditor inlineEditor(&surface);
    textEditor.setAnnotationLayer(&layer);
    textEditor.setTextEditor(&inlineEditor);
    textEditor.setParentWidget(&surface);
    manager.setTextAnnotationEditor(&textEditor);
    manager.setShapeAnnotationEditor(&shapeEditor);
    SelectionStateManager selection;
    selection.setBounds(surface.rect());
    selection.setSelectionRect(surface.rect());
    RegionInputState state;
    state.currentTool = eraser ? ToolId::Eraser : ToolId::Pencil;
    RegionInputHandler handler;
    handler.setParentWidget(&surface);
    handler.setSharedState(&state);
    handler.setSelectionManager(&selection);
    handler.setAnnotationLayer(&layer);
    handler.setToolManager(&manager);
    handler.setTextEditor(&inlineEditor);
    handler.setTextAnnotationEditor(&textEditor);
    CursorManager::instance().registerWidget(&surface, &manager);
    const auto unregister = qScopeGuard([&] {
        CursorManager::instance().unregisterWidget(&surface);
    });
    verifyRouting(&layer, &manager,
        [&](QMouseEvent* event) { handler.handleMousePress(event); },
        [&](QMouseEvent* event) { handler.handleMouseMove(event); },
        [&](QMouseEvent* event) { handler.handleMouseRelease(event); });
}

void TestEraserHostRouting::pinWindow()
{
    QFETCH(bool, eraser);
    QPixmap pixmap(400, 300);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint());
    if (!window.m_toolManager) {
        window.initializeAnnotationComponents();
    }
    window.m_annotationMode = true;
    window.m_currentToolId = eraser ? ToolId::Eraser : ToolId::Pencil;
    verifyRouting(window.m_annotationLayer, window.m_toolManager,
        [&](QMouseEvent* event) { window.mousePressEvent(event); },
        [&](QMouseEvent* event) { window.mouseMoveEvent(event); },
        [&](QMouseEvent* event) { window.mouseReleaseEvent(event); });
}

void TestEraserHostRouting::screenCanvas()
{
    QFETCH(bool, eraser);
    ScreenCanvasSession session;
    session.m_qmlToolbar.reset();
    session.m_qmlSubToolbar.reset();
    ScreenCanvas surface(&session);
    surface.resize(400, 300);
    surface.setSharedToolManager(session.m_toolManager);
    session.configureSurface(&surface);
    session.m_currentToolId = eraser ? ToolId::Eraser : ToolId::Pencil;
    verifyRouting(session.m_annotationLayer, session.m_toolManager,
        [&](QMouseEvent* event) {
            session.handleSurfaceMousePress(&surface, event);
            // Keep subsequent synthetic events in local coordinates instead of sampling QCursor.
            session.endMouseGrab();
        },
        [&](QMouseEvent* event) { session.handleSurfaceMouseMove(&surface, event); },
        [&](QMouseEvent* event) { session.handleSurfaceMouseRelease(&surface, event); });
}

QTEST_MAIN(TestEraserHostRouting)
#include "tst_EraserHostRouting.moc"
