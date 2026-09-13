#include <QtTest>
#include <QWindow>
#include <QQuickView>
#include <QQuickItem>
#include <QWidget>
#include <QTemporaryDir>
#include <QFile>
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
}

QTEST_MAIN(SnapTray::TestQmlDialogModality)
#include "tst_QmlDialogModality.moc"
