#include <QtTest/QtTest>

#include "InlineTextEditor.h"
#include "annotation/AnnotationContext.h"
#include "annotation/AnnotationHostAdapter.h"
#include "annotations/AnnotationLayer.h"
#include "region/TextAnnotationEditor.h"
#include "settings/Settings.h"
#include "toolbar/ToolOptionsPanel.h"

namespace {

constexpr const char* kTextBoldKey = "annotation/text_bold";
constexpr const char* kTextItalicKey = "annotation/text_italic";
constexpr const char* kTextUnderlineKey = "annotation/text_underline";
constexpr const char* kTextSizeKey = "annotation/text_size";
constexpr const char* kTextFamilyKey = "annotation/text_family";

QStringList textSettingKeys()
{
    return {
        QString::fromLatin1(kTextBoldKey),
        QString::fromLatin1(kTextItalicKey),
        QString::fromLatin1(kTextUnderlineKey),
        QString::fromLatin1(kTextSizeKey),
        QString::fromLatin1(kTextFamilyKey)
    };
}

} // namespace

class FakeAnnotationHostAdapter : public AnnotationHostAdapter
{
public:
    FakeAnnotationHostAdapter()
        : toolOptionsPanel(&hostWidget)
        , inlineTextEditor(&hostWidget)
        , textAnnotationEditor(&hostWidget)
    {
        textAnnotationEditor.setAnnotationLayer(&annotationLayer);
        textAnnotationEditor.setTextEditor(&inlineTextEditor);
        textAnnotationEditor.setParentWidget(&hostWidget);
        textAnnotationEditor.setColorAndWidthWidget(&toolOptionsPanel);
    }

    QWidget* annotationHostWidget() const override
    {
        return const_cast<QWidget*>(&hostWidget);
    }

    AnnotationLayer* annotationLayerForContext() const override
    {
        return const_cast<AnnotationLayer*>(&annotationLayer);
    }

    ToolOptionsPanel* toolOptionsPanelForContext() const override
    {
        return const_cast<ToolOptionsPanel*>(&toolOptionsPanel);
    }

    InlineTextEditor* inlineTextEditorForContext() const override
    {
        return const_cast<InlineTextEditor*>(&inlineTextEditor);
    }

    TextAnnotationEditor* textAnnotationEditorForContext() const override
    {
        return const_cast<TextAnnotationEditor*>(&textAnnotationEditor);
    }

    void onContextColorSelected(const QColor& color) override
    {
        lastColor = color;
    }

    void onContextMoreColorsRequested() override
    {
        ++moreColorsRequestedCount;
    }

    void onContextLineWidthChanged(int width) override
    {
        lastWidth = width;
    }

    void onContextArrowStyleChanged(LineEndStyle style) override
    {
        lastArrowStyle = style;
    }

    void onContextLineStyleChanged(LineStyle style) override
    {
        lastLineStyle = style;
    }

    void onContextFontSizeDropdownRequested(const QPoint& pos) override
    {
        lastFontSizeDropdownPos = pos;
    }

    void onContextFontFamilyDropdownRequested(const QPoint& pos) override
    {
        lastFontFamilyDropdownPos = pos;
    }

    void onContextTextEditingFinished(const QString& text, const QPoint& position) override
    {
        lastFinishedText = text;
        lastFinishedPosition = position;
    }

    void onContextTextEditingCancelled() override
    {
        ++textEditingCancelledCount;
    }

    QWidget hostWidget;
    AnnotationLayer annotationLayer;
    ToolOptionsPanel toolOptionsPanel;
    InlineTextEditor inlineTextEditor;
    TextAnnotationEditor textAnnotationEditor;

    QColor lastColor;
    int moreColorsRequestedCount = 0;
    int lastWidth = -1;
    LineEndStyle lastArrowStyle = LineEndStyle::EndArrow;
    LineStyle lastLineStyle = LineStyle::Solid;
    QPoint lastFontSizeDropdownPos;
    QPoint lastFontFamilyDropdownPos;
    QString lastFinishedText;
    QPoint lastFinishedPosition;
    int textEditingCancelledCount = 0;
};

class TestAnnotationContext : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void cleanup();
    void testConnectTextFormattingSignals_PropagatesToEditor();
    void testApplyTextFontFormatting_UpdatesEditorAndPanel();
    void testSyncTextFormattingControls_UsesEditorState();

private:
    QHash<QString, QVariant> m_savedTextSettings;
    QSet<QString> m_existingTextKeys;
};

void TestAnnotationContext::init()
{
    m_savedTextSettings.clear();
    m_existingTextKeys.clear();

    auto settings = SnapTray::getSettings();
    for (const QString& key : textSettingKeys()) {
        if (!settings.contains(key)) {
            continue;
        }
        m_existingTextKeys.insert(key);
        m_savedTextSettings.insert(key, settings.value(key));
    }
}

void TestAnnotationContext::cleanup()
{
    auto settings = SnapTray::getSettings();
    for (const QString& key : textSettingKeys()) {
        if (m_existingTextKeys.contains(key)) {
            settings.setValue(key, m_savedTextSettings.value(key));
        } else {
            settings.remove(key);
        }
    }
}

void TestAnnotationContext::testConnectTextFormattingSignals_PropagatesToEditor()
{
    FakeAnnotationHostAdapter host;
    AnnotationContext context(host);

    context.connectTextFormattingSignals();

    QSignalSpy formattingSpy(&host.textAnnotationEditor, &TextAnnotationEditor::formattingChanged);

    host.toolOptionsPanel.setBold(false);
    host.toolOptionsPanel.setItalic(true);
    host.toolOptionsPanel.setUnderline(true);

    const TextFormattingState formatting = host.textAnnotationEditor.formatting();
    QCOMPARE(formatting.bold, false);
    QCOMPARE(formatting.italic, true);
    QCOMPARE(formatting.underline, true);
    QVERIFY(formattingSpy.count() >= 3);
}

void TestAnnotationContext::testApplyTextFontFormatting_UpdatesEditorAndPanel()
{
    FakeAnnotationHostAdapter host;
    AnnotationContext context(host);

    context.applyTextFontSize(28);
    context.applyTextFontFamily(QStringLiteral("Courier New"));

    const TextFormattingState formatting = host.textAnnotationEditor.formatting();
    QCOMPARE(formatting.fontSize, 28);
    QCOMPARE(formatting.fontFamily, QStringLiteral("Courier New"));
    QCOMPARE(host.toolOptionsPanel.fontSize(), 28);
    QCOMPARE(host.toolOptionsPanel.fontFamily(), QStringLiteral("Courier New"));
}

void TestAnnotationContext::testSyncTextFormattingControls_UsesEditorState()
{
    FakeAnnotationHostAdapter host;
    AnnotationContext context(host);

    // Make panel deliberately different from editor state.
    host.toolOptionsPanel.setBold(true);
    host.toolOptionsPanel.setItalic(false);
    host.toolOptionsPanel.setUnderline(false);
    host.toolOptionsPanel.setFontSize(10);
    host.toolOptionsPanel.setFontFamily(QStringLiteral("Arial"));

    host.textAnnotationEditor.setBold(false);
    host.textAnnotationEditor.setItalic(true);
    host.textAnnotationEditor.setUnderline(true);
    host.textAnnotationEditor.setFontSize(22);
    host.textAnnotationEditor.setFontFamily(QStringLiteral("Georgia"));

    context.syncTextFormattingControls();

    QCOMPARE(host.toolOptionsPanel.isBold(), false);
    QCOMPARE(host.toolOptionsPanel.isItalic(), true);
    QCOMPARE(host.toolOptionsPanel.isUnderline(), true);
    QCOMPARE(host.toolOptionsPanel.fontSize(), 22);
    QCOMPARE(host.toolOptionsPanel.fontFamily(), QStringLiteral("Georgia"));
}

QTEST_MAIN(TestAnnotationContext)
#include "tst_AnnotationContext.moc"
