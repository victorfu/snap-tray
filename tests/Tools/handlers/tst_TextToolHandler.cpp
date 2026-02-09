#include <QtTest/QtTest>

#include "tools/handlers/TextToolHandler.h"
#include "tools/ToolContext.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "region/TextAnnotationEditor.h"
#include "InlineTextEditor.h"

class TestTextToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();

    void testToolIdAndCapabilities();
    void testOnMousePress_StartsInlineEditingWhenTextToolActive();
    void testGlobalPress_SelectsExistingText();
    void testGlobalDoubleClick_StartsReEditAndSyncsColor();
    void testHandleEscape_CancelsInlineEditing();
    void testInteractionMethods_ReturnFalseWithoutContext();

private:
    TextToolHandler* m_handler = nullptr;
    ToolContext* m_context = nullptr;
    AnnotationLayer* m_layer = nullptr;
    QWidget* m_parent = nullptr;
    InlineTextEditor* m_inlineTextEditor = nullptr;
    TextAnnotationEditor* m_textAnnotationEditor = nullptr;
};

void TestTextToolHandler::init()
{
    m_handler = new TextToolHandler();
    m_context = new ToolContext();
    m_layer = new AnnotationLayer();
    m_parent = new QWidget();
    m_parent->resize(800, 600);

    m_inlineTextEditor = new InlineTextEditor(m_parent);
    m_textAnnotationEditor = new TextAnnotationEditor();
    m_textAnnotationEditor->setAnnotationLayer(m_layer);
    m_textAnnotationEditor->setTextEditor(m_inlineTextEditor);
    m_textAnnotationEditor->setParentWidget(m_parent);

    m_context->annotationLayer = m_layer;
    m_context->inlineTextEditor = m_inlineTextEditor;
    m_context->textAnnotationEditor = m_textAnnotationEditor;
    m_context->textEditingBounds = m_parent->rect();
    m_context->color = Qt::red;
}

void TestTextToolHandler::cleanup()
{
    delete m_textAnnotationEditor;
    delete m_inlineTextEditor;
    delete m_parent;
    delete m_layer;
    delete m_context;
    delete m_handler;

    m_textAnnotationEditor = nullptr;
    m_inlineTextEditor = nullptr;
    m_parent = nullptr;
    m_layer = nullptr;
    m_context = nullptr;
    m_handler = nullptr;
}

void TestTextToolHandler::testToolIdAndCapabilities()
{
    QCOMPARE(m_handler->toolId(), ToolId::Text);
    QVERIFY(m_handler->supportsColor());
    QVERIFY(m_handler->supportsTextFormatting());
    QVERIFY(!m_handler->supportsWidth());
}

void TestTextToolHandler::testOnMousePress_StartsInlineEditingWhenTextToolActive()
{
    QVERIFY(!m_inlineTextEditor->isEditing());
    m_handler->onMousePress(m_context, QPoint(120, 120));
    QVERIFY(m_inlineTextEditor->isEditing());
}

void TestTextToolHandler::testGlobalPress_SelectsExistingText()
{
    QFont font;
    font.setPointSize(14);
    auto item = std::make_unique<TextBoxAnnotation>(QPointF(100, 100), "hello", font, Qt::green);
    const QPoint hitPoint = item->boundingRect().center();
    m_layer->addItem(std::move(item));

    const bool handled = m_handler->handleInteractionPress(m_context, hitPoint, false);
    QVERIFY(handled);
    QCOMPARE(m_layer->selectedIndex(), 0);
}

void TestTextToolHandler::testGlobalDoubleClick_StartsReEditAndSyncsColor()
{
    QFont font;
    font.setPointSize(16);
    const QColor textColor(12, 140, 220);
    auto item = std::make_unique<TextBoxAnnotation>(QPointF(150, 150), "edit me", font, textColor);
    const QPoint hitPoint = item->boundingRect().center();
    m_layer->addItem(std::move(item));

    QColor syncedColor;
    bool callbackCalled = false;
    m_context->syncColorToHost = [&](const QColor& color) {
        callbackCalled = true;
        syncedColor = color;
        m_context->color = color;
    };

    const bool handled = m_handler->handleInteractionDoubleClick(m_context, hitPoint);
    QVERIFY(handled);
    QVERIFY(callbackCalled);
    QCOMPARE(syncedColor, textColor);
    QCOMPARE(m_context->color, textColor);

    auto* textItem = dynamic_cast<TextBoxAnnotation*>(m_layer->itemAt(0));
    QVERIFY(textItem != nullptr);
    QVERIFY(!textItem->isVisible());
}

void TestTextToolHandler::testHandleEscape_CancelsInlineEditing()
{
    m_inlineTextEditor->startEditing(QPoint(200, 200), m_parent->rect());
    QVERIFY(m_inlineTextEditor->isEditing());
    QVERIFY(m_handler->handleEscape(m_context));
    QVERIFY(!m_inlineTextEditor->isEditing());
}

void TestTextToolHandler::testInteractionMethods_ReturnFalseWithoutContext()
{
    ToolContext emptyContext;
    QVERIFY(!m_handler->handleInteractionPress(&emptyContext, QPoint(1, 1), false));
    QVERIFY(!m_handler->handleInteractionMove(&emptyContext, QPoint(1, 1)));
    QVERIFY(!m_handler->handleInteractionRelease(&emptyContext, QPoint(1, 1)));
    QVERIFY(!m_handler->handleInteractionDoubleClick(&emptyContext, QPoint(1, 1)));
}

QTEST_MAIN(TestTextToolHandler)
#include "tst_TextToolHandler.moc"
