#include <QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>
#include <QWindow>

#include "qml/RecordingPreviewBackend.h"

// User-facing discard paths (trash button, Esc, title-bar close) must ask
// before deleting the recording; Esc on the question keeps it.
class tst_RecordingPreviewDiscard : public QObject
{
    Q_OBJECT

private slots:
    void requestDiscardWaitsForConfirmation();
    void programmaticCloseStillDiscards();
    void titleBarCloseAsksInsteadOfDiscarding();
    void keyboardFlow();
    void applicationQuitCloseIsNotBlocked();

private:
    static QQuickView* findPreviewWindow();

};

QQuickView* tst_RecordingPreviewDiscard::findPreviewWindow()
{
    for (QWindow* window : QGuiApplication::topLevelWindows()) {
        if (window->isVisible() && window->title() == RecordingPreviewBackend::tr("Recording Preview")) {
            return qobject_cast<QQuickView*>(window);
        }
    }
    return nullptr;
}

void tst_RecordingPreviewDiscard::requestDiscardWaitsForConfirmation()
{
    RecordingPreviewBackend backend(QStringLiteral("temporary.mp4"));
    QSignalSpy discarded(&backend, &RecordingPreviewBackend::discardRequested);
    QSignalSpy closed(&backend, &RecordingPreviewBackend::closed);
    QSignalSpy pendingChanged(&backend, &RecordingPreviewBackend::discardConfirmationChanged);

    backend.requestDiscard();
    QVERIFY(backend.discardConfirmationPending());
    QCOMPARE(pendingChanged.count(), 1);
    QCOMPARE(discarded.count(), 0);
    QCOMPARE(closed.count(), 0);

    // A repeated request (double click, key repeat) is one question.
    backend.requestDiscard();
    QCOMPARE(pendingChanged.count(), 1);

    backend.cancelDiscard();
    QVERIFY(!backend.discardConfirmationPending());
    QCOMPARE(discarded.count(), 0);
    QCOMPARE(closed.count(), 0);

    backend.requestDiscard();
    backend.discard();
    QVERIFY(!backend.discardConfirmationPending());
    QCOMPARE(discarded.count(), 1);
    QCOMPARE(closed.count(), 1);
    QCOMPARE(closed.first().first().toBool(), false);

    // After closing, nothing reopens the question.
    backend.requestDiscard();
    QVERIFY(!backend.discardConfirmationPending());
}

void tst_RecordingPreviewDiscard::programmaticCloseStillDiscards()
{
    // SnapTray's own close (e.g. teardown) keeps the existing contract.
    RecordingPreviewBackend backend(QStringLiteral("temporary.mp4"));
    QSignalSpy discarded(&backend, &RecordingPreviewBackend::discardRequested);
    backend.close();
    QCOMPARE(discarded.count(), 1);
}

void tst_RecordingPreviewDiscard::titleBarCloseAsksInsteadOfDiscarding()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    QSignalSpy discarded(&backend, &RecordingPreviewBackend::discardRequested);
    QSignalSpy closed(&backend, &RecordingPreviewBackend::closed);
    backend.show();
    QQuickView* view = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((view = findPreviewWindow()) != nullptr, 5000);
    QVERIFY(QTest::qWaitForWindowExposed(view));

    // QWindow::close() goes through the platform window like the title-bar button.
    QVERIFY(!view->close());
    QVERIFY(view->isVisible());
    QVERIFY(backend.discardConfirmationPending());
    QCOMPARE(discarded.count(), 0);
    QCOMPARE(closed.count(), 0);

    auto* overlay = view->rootObject()->findChild<QQuickItem*>(QStringLiteral("discardConfirmation"));
    QVERIFY(overlay);
    QVERIFY(overlay->isVisible());

    backend.cancelDiscard();
    QVERIFY(!overlay->isVisible());
    QVERIFY(view->isVisible());

    // A confirmed discard closes the window once.
    backend.requestDiscard();
    backend.discard();
    QCOMPARE(discarded.count(), 1);
    QCOMPARE(closed.count(), 1);
}

void tst_RecordingPreviewDiscard::keyboardFlow()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    QSignalSpy discarded(&backend, &RecordingPreviewBackend::discardRequested);
    backend.show();
    QQuickView* view = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((view = findPreviewWindow()) != nullptr, 5000);
    QVERIFY(QTest::qWaitForWindowExposed(view));
    view->requestActivate();
    QVERIFY(QTest::qWaitForWindowActive(view));
    QTRY_VERIFY(view->activeFocusItem());

    // Esc asks; Esc again keeps the recording.
    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(backend.discardConfirmationPending());
    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(!backend.discardConfirmationPending());
    QCOMPARE(discarded.count(), 0);

    // Enter on the default button keeps the recording.
    QTest::keyClick(view, Qt::Key_Escape);
    QTest::keyClick(view, Qt::Key_Return);
    QVERIFY(!backend.discardConfirmationPending());
    QCOMPARE(discarded.count(), 0);

    // Tab moves to Discard; Enter confirms.
    QTest::keyClick(view, Qt::Key_Escape);
    QTest::keyClick(view, Qt::Key_Tab);
    QTest::keyClick(view, Qt::Key_Return);
    QCOMPARE(discarded.count(), 1);
}

void tst_RecordingPreviewDiscard::applicationQuitCloseIsNotBlocked()
{
    // QCoreApplication::quit() closes every window and stops if one refuses;
    // the preview must not cancel Exit. Deliver the app's Quit event to the
    // backend's filter without actually ending the test's event loop.
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    QSignalSpy closed(&backend, &RecordingPreviewBackend::closed);
    backend.show();
    QQuickView* view = nullptr;
    QTRY_VERIFY_WITH_TIMEOUT((view = findPreviewWindow()) != nullptr, 5000);
    QVERIFY(QTest::qWaitForWindowExposed(view));

    QSignalSpy discarded(&backend, &RecordingPreviewBackend::discardRequested);
    QEvent quit(QEvent::Quit);
    backend.eventFilter(qApp, &quit);
    QVERIFY(view->close());
    QVERIFY(!backend.discardConfirmationPending());
    QCOMPARE(closed.count(), 1);
    // Quitting is not discarding: the recording stays for recovery (#157).
    QCOMPARE(discarded.count(), 0);
}

QTEST_MAIN(tst_RecordingPreviewDiscard)
#include "tst_RecordingPreviewDiscard.moc"
