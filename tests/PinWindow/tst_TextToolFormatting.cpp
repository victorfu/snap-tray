#include <QtTest/QtTest>
#include <QAction>
#include <QApplication>
#include <QEnterEvent>
#include <QFontDatabase>
#include <QGuiApplication>
#include <QMenu>
#include <QPixmap>
#include <QQuickItem>
#include <QQuickView>
#include <QSet>
#include <QTimer>

#include "PinWindow.h"
#include "InlineTextEditor.h"
#include "annotation/AnnotationHostAdapter.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "qml/PinToolOptionsViewModel.h"
#include "qml/QmlWindowedToolbar.h"
#include "qml/QmlWindowedSubToolbar.h"
#include "pinwindow/EmojiPickerPopup.h"
#include "region/TextAnnotationEditor.h"
#include "settings/Settings.h"

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

QString pickAvailableCommonFont()
{
    const QStringList availableFamilies = QFontDatabase::families();
    const QStringList preferredFamilies = {
        QStringLiteral("Arial"),
        QStringLiteral("Helvetica"),
        QStringLiteral("Times New Roman"),
        QStringLiteral("Courier New"),
        QStringLiteral("Verdana"),
        QStringLiteral("Georgia"),
        QStringLiteral("Trebuchet MS"),
        QStringLiteral("Impact")
    };

    for (const QString& family : preferredFamilies) {
        if (availableFamilies.contains(family)) {
            return family;
        }
    }

    return QString();
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

QQuickView* findVisibleQuickView(const QRect& expectedGeometry)
{
    const auto windows = QGuiApplication::topLevelWindows();
    for (QWindow* window : windows) {
        auto* quickView = qobject_cast<QQuickView*>(window);
        if (quickView && quickView->isVisible() && quickView->geometry() == expectedGeometry) {
            return quickView;
        }
    }

    return nullptr;
}

QObject* findRootChild(QObject* rootObject, const QString& objectName)
{
    return rootObject ? rootObject->findChild<QObject*>(objectName) : nullptr;
}

QPoint styleButtonCenter(QObject* sectionObject)
{
    const int sectionX = qRound(sectionObject->property("x").toDouble());
    const int buttonWidth = sectionObject->property("buttonWidth").toInt();
    const int buttonHeight = sectionObject->property("buttonHeight").toInt();
    const int buttonTop = sectionObject->property("buttonTop").toInt();
    return QPoint(sectionX + buttonWidth / 2, buttonTop + buttonHeight / 2);
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
    void testReEditingSyncsTextColorToSubToolbar();
    void testFloatingUiHitTestingCoversToolbarSurfaces();
    void testCustomToolCursorRestoresAfterArrowOverride();
    void testAnnotationCursorReappliesToolCursorWhenHoverTargetUnchanged();
    void testEnterEventRestoresToolCursorAfterToolbarArrowOverride();
    void testArrowStyleMenuClosesOnOtherSubToolbarClick();
    void testLineStyleMenuClosesOnOtherSubToolbarClick();

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
    auto* editor = host.textAnnotationEditorForContext();
    QVERIFY(editor != nullptr);

    QSignalSpy formattingSpy(editor, &TextAnnotationEditor::formattingChanged);

    const bool newBold = !editor->formatting().bold;
    const bool newItalic = !editor->formatting().italic;
    const bool newUnderline = !editor->formatting().underline;

    editor->setBold(newBold);
    editor->setItalic(newItalic);
    editor->setUnderline(newUnderline);

    const TextFormattingState formatting = editor->formatting();
    QCOMPARE(formatting.bold, newBold);
    QCOMPARE(formatting.italic, newItalic);
    QCOMPARE(formatting.underline, newUnderline);
    QVERIFY(formattingSpy.count() >= 3);
}

void TestPinWindowTextToolFormatting::testFontDropdownSelectionsPropagateToEditor()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();

    AnnotationHostAdapter& host = window;
    auto* editor = host.textAnnotationEditorForContext();
    QVERIFY(editor != nullptr);

    bool sizeSelectionTriggered = false;
    queueMenuActionSelection(QStringLiteral("24"), &sizeSelectionTriggered);
    host.onContextFontSizeDropdownRequested(QPoint(8, 8));

    QVERIFY(sizeSelectionTriggered);
    QCOMPARE(editor->formatting().fontSize, 24);

    const QString targetFamily = pickAvailableCommonFont();
    bool familySelectionTriggered = false;
    queueMenuActionSelection(targetFamily.isEmpty() ? QStringLiteral("Default") : targetFamily,
                             &familySelectionTriggered);
    host.onContextFontFamilyDropdownRequested(QPoint(8, 8));

    QVERIFY(familySelectionTriggered);
    QCOMPARE(editor->formatting().fontFamily, targetFamily);
}

void TestPinWindowTextToolFormatting::testFormattingUpdatesInlineEditorDuringEditing()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();

    AnnotationHostAdapter& host = window;
    auto* editor = host.textAnnotationEditorForContext();
    auto* inlineEditor = host.inlineTextEditorForContext();
    QVERIFY(editor != nullptr);
    QVERIFY(inlineEditor != nullptr);

    editor->startEditing(QPoint(80, 80), window.rect(), Qt::yellow);
    QVERIFY(inlineEditor->isEditing());

    editor->setBold(false);
    editor->setItalic(true);
    editor->setUnderline(true);
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
    auto* editor = host.textAnnotationEditorForContext();
    auto* layer = host.annotationLayerForContext();
    QVERIFY(editor != nullptr);
    QVERIFY(layer != nullptr);

    editor->setBold(false);
    editor->setItalic(true);
    editor->setUnderline(true);
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
    auto* editor = host.textAnnotationEditorForContext();
    QVERIFY(editor != nullptr);

    const TextFormattingState formatting = editor->formatting();
    QCOMPARE(formatting.bold, false);
    QCOMPARE(formatting.italic, true);
    QCOMPARE(formatting.underline, true);
    QCOMPARE(formatting.fontSize, 32);
    QCOMPARE(formatting.fontFamily, QStringLiteral("Georgia"));
}

void TestPinWindowTextToolFormatting::testReEditingSyncsTextColorToSubToolbar()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.toggleToolbar();

    AnnotationHostAdapter& host = window;
    auto* editor = host.textAnnotationEditorForContext();
    auto* layer = host.annotationLayerForContext();
    QVERIFY(editor != nullptr);
    QVERIFY(layer != nullptr);
    QVERIFY(window.m_subToolbar != nullptr);
    auto* optionsVM = window.m_subToolbar->viewModel();
    QVERIFY(optionsVM != nullptr);

    const QColor textColor = QColor(QStringLiteral("#007AFF"));
    const QColor drawingColor = QColor(QStringLiteral("#FF3B30"));

    editor->startEditing(QPoint(100, 100), window.rect(), textColor);
    const bool created = editor->finishEditing(QStringLiteral("Re-edit color"),
                                               QPoint(100, 100),
                                               textColor);
    QVERIFY(created);
    QCOMPARE(layer->itemCount(), qsizetype(1));

    window.onColorSelected(drawingColor);
    optionsVM->setCurrentColor(drawingColor);
    QCOMPARE(optionsVM->currentColor(), drawingColor);

    editor->startReEditing(0, drawingColor);

    QCOMPARE(optionsVM->currentColor(), textColor);

    editor->cancelEditing();
}

void TestPinWindowTextToolFormatting::testFloatingUiHitTestingCoversToolbarSurfaces()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.toggleToolbar();
    QVERIFY(window.m_toolbar != nullptr);
    QTRY_VERIFY(window.m_toolbar->isVisible());
    QVERIFY(window.isGlobalPosOverFloatingUi(window.m_toolbar->geometry().center()));

    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));
    QVERIFY(window.m_subToolbar != nullptr);
    QTRY_VERIFY(window.m_subToolbar->isVisible());
    QVERIFY(window.isGlobalPosOverFloatingUi(window.m_subToolbar->geometry().center()));

    window.showEmojiPickerPopup();
    QVERIFY(window.m_emojiPickerPopup != nullptr);
    QTRY_VERIFY(window.m_emojiPickerPopup->isVisible());
    QVERIFY(window.isGlobalPosOverFloatingUi(window.m_emojiPickerPopup->frameGeometry().center()));

    QVERIFY(!window.isGlobalPosOverFloatingUi(window.frameGeometry().center()));
}

void TestPinWindowTextToolFormatting::testCustomToolCursorRestoresAfterArrowOverride()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.toggleToolbar();
    QVERIFY(window.m_toolbar != nullptr);
    QTRY_VERIFY(window.m_toolbar->isVisible());
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Eraser));

    QVERIFY(window.isAnnotationMode());
    const QCursor expectedCursor = window.cursor();
    QVERIFY(expectedCursor.shape() != Qt::ArrowCursor);
    QVERIFY(!expectedCursor.pixmap().isNull());

    const QPoint originalCursorPos = QCursor::pos();
    QCursor::setPos(window.m_toolbar->geometry().center());
    window.setCursor(Qt::ArrowCursor);
    QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);

    window.updateCursorForTool();

    const QCursor restoredCursor = window.cursor();
    QCOMPARE(restoredCursor.shape(), expectedCursor.shape());
    QCOMPARE(restoredCursor.hotSpot(), expectedCursor.hotSpot());
    QCOMPARE(restoredCursor.pixmap().cacheKey(), expectedCursor.pixmap().cacheKey());
    QCursor::setPos(originalCursorPos);
}

void TestPinWindowTextToolFormatting::testAnnotationCursorReappliesToolCursorWhenHoverTargetUnchanged()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.toggleToolbar();
    QVERIFY(window.m_toolbar != nullptr);
    QTRY_VERIFY(window.m_toolbar->isVisible());
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));

    const QPoint originalCursorPos = QCursor::pos();
    QCursor::setPos(window.m_toolbar->geometry().center());
    window.syncFloatingUiCursor();
    QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);

    window.updateAnnotationCursor(window.rect().center());

    QCOMPARE(window.cursor().shape(), Qt::CrossCursor);
    QCursor::setPos(originalCursorPos);
}

void TestPinWindowTextToolFormatting::testEnterEventRestoresToolCursorAfterToolbarArrowOverride()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.toggleToolbar();
    QVERIFY(window.m_toolbar != nullptr);
    QTRY_VERIFY(window.m_toolbar->isVisible());

    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));
    QVERIFY(window.isAnnotationMode());

    const QPoint originalCursorPos = QCursor::pos();
    QCursor::setPos(window.m_toolbar->geometry().center());
    window.syncFloatingUiCursor();
    QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);

    const QPoint localPos = window.rect().center();
    const QPoint globalPos = window.mapToGlobal(localPos);
    const QPointF localPosF(localPos);
    const QPointF globalPosF(globalPos);
    QEnterEvent enterEvent{localPosF, globalPosF, globalPosF};
    QCoreApplication::sendEvent(&window, &enterEvent);

    QCOMPARE(window.cursor().shape(), Qt::CrossCursor);
    QCursor::setPos(originalCursorPos);
}

void TestPinWindowTextToolFormatting::testArrowStyleMenuClosesOnOtherSubToolbarClick()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.toggleToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Arrow));

    QVERIFY(window.m_subToolbar != nullptr);
    QTRY_VERIFY(window.m_subToolbar->isVisible());

    QQuickView* subToolbarView = findVisibleQuickView(window.m_subToolbar->geometry());
    QVERIFY(subToolbarView != nullptr);
    QObject* arrowSection = findRootChild(subToolbarView->rootObject(), QStringLiteral("arrowStyleSection"));
    QVERIFY(arrowSection != nullptr);

    QTest::mouseClick(subToolbarView, Qt::LeftButton, Qt::NoModifier, styleButtonCenter(arrowSection));
    QTRY_VERIFY(arrowSection->property("dropdownOpen").toBool());

    QTest::mouseClick(subToolbarView, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTRY_VERIFY(!arrowSection->property("dropdownOpen").toBool());
}

void TestPinWindowTextToolFormatting::testLineStyleMenuClosesOnOtherSubToolbarClick()
{
    QPixmap pixmap(320, 240);
    pixmap.fill(Qt::white);
    PinWindow window(pixmap, QPoint(0, 0), nullptr, false);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    window.toggleToolbar();
    window.handleToolbarToolSelected(static_cast<int>(ToolId::Pencil));

    QVERIFY(window.m_subToolbar != nullptr);
    QTRY_VERIFY(window.m_subToolbar->isVisible());

    QQuickView* subToolbarView = findVisibleQuickView(window.m_subToolbar->geometry());
    QVERIFY(subToolbarView != nullptr);
    QObject* lineSection = findRootChild(subToolbarView->rootObject(), QStringLiteral("lineStyleSection"));
    QVERIFY(lineSection != nullptr);

    QTest::mouseClick(subToolbarView, Qt::LeftButton, Qt::NoModifier, styleButtonCenter(lineSection));
    QTRY_VERIFY(lineSection->property("dropdownOpen").toBool());

    QTest::mouseClick(subToolbarView, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTRY_VERIFY(!lineSection->property("dropdownOpen").toBool());
}

QTEST_MAIN(TestPinWindowTextToolFormatting)
#include "tst_TextToolFormatting.moc"
