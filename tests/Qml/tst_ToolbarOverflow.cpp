#include <QtTest>
#include <QQuickView>
#include <QQuickItem>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <QDir>
#include <QtQml/qqmlextensionplugin.h>
#include "qml/QmlWindowedToolbar.h"
#include "qml/ToolbarOverflowLayout.h"
#include "qml/PinToolbarViewModel.h"
#include "tools/ToolId.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace SnapTray {
class TestToolbarOverflow : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(".*Binding loop.*")); }
    void standaloneEngineCanImportModule();
    void layout_data();
    void layout();
    void safePositioning();
    void menuActionsAndLifetime();
};

void TestToolbarOverflow::standaloneEngineCanImportModule()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl("qrc:/SnapTrayQml/toolbar/WidthSection.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> item(component.create());
    QVERIFY(item);
}

void TestToolbarOverflow::layout_data()
{
    QTest::addColumn<int>("width");
    QTest::addColumn<bool>("ocr");
    for (int width : {1, 100, 300, 500, 640, 1920})
        for (bool ocr : {false, true})
            QTest::newRow(qPrintable(QString("%1-ocr-%2").arg(width).arg(ocr))) << width << ocr;
}

void TestToolbarOverflow::layout()
{
    QFETCH(int, width);
    QFETCH(bool, ocr);
    QmlWindowedToolbar toolbar;
    toolbar.viewModel()->setOCRAvailable(ocr);
    toolbar.ensureView();
    QVERIFY(toolbar.m_rootItem);
    QCOMPARE(toolbar.m_view->status(), QQuickView::Ready);
    const QRect bounds = toolbarUsableBounds(QRect(-width, -200, width, 400));
    toolbar.applyOverflowLayout(bounds);
    const QSize expectedSize(toolbar.m_rootItem->property("constrainedWidth").toInt(),
                             toolbar.m_rootItem->property("barHeight").toInt());
    // Native windows and QQuickView can finish resizing in follow-up events;
    // one processEvents() pass does not guarantee their geometry has settled.
    QCoreApplication::processEvents();
    QTRY_COMPARE(toolbar.m_view->size(), expectedSize);
    QCOMPARE(toolbar.m_rootItem->size(), QSizeF(expectedSize));
    QVERIFY2(toolbar.m_view->width() <= bounds.width(),
             qPrintable(QString("toolbar width %1 exceeds usable width %2")
                 .arg(toolbar.m_view->width()).arg(bounds.width())));
    QSet<int> expected;
    for (const auto& b : toolbar.viewModel()->buttons())
        if (ocr || !b.toMap().value("isOCR").toBool()) expected.insert(b.toMap().value("id").toInt());
    QSet<int> actual;
    for (const char* property : {"displayButtons", "overflowButtons"}) {
        for (const auto& b : toolbar.m_rootItem->property(property).toList()) {
            const int id = b.toMap().value("id").toInt();
            QVERIFY(!actual.contains(id));
            actual.insert(id);
        }
    }
    QCOMPARE(actual, expected);
    if (width >= 300) {
        QSet<int> visible;
        for (const auto& b : toolbar.m_rootItem->property("displayButtons").toList())
            visible.insert(b.toMap().value("id").toInt());
        QVERIFY(visible.contains(int(ToolId::Save)));
        QVERIFY(visible.contains(int(ToolId::Copy)));
        QVERIFY(visible.contains(PinToolbarViewModel::ButtonDone));
    }
    // Rendered buttons must fit the computed view, not merely its model.
    for (QQuickItem* item : toolbar.m_rootItem->findChildren<QQuickItem*>()) {
        if (!item->isVisible() || item->metaObject()->indexOfProperty("buttonId") < 0) continue;
        const QRectF geometry = item->mapRectToItem(toolbar.m_rootItem, item->boundingRect());
        QVERIFY2(geometry.left() >= 0 && geometry.right() <= toolbar.m_view->width(),
                  qPrintable(QString("button %1 outside width %2: %3..%4")
                      .arg(item->property("buttonId").toInt()).arg(toolbar.m_view->width())
                      .arg(geometry.left()).arg(geometry.right())));
    }
    toolbar.applyOverflowLayout(QRect(10, 10, 1900, 1000));
    const int expandedWidth = toolbar.m_rootItem->property("constrainedWidth").toInt();
    QCoreApplication::processEvents();
    QTRY_COMPARE(toolbar.m_view->width(), expandedWidth);
    QVERIFY(toolbar.m_rootItem->property("overflowButtons").toList().isEmpty());
    QVERIFY(toolbar.m_rootItem->property("showDragHandle").toBool());
}

void TestToolbarOverflow::safePositioning()
{
    for (const QRect screen : {QRect(0, 0, 640, 480), QRect(-500, -300, 500, 300),
                                QRect(0, 0, 1, 1), QRect()}) {
        const QRect bounds = toolbarUsableBounds(screen);
        for (const QSize size : {QSize(638, 32), QSize(2000, 900), QSize(28, 24)}) {
            const QPoint pos = boundToolbarPosition(QPoint(9000, -9000), size, bounds);
            if (bounds.isEmpty()) { QCOMPARE(pos, QPoint()); continue; }
            QVERIFY(pos.x() >= bounds.left());
            QVERIFY(pos.y() >= bounds.top());
            if (size.width() <= bounds.width()) QVERIFY(pos.x() + size.width() <= bounds.x() + bounds.width());
            if (size.height() <= bounds.height()) QVERIFY(pos.y() + size.height() <= bounds.y() + bounds.height());
        }
    }
}

void TestToolbarOverflow::menuActionsAndLifetime()
{
#ifdef Q_OS_MACOS
    if (QGuiApplication::platformName() == "offscreen") QSKIP("Native popup lifecycle requires Cocoa");
#endif
    QWidget host;
    host.resize(200, 100);
    host.show();
    QmlWindowedToolbar toolbar;
    toolbar.setAssociatedWidgets(&host, nullptr);
    toolbar.show();
    toolbar.applyOverflowLayout(QRect(10, 10, 100, 200));
    auto* more = toolbar.m_rootItem->findChild<QQuickItem*>("toolbarMoreButton");
    QVERIFY(more);
    QVERIFY(more->isVisible());
    QSignalSpy closed(&toolbar, &QmlWindowedToolbar::closeRequested);
    QSignalSpy undo(toolbar.viewModel(), &PinToolbarViewModel::undoClicked);
    toolbar.onOverflowRequested(8, 4, 28, 24);
    QVERIFY(toolbar.m_overflowRootItem);
    QCOMPARE(toolbar.m_overflowView->status(), QQuickView::Ready);
    QVERIFY(toolbar.m_overflowView->isVisible());
    QVERIFY(toolbar.m_overflowView->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
    QVERIFY(toolbar.m_view->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(toolbar.m_view, &leave);
    QVERIFY(toolbar.m_overflowView->isVisible());
    const auto buttons = toolbar.m_overflowRootItem->property("buttons").toList();
    int undoIndex = -1;
    for (int i = 0; i < buttons.size(); ++i)
        if (buttons[i].toMap().value("id").toInt() == int(ToolId::Undo)) undoIndex = i;
    QVERIFY(undoIndex >= 0);
    toolbar.m_overflowRootItem->setProperty("selectedIndex", undoIndex);
    QMetaObject::invokeMethod(toolbar.m_overflowRootItem, "activateSelected");
    QCOMPARE(undo.count(), 0);
    toolbar.viewModel()->setCanUndo(true);
    QMetaObject::invokeMethod(toolbar.m_overflowRootItem, "activateSelected");
    QCOMPARE(undo.count(), 1);
    QVERIFY(!toolbar.m_overflowView->isVisible());
    QCOMPARE(closed.count(), 0);
    toolbar.onOverflowRequested(8, 4, 28, 24);
    QTest::keyClick(&host, Qt::Key_Down);
    QVERIFY(toolbar.m_overflowRootItem->property("selectedIndex").toInt() >= 0);
    QTest::keyClick(&host, Qt::Key_Escape);
    QVERIFY(!toolbar.m_overflowView->isVisible());
    QCOMPARE(closed.count(), 0);
    toolbar.onOverflowRequested(8, 4, 28, 24);
    toolbar.m_overflowRootItem->setProperty("maximumHeight", 80);
    toolbar.m_overflowRootItem->setProperty("selectedIndex", buttons.size() - 2);
    QMetaObject::invokeMethod(toolbar.m_overflowRootItem, "moveSelection", Q_ARG(QVariant, 1));
    QCoreApplication::processEvents();
    auto* list = toolbar.m_overflowRootItem->findChild<QQuickItem*>("toolbarOverflowList");
    QVERIFY(list);
    QVERIFY(list->property("contentY").toReal() > 0);
    QSignalSpy done(toolbar.viewModel(), &PinToolbarViewModel::doneClicked);
    QTest::keyClick(&host, Qt::Key_Return);
    QCOMPARE(done.count(), 1);
    QCOMPARE(closed.count(), 0);
    toolbar.onOverflowRequested(8, 4, 28, 24);
    toolbar.hide();
    QVERIFY(!toolbar.m_overflowView->isVisible());

    const QString artifactDir = qEnvironmentVariable("SNAPTRAY_TEST_ARTIFACT_DIR");
    if (!artifactDir.isEmpty()) {
        QVERIFY(QDir().mkpath(artifactDir));
        toolbar.show();
        toolbar.applyOverflowLayout(QRect(10, 10, 480, 400));
        QTest::qWait(100);
        QVERIFY(toolbar.m_view->grabWindow().save(QDir(artifactDir).filePath("toolbar-500.png")));
        const QPointF anchor = more->mapToItem(toolbar.m_rootItem, QPointF());
        toolbar.onOverflowRequested(anchor.x(), anchor.y(), more->width(), more->height());
        QTest::qWait(100);
        QVERIFY(toolbar.m_overflowView->grabWindow().save(QDir(artifactDir).filePath("toolbar-overflow.png")));
    }
    toolbar.close();
    QVERIFY(!toolbar.overflowWindow());
}
}

QTEST_MAIN(SnapTray::TestToolbarOverflow)
#include "tst_ToolbarOverflow.moc"
