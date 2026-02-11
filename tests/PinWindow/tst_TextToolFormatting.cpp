#include <QtTest/QtTest>
#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QMenu>
#include <QPixmap>
#include <QSet>
#include <QTimer>

#include "PinWindow.h"
#include "InlineTextEditor.h"
#include "annotation/AnnotationHostAdapter.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
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

void queueMenuActionSelection(const QString& actionText, bool* triggered, int retriesLeft = 30)
{
    QTimer::singleShot(0, [actionText, triggered, retriesLeft]() {
        auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!menu) {
            if (retriesLeft > 0) {
                queueMenuActionSelection(actionText, triggered, retriesLeft - 1);
            }
            return;
        }

        for (QAction* action : menu->actions()) {
            if (action && action->text() == actionText) {
                if (triggered) {
                    *triggered = true;
                }
                action->trigger();
                break;
            }
        }

        menu->close();
    });
}
} // namespace

class TestPinWindowTextToolFormatting : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanup();

    void testTextStyleTogglesPropagateToEditor();
    void testFontDropdownSelectionsPropagateToEditor();
    void testFormattingUpdatesInlineEditorDuringEditing();
    void testFormattedFontAppliedToCreatedTextAnnotation();
    void testFormattingControlsSyncFromPersistedSettings();

private:
    QHash<QString, QVariant> m_savedTextSettings;
    QSet<QString> m_existingTextKeys;
};

void TestPinWindowTextToolFormatting::initTestCase()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screens available for PinWindow tests in this environment.");
    }
}

void TestPinWindowTextToolFormatting::init()
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

void TestPinWindowTextToolFormatting::cleanup()
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

void TestPinWindowTextToolFormatting::testTextStyleTogglesPropagateToEditor()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();

    AnnotationHostAdapter& host = window;
    auto* panel = host.toolOptionsPanelForContext();
    auto* editor = host.textAnnotationEditorForContext();
    QVERIFY(panel != nullptr);
    QVERIFY(editor != nullptr);

    QSignalSpy formattingSpy(editor, &TextAnnotationEditor::formattingChanged);

    const bool newBold = !editor->formatting().bold;
    const bool newItalic = !editor->formatting().italic;
    const bool newUnderline = !editor->formatting().underline;

    panel->setBold(newBold);
    panel->setItalic(newItalic);
    panel->setUnderline(newUnderline);

    const TextFormattingState formatting = editor->formatting();
    QCOMPARE(formatting.bold, newBold);
    QCOMPARE(formatting.italic, newItalic);
    QCOMPARE(formatting.underline, newUnderline);
    QCOMPARE(panel->isBold(), newBold);
    QCOMPARE(panel->isItalic(), newItalic);
    QCOMPARE(panel->isUnderline(), newUnderline);
    QVERIFY(formattingSpy.count() >= 3);
}

void TestPinWindowTextToolFormatting::testFontDropdownSelectionsPropagateToEditor()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();

    AnnotationHostAdapter& host = window;
    auto* panel = host.toolOptionsPanelForContext();
    auto* editor = host.textAnnotationEditorForContext();
    QVERIFY(panel != nullptr);
    QVERIFY(editor != nullptr);

    bool sizeSelectionTriggered = false;
    queueMenuActionSelection(QStringLiteral("24"), &sizeSelectionTriggered);
    host.onContextFontSizeDropdownRequested(QPoint(8, 8));

    QVERIFY(sizeSelectionTriggered);
    QCOMPARE(editor->formatting().fontSize, 24);
    QCOMPARE(panel->fontSize(), 24);

    const QString targetFamily = QStringLiteral("Courier New");
    bool familySelectionTriggered = false;
    queueMenuActionSelection(targetFamily, &familySelectionTriggered);
    host.onContextFontFamilyDropdownRequested(QPoint(8, 8));

    QVERIFY(familySelectionTriggered);
    QCOMPARE(editor->formatting().fontFamily, targetFamily);
    QCOMPARE(panel->fontFamily(), targetFamily);
}

void TestPinWindowTextToolFormatting::testFormattingUpdatesInlineEditorDuringEditing()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();

    AnnotationHostAdapter& host = window;
    auto* panel = host.toolOptionsPanelForContext();
    auto* editor = host.textAnnotationEditorForContext();
    auto* inlineEditor = host.inlineTextEditorForContext();
    QVERIFY(panel != nullptr);
    QVERIFY(editor != nullptr);
    QVERIFY(inlineEditor != nullptr);

    editor->startEditing(QPoint(80, 80), window.rect(), Qt::yellow);
    QVERIFY(inlineEditor->isEditing());

    panel->setBold(false);
    panel->setItalic(true);
    panel->setUnderline(true);
    editor->setFontSize(28);
    editor->setFontFamily(QStringLiteral("Verdana"));

    const QFont font = inlineEditor->font();
    QCOMPARE(font.bold(), false);
    QCOMPARE(font.italic(), true);
    QCOMPARE(font.underline(), true);
    QCOMPARE(font.pointSize(), 28);
    QCOMPARE(font.family(), QStringLiteral("Verdana"));

    editor->cancelEditing();
}

void TestPinWindowTextToolFormatting::testFormattedFontAppliedToCreatedTextAnnotation()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();

    AnnotationHostAdapter& host = window;
    auto* panel = host.toolOptionsPanelForContext();
    auto* editor = host.textAnnotationEditorForContext();
    auto* layer = host.annotationLayerForContext();
    QVERIFY(panel != nullptr);
    QVERIFY(editor != nullptr);
    QVERIFY(layer != nullptr);

    panel->setBold(false);
    panel->setItalic(true);
    panel->setUnderline(true);
    editor->setFontSize(26);
    editor->setFontFamily(QStringLiteral("Tahoma"));

    editor->startEditing(QPoint(100, 100), window.rect(), Qt::red);
    const bool created = editor->finishEditing(QStringLiteral("PinWindow Text"),
                                               QPoint(100, 100),
                                               Qt::red);
    QVERIFY(created);
    QVERIFY(layer->itemCount() > 0);

    auto* textItem = dynamic_cast<TextBoxAnnotation*>(
        layer->itemAt(static_cast<int>(layer->itemCount()) - 1));
    QVERIFY(textItem != nullptr);

    const QFont font = textItem->font();
    QCOMPARE(font.bold(), false);
    QCOMPARE(font.italic(), true);
    QCOMPARE(font.underline(), true);
    QCOMPARE(font.pointSize(), 26);
    QCOMPARE(font.family(), QStringLiteral("Tahoma"));
}

void TestPinWindowTextToolFormatting::testFormattingControlsSyncFromPersistedSettings()
{
    auto settings = SnapTray::getSettings();
    settings.setValue(kTextBoldKey, false);
    settings.setValue(kTextItalicKey, true);
    settings.setValue(kTextUnderlineKey, true);
    settings.setValue(kTextSizeKey, 32);
    settings.setValue(kTextFamilyKey, QStringLiteral("Georgia"));

    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();

    AnnotationHostAdapter& host = window;
    auto* panel = host.toolOptionsPanelForContext();
    auto* editor = host.textAnnotationEditorForContext();
    QVERIFY(panel != nullptr);
    QVERIFY(editor != nullptr);

    const TextFormattingState formatting = editor->formatting();
    QCOMPARE(formatting.bold, false);
    QCOMPARE(formatting.italic, true);
    QCOMPARE(formatting.underline, true);
    QCOMPARE(formatting.fontSize, 32);
    QCOMPARE(formatting.fontFamily, QStringLiteral("Georgia"));

    QCOMPARE(panel->isBold(), false);
    QCOMPARE(panel->isItalic(), true);
    QCOMPARE(panel->isUnderline(), true);
    QCOMPARE(panel->fontSize(), 32);
    QCOMPARE(panel->fontFamily(), QStringLiteral("Georgia"));
}

QTEST_MAIN(TestPinWindowTextToolFormatting)
#include "tst_TextToolFormatting.moc"
