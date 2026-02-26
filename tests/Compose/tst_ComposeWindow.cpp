#include <QtTest/QtTest>

#include "compose/ComposeRichTextEdit.h"
#include "compose/ComposeWindow.h"

#include <QComboBox>
#include <QCoreApplication>
#include <QKeyEvent>
#include <QLineEdit>
#include <QSignalSpy>

class tst_ComposeWindow : public QObject
{
    Q_OBJECT

private slots:
    void testSystemWindowAndTemplateBehavior();
    void testCaptureImageInsertedIntoEditor();
    void testCtrlEnterCopiesHtml();
    void testCtrlShiftEnterCopiesMarkdown();
    void testEscapeCancels();
};

void tst_ComposeWindow::testSystemWindowAndTemplateBehavior()
{
    ComposeWindow window;
    QVERIFY(window.windowFlags().testFlag(Qt::Window));
    QVERIFY(!window.windowFlags().testFlag(Qt::FramelessWindowHint));

    auto* templateCombo = window.findChild<QComboBox*>(QStringLiteral("templateCombo"));
    auto* titleEdit = window.findChild<QLineEdit*>(QStringLiteral("titleEdit"));
    auto* editor = window.findChild<ComposeRichTextEdit*>(QStringLiteral("descriptionEdit"));

    QVERIFY(templateCombo);
    QVERIFY(titleEdit);
    QVERIFY(editor);

    const int bugIndex = templateCombo->findData(QStringLiteral("bug_report"));
    QVERIFY(bugIndex >= 0);
    templateCombo->setCurrentIndex(bugIndex);

    QVERIFY(titleEdit->placeholderText().contains(QStringLiteral("bug"), Qt::CaseInsensitive));
    QVERIFY(editor->toHtml().contains(QStringLiteral("Steps to Reproduce")));
}

void tst_ComposeWindow::testCaptureImageInsertedIntoEditor()
{
    ComposeWindow window;
    ComposeWindow::CaptureContext context;
    context.screenshot = QPixmap(120, 80);
    context.screenshot.fill(Qt::green);
    context.timestamp = QStringLiteral("2025-02-26T10:00:00");
    window.setCaptureContext(context);

    auto* editor = window.findChild<ComposeRichTextEdit*>(QStringLiteral("descriptionEdit"));
    QVERIFY(editor);
    QVERIFY(editor->toHtml().contains(QStringLiteral("data:image/png;base64,")));
    QVERIFY(editor->hasEmbeddedImage());
}

void tst_ComposeWindow::testCtrlEnterCopiesHtml()
{
    ComposeWindow window;
    QSignalSpy copiedSpy(&window, &ComposeWindow::composeCopied);

    QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier);
    QCoreApplication::sendEvent(&window, &keyEvent);

    QTRY_COMPARE(copiedSpy.count(), 1);
    QCOMPARE(copiedSpy.takeFirst().at(0).toString(), QStringLiteral("html"));
}

void tst_ComposeWindow::testCtrlShiftEnterCopiesMarkdown()
{
    ComposeWindow window;
    QSignalSpy copiedSpy(&window, &ComposeWindow::composeCopied);

    QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Return, Qt::ControlModifier | Qt::ShiftModifier);
    QCoreApplication::sendEvent(&window, &keyEvent);

    QTRY_COMPARE(copiedSpy.count(), 1);
    QCOMPARE(copiedSpy.takeFirst().at(0).toString(), QStringLiteral("markdown"));
}

void tst_ComposeWindow::testEscapeCancels()
{
    ComposeWindow window;
    QSignalSpy cancelSpy(&window, &ComposeWindow::composeCancelled);

    QKeyEvent keyEvent(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QCoreApplication::sendEvent(&window, &keyEvent);

    QTRY_COMPARE(cancelSpy.count(), 1);
}

QTEST_MAIN(tst_ComposeWindow)
#include "tst_ComposeWindow.moc"
