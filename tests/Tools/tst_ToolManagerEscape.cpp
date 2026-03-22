#include <QtTest/QtTest>
#include <QSignalSpy>

#include "tools/ToolManager.h"
#include "tools/IToolHandler.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/ArrowAnnotation.h"

class FakeEscapeHandler : public IToolHandler
{
public:
    explicit FakeEscapeHandler(bool escapeHandled)
        : m_escapeHandled(escapeHandled)
    {
    }

    ToolId toolId() const override
    {
        return ToolId::Pencil;
    }

    bool handleEscape(ToolContext* ctx) override
    {
        Q_UNUSED(ctx);
        ++m_escapeCallCount;
        return m_escapeHandled;
    }

    int escapeCallCount() const
    {
        return m_escapeCallCount;
    }

private:
    bool m_escapeHandled = false;
    int m_escapeCallCount = 0;
};

class FakeInteractionEscapeHandler : public IToolHandler
{
public:
    ToolId toolId() const override
    {
        return ToolId::Text;
    }

    bool handleEscape(ToolContext* ctx) override
    {
        Q_UNUSED(ctx);
        ++m_escapeCallCount;
        m_interactionActive = false;
        return true;
    }

    QRect interactionBounds(const ToolContext* ctx) const override
    {
        Q_UNUSED(ctx);
        return m_interactionActive ? QRect(20, 30, 40, 50) : QRect();
    }

    AnnotationInteractionKind activeInteractionKind(const ToolContext* ctx) const override
    {
        Q_UNUSED(ctx);
        return m_interactionActive
            ? AnnotationInteractionKind::SelectedTransform
            : AnnotationInteractionKind::None;
    }

    int escapeCallCount() const
    {
        return m_escapeCallCount;
    }

private:
    bool m_interactionActive = true;
    int m_escapeCallCount = 0;
};

class TestToolManagerEscape : public QObject
{
    Q_OBJECT

private slots:
    void testHandleEscape_ClearsSelectionWhenHandlerIgnoresEscape();
    void testHandleEscape_PreservesSelectionWhenHandlerConsumesEscape();
    void testHandleEscape_RepaintsActiveInteractionBounds();
    void testHandleEscape_DispatchesToActiveInteractionTool();
    void testHandleEscape_ReturnsFalseWhenNothingHandled();

private:
    static void addSelectableArrow(AnnotationLayer& layer)
    {
        auto arrow = std::make_unique<ArrowAnnotation>(
            QPoint(10, 10), QPoint(120, 90), Qt::red, 3);
        layer.addItem(std::move(arrow));
        layer.setSelectedIndex(0);
    }
};

void TestToolManagerEscape::testHandleEscape_ClearsSelectionWhenHandlerIgnoresEscape()
{
    ToolManager manager;
    AnnotationLayer layer;
    manager.setAnnotationLayer(&layer);

    auto* handler = new FakeEscapeHandler(false);
    manager.registerHandler(std::unique_ptr<IToolHandler>(handler));
    manager.setCurrentTool(ToolId::Pencil);

    addSelectableArrow(layer);
    QCOMPARE(layer.selectedIndex(), 0);

    QSignalSpy repaintSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    const bool handled = manager.handleEscape();
    QVERIFY(handled);
    QCOMPARE(handler->escapeCallCount(), 1);
    QCOMPARE(layer.selectedIndex(), -1);
    QCOMPARE(repaintSpy.count(), 1);
}

void TestToolManagerEscape::testHandleEscape_PreservesSelectionWhenHandlerConsumesEscape()
{
    ToolManager manager;
    AnnotationLayer layer;
    manager.setAnnotationLayer(&layer);

    auto* handler = new FakeEscapeHandler(true);
    manager.registerHandler(std::unique_ptr<IToolHandler>(handler));
    manager.setCurrentTool(ToolId::Pencil);

    addSelectableArrow(layer);
    QCOMPARE(layer.selectedIndex(), 0);

    QSignalSpy repaintSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    const bool handled = manager.handleEscape();
    QVERIFY(handled);
    QCOMPARE(handler->escapeCallCount(), 1);
    QCOMPARE(layer.selectedIndex(), 0);
    QCOMPARE(repaintSpy.count(), 0);
}

void TestToolManagerEscape::testHandleEscape_RepaintsActiveInteractionBounds()
{
    ToolManager manager;
    manager.registerHandler(std::make_unique<FakeInteractionEscapeHandler>());
    manager.setCurrentTool(ToolId::Text);

    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));
    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));

    const bool handled = manager.handleEscape();
    QVERIFY(handled);
    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.count(), 1);
    QCOMPARE(rectSpy.takeFirst().at(0).toRect(), QRect(20, 30, 40, 50));
}

void TestToolManagerEscape::testHandleEscape_DispatchesToActiveInteractionTool()
{
    ToolManager manager;
    auto* currentHandler = new FakeEscapeHandler(false);
    auto* interactionHandler = new FakeInteractionEscapeHandler();
    manager.registerHandler(std::unique_ptr<IToolHandler>(currentHandler));
    manager.registerHandler(std::unique_ptr<IToolHandler>(interactionHandler));
    manager.setCurrentTool(ToolId::Pencil);

    QSignalSpy fullSpy(&manager, qOverload<>(&ToolManager::needsRepaint));
    QSignalSpy rectSpy(&manager, qOverload<const QRect&>(&ToolManager::needsRepaint));

    const bool handled = manager.handleEscape();
    QVERIFY(handled);
    QCOMPARE(currentHandler->escapeCallCount(), 1);
    QCOMPARE(interactionHandler->escapeCallCount(), 1);
    QCOMPARE(fullSpy.count(), 0);
    QCOMPARE(rectSpy.count(), 1);
    QCOMPARE(rectSpy.takeFirst().at(0).toRect(), QRect(20, 30, 40, 50));
}

void TestToolManagerEscape::testHandleEscape_ReturnsFalseWhenNothingHandled()
{
    ToolManager manager;
    AnnotationLayer layer;
    manager.setAnnotationLayer(&layer);

    auto* handler = new FakeEscapeHandler(false);
    manager.registerHandler(std::unique_ptr<IToolHandler>(handler));
    manager.setCurrentTool(ToolId::Pencil);

    QSignalSpy repaintSpy(&manager, qOverload<>(&ToolManager::needsRepaint));

    const bool handled = manager.handleEscape();
    QVERIFY(!handled);
    QCOMPARE(handler->escapeCallCount(), 1);
    QCOMPARE(layer.selectedIndex(), -1);
    QCOMPARE(repaintSpy.count(), 0);
}

QTEST_MAIN(TestToolManagerEscape)
#include "tst_ToolManagerEscape.moc"
