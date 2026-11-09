#include <QtTest>
#include <QWindow>
#include <QQuickView>
#include <QQuickItem>
#include <QWidget>
#include <QTemporaryDir>
#include <QFile>
#include <QScreen>
#include <QScopeGuard>
#include <QtQml/qqmlextensionplugin.h>
#include "qml/QmlDialog.h"
#include "qml/ScreenPickerViewModel.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace SnapTray {
class InputWindow : public QWindow
{
public:
    int mousePresses = 0;
    int keyPresses = 0;
protected:
    void mousePressEvent(QMouseEvent*) override { ++mousePresses; }
    void keyPressEvent(QKeyEvent*) override { ++keyPresses; }
};

class TestQmlDialogModality : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void modalityBlocksAndRestoresInput();
    void screenPickerPreservesParentAndContent();
    void growingDialogStaysOnScreen();
    void constrainedResizeKeepsCentering();
    void movedDialogKeepsUserPosition();
};

void TestQmlDialogModality::initTestCase()
{
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "offscreen") QSKIP("Cocoa modality needs native windows");
#endif
    if (QGuiApplication::screens().isEmpty()) QSKIP("No native screen available");
}

void TestQmlDialogModality::modalityBlocksAndRestoresInput()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile qml(dir.filePath("Dialog.qml"));
    QVERIFY(qml.open(QIODevice::WriteOnly));
    qml.write("import QtQuick\nRectangle { property var viewModel; width: 160; height: 100; color: 'white' }");
    qml.close();
    InputWindow background;
    background.setGeometry(30, 30, 200, 100);
    background.show();
    QVERIFY(QTest::qWaitForWindowExposed(&background));
    const auto clickAndType = [&] {
        QTest::mouseClick(&background, Qt::LeftButton, Qt::NoModifier, QPoint(20,20));
        QTest::keyClick(&background, Qt::Key_A);
        QCoreApplication::processEvents();
    };
    clickAndType();
    QCOMPARE(background.mousePresses, 1);
    QCOMPARE(background.keyPresses, 1);
    QPointer<QmlDialog> dialog = new QmlDialog(QUrl::fromLocalFile(qml.fileName()), new QObject, "viewModel");
    dialog->setModal(true);
    dialog->showAt(QPoint(280, 30));
    QCOMPARE(dialog->m_view->modality(), Qt::ApplicationModal);
    QVERIFY(QTest::qWaitForWindowExposed(dialog->m_view));
    QCOMPARE(QGuiApplication::modalWindow(), dialog->m_view);
    clickAndType();
    QCOMPARE(background.mousePresses, 1);
    QCOMPARE(background.keyPresses, 1);
    QSignalSpy closed(dialog, &QmlDialog::closed);
    dialog->setModal(false);
    QCOMPARE(dialog->m_view->modality(), Qt::NonModal);
    QVERIFY(QGuiApplication::modalWindow() != dialog->m_view);
    QCOMPARE(closed.count(), 0);
    clickAndType();
    QCOMPARE(background.mousePresses, 2);
    QCOMPARE(background.keyPresses, 2);
    dialog->setModal(true);
    clickAndType();
    QCOMPARE(background.mousePresses, 2);
    QCOMPARE(background.keyPresses, 2);
    dialog->close();
    QCOMPARE(closed.count(), 1);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(dialog.isNull());
    clickAndType();
    QCOMPARE(background.mousePresses, 3);
    QCOMPARE(background.keyPresses, 3);
}

void TestQmlDialogModality::screenPickerPreservesParentAndContent()
{
    QWidget host;
    host.resize(200,100);
    host.show();
    QVERIFY(QTest::qWaitForWindowExposed(&host));
    auto* model = new ScreenPickerViewModel;
    const QUrl source("qrc:/SnapTrayQml/dialogs/ScreenPickerDialog.qml");
    QPointer<QmlDialog> dialog = new QmlDialog(source, model, "viewModel", &host);
    dialog->setModal(true);
    dialog->showAt(QPoint(80,100));
    QCOMPARE(dialog->m_view->status(), QQuickView::Ready);
    QCOMPARE(dialog->m_view->modality(), Qt::ApplicationModal);
    QCOMPARE(dialog->m_view->transientParent(), host.windowHandle());
    const QRect geometry = dialog->m_view->geometry();
    QPointer<QQuickItem> content(dialog->m_view->rootObject());
    QSignalSpy closed(dialog, &QmlDialog::closed);
    dialog->setModal(false);
    dialog->setModal(true);
    QCOMPARE(closed.count(), 0);
    QCOMPARE(dialog->m_view->geometry(), geometry);
    QCOMPARE(dialog->m_view->rootObject(), content.data());
    QCOMPARE(dialog->m_viewModel.data(), model);
    QCOMPARE(dialog->m_view->transientParent(), host.windowHandle());
    dialog->close();
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    QVERIFY(dialog.isNull());
}

// Content that grows after the dialog is shown (the recording setup lays out
// and loads its devices late) must not push the bottom, with Start, off the
// screen.
void TestQmlDialogModality::growingDialogStaysOnScreen()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QRect available = screen->availableGeometry();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile qml(dir.filePath("Dialog.qml"));
    QVERIFY(qml.open(QIODevice::WriteOnly));
    qml.write("import QtQuick\nRectangle { property var viewModel; width: 200; height: 100; color: 'white' }");
    qml.close();
    QPointer<QmlDialog> dialog = new QmlDialog(QUrl::fromLocalFile(qml.fileName()), new QObject, "viewModel");
    auto cleanup = qScopeGuard([&dialog] {
        if (dialog) dialog->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    });
    const auto describe = [&dialog] {
        const QRect g = dialog->m_view->geometry();
        return QStringLiteral("%1,%2 %3x%4").arg(g.x()).arg(g.y()).arg(g.width()).arg(g.height());
    };
    dialog->showCenteredOnScreen(screen);
    QVERIFY(QTest::qWaitForWindowExposed(dialog->m_view));
    QQuickItem* root = dialog->m_view->rootObject();
    QVERIFY(root);

    // A centered dialog stays centered, on the screen, as it grows.
    const int tall = available.height() - 40;
    root->setHeight(tall);
    QTRY_COMPARE(dialog->m_view->height(), tall);
    QTRY_VERIFY2(available.contains(dialog->m_view->geometry()), qPrintable(describe()));
    QTRY_VERIFY2(qAbs(dialog->m_view->geometry().center().y() - available.center().y()) <= 1,
                 qPrintable(describe()));

    // A dialog placed at a point keeps its place, but is kept on the screen.
    root->setHeight(100);
    QTRY_COMPARE(dialog->m_view->height(), 100);
    const QPoint placed(available.left() + 20, available.bottom() - 150);
    dialog->showAt(placed);
    QTRY_COMPARE(dialog->m_view->position(), placed);
    root->setHeight(400);
    QTRY_COMPARE(dialog->m_view->height(), 400);
    QTRY_VERIFY2(available.contains(dialog->m_view->geometry()), qPrintable(describe()));
    QCOMPARE(dialog->m_view->x(), placed.x());
}

// Reproduce a WM delivering the constrained geometry before the resize signal,
// even on CI's Xvfb where there is no window manager.
void TestQmlDialogModality::constrainedResizeKeepsCentering()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile qml(dir.filePath("Dialog.qml"));
    QVERIFY(qml.open(QIODevice::WriteOnly));
    qml.write("import QtQuick\nRectangle { property var viewModel; width: 200; height: 100 }");
    qml.close();
    QmlDialog dialog(QUrl::fromLocalFile(qml.fileName()), new QObject, "viewModel");
    dialog.showCenteredOnScreen(screen);
    QVERIFY(QTest::qWaitForWindowExposed(dialog.m_view));
    const QSize grown(available.width() - 40, available.height() - 40);
    const QPoint constrained(available.right() - grown.width() + 1,
                             available.bottom() - grown.height() + 1);
    {
        const QSignalBlocker blocker(dialog.m_view);
        dialog.m_view->setGeometry(QRect(constrained, grown));
    }
    dialog.keepViewOnScreen();
    QCOMPARE(dialog.m_view->position(), QmlDialog::centeredTopLeftForBounds(available, grown));
    QCOMPARE(dialog.m_centeredOn.data(), screen);
}

void TestQmlDialogModality::movedDialogKeepsUserPosition()
{
    QScreen* screen = QGuiApplication::primaryScreen();
    const QRect available = screen->availableGeometry();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    QFile qml(dir.filePath("Dialog.qml"));
    QVERIFY(qml.open(QIODevice::WriteOnly));
    qml.write("import QtQuick\nRectangle { property var viewModel; width: 200; height: 100 }");
    qml.close();
    QmlDialog dialog(QUrl::fromLocalFile(qml.fileName()), new QObject, "viewModel");
    dialog.showCenteredOnScreen(screen);
    QVERIFY(QTest::qWaitForWindowExposed(dialog.m_view));
    // Keep the same x: after growth both the old centered position and the
    // user's position would be constrained to the very same bottom edge.
    const QPoint moved(dialog.m_view->x(), available.top() + 100);
    dialog.m_view->setPosition(moved);
    QTRY_COMPARE(dialog.m_view->position(), moved);
    QTRY_VERIFY(!dialog.m_centeredOn);
    const int tall = available.height() - 40;
    dialog.m_view->rootObject()->setHeight(tall);
    QTRY_COMPARE(dialog.m_view->height(), tall);
    const QPoint constrained(moved.x(), available.bottom() - tall + 1);
    QTRY_COMPARE(dialog.m_view->position(), constrained);
    QVERIFY(!dialog.m_centeredOn);
}

}

QTEST_MAIN(SnapTray::TestQmlDialogModality)
#include "tst_QmlDialogModality.moc"
