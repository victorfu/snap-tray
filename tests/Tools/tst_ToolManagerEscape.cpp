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

class TestToolManagerEscape : public QObject
{
    Q_OBJECT

private slots:
    void testHandleEscape_ClearsSelectionWhenHandlerIgnoresEscape();
    void testHandleEscape_PreservesSelectionWhenHandlerConsumesEscape();
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

    QSignalSpy repaintSpy(&manager, &ToolManager::needsRepaint);

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

    QSignalSpy repaintSpy(&manager, &ToolManager::needsRepaint);

    const bool handled = manager.handleEscape();
    QVERIFY(handled);
    QCOMPARE(handler->escapeCallCount(), 1);
    QCOMPARE(layer.selectedIndex(), 0);
    QCOMPARE(repaintSpy.count(), 0);
}

void TestToolManagerEscape::testHandleEscape_ReturnsFalseWhenNothingHandled()
{
    ToolManager manager;
    AnnotationLayer layer;
    manager.setAnnotationLayer(&layer);

    auto* handler = new FakeEscapeHandler(false);
    manager.registerHandler(std::unique_ptr<IToolHandler>(handler));
    manager.setCurrentTool(ToolId::Pencil);

    QSignalSpy repaintSpy(&manager, &ToolManager::needsRepaint);

    const bool handled = manager.handleEscape();
    QVERIFY(!handled);
    QCOMPARE(handler->escapeCallCount(), 1);
    QCOMPARE(layer.selectedIndex(), -1);
    QCOMPARE(repaintSpy.count(), 0);
}

QTEST_MAIN(TestToolManagerEscape)
#include "tst_ToolManagerEscape.moc"
