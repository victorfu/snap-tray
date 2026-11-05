#include <QtTest>
#include <QQuickView>
#include <QQuickItem>
#include <QPointer>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QApplication>
#include <QScreen>
#include <QWidget>
#include <QDir>
#include <QtQml/qqmlextensionplugin.h>
#include "qml/QmlWindowedToolbar.h"
#include "qml/ToolbarOverflowLayout.h"
#include "qml/ToolbarOverflowPopup.h"
#include "qml/PinToolbarViewModel.h"
#include "qml/QmlFloatingToolbar.h"
#include "qml/RegionToolbarViewModel.h"
#include "tools/ToolId.h"
#include "tools/ToolbarTraits.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace SnapTray {
class TestToolbarOverflow : public QObject
{
    Q_OBJECT
private slots:
    void init() { QTest::failOnWarning(QRegularExpression(".*Binding loop.*")); }
    void standaloneEngineCanImportModule();
    void textLabelsCreatedOnlyWhenNeeded();
    void layout_data();
    void layout();
    void safePositioning();
    void menuActionsAndLifetime();
    void collapsesByPriority();
    void activeToolStaysVisible();
    void groupGapCountsInWidth();
    void finishGroupIsTinted();
    void captureToolbarOverflows();
    void selectionPlacementStaysOnScreen_data();
    void selectionPlacementStaysOnScreen();
    void overflowClosesOnRelayout();
    void overflowReturnsFocusToMore();
};

void TestToolbarOverflow::standaloneEngineCanImportModule()
{
    QQmlEngine engine;
    QQmlComponent component(&engine, QUrl("qrc:/SnapTrayQml/toolbar/WidthSection.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> item(component.create());
    QVERIFY(item);
}

void TestToolbarOverflow::textLabelsCreatedOnlyWhenNeeded()
{
    QQmlEngine engine;
    QQuickWindow window;
    window.resize(100, 60);
    QQuickItem parent(window.contentItem());
    QQmlComponent component(&engine, QUrl("qrc:/SnapTrayQml/components/ToolbarButton.qml"));
    QVERIFY2(component.isReady(), qPrintable(component.errorString()));
    QScopedPointer<QObject> object(component.create());
    auto* button = qobject_cast<QQuickItem*>(object.data());
    QVERIFY(button);
    button->setParentItem(&parent);
    window.show();
    QTRY_VERIFY(window.isExposed());
    auto label = [button]() -> QQuickItem* {
        // Loader items belong to the visual tree, which need not match QObject ownership.
        QList<QQuickItem*> pending{button};
        while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item->objectName() == QStringLiteral("toolbarTextLabel")) {
                return item;
            }
            pending.append(item->childItems());
        }
        return nullptr;
    };
    QVERIFY(!label()); // Visible icon-only button.

    button->setVisible(false);
    button->setProperty("textLabel", QStringLiteral("\u22ef"));
    QVERIFY(!label()); // Hidden More button must not initialize text either.

    button->setProperty("iconNormalColor", QColor(Qt::red));
    button->setProperty("iconActiveColor", QColor(Qt::blue));
    parent.setVisible(false);
    button->setVisible(true);
    QVERIFY(!label()); // An initially hidden ancestor also defers creation.
    parent.setVisible(true);
    QTRY_VERIFY(label());
    QPointer<QQuickItem> originalLabel(label());
    QCOMPARE(label()->property("text").toString(), QStringLiteral("\u22ef"));
    QCOMPARE(label()->property("color").value<QColor>(), QColor(Qt::red));
    QTRY_VERIFY(label()->width() > 0);
    // Qt centers odd-sized text on whole pixels by default.
    constexpr qreal kCenterRoundingTolerance = 0.5;
    QTRY_VERIFY(qAbs(label()->mapToItem(button, label()->boundingRect().center()).x()
                     - button->boundingRect().center().x()) <= kCenterRoundingTolerance);
    QTRY_VERIFY(qAbs(label()->mapToItem(button, label()->boundingRect().center()).y()
                     - button->boundingRect().center().y()) <= kCenterRoundingTolerance);
    button->setProperty("isActive", true);
    QCOMPARE(label()->property("color").value<QColor>(), QColor(Qt::blue));
    button->setProperty("textLabel", QStringLiteral("More"));
    QCOMPARE(label()->property("text").toString(), QStringLiteral("More"));

    for (int i = 0; i < 3; ++i) {
        button->setVisible(false);
        QCoreApplication::processEvents();
        QVERIFY(originalLabel);
        QCOMPARE(label(), originalLabel.data());
        QVERIFY(!label()->isVisible());
        button->setVisible(true);
        QCOMPARE(label(), originalLabel.data());
        QVERIFY(label()->isVisible());

        parent.setVisible(false);
        QCoreApplication::processEvents();
        QVERIFY(originalLabel);
        QCOMPARE(label(), originalLabel.data());
        QVERIFY(!label()->isVisible());
        parent.setVisible(true);
        QCOMPARE(label(), originalLabel.data());
        QVERIFY(label()->isVisible());
    }
    QCOMPARE(label()->property("text").toString(), QStringLiteral("More"));
    QCOMPARE(label()->property("color").value<QColor>(), QColor(Qt::blue));
    QSignalSpy clicked(button, SIGNAL(clicked(int)));
    QVERIFY(clicked.isValid());
    const QPoint clickPosition = button->mapToScene(button->boundingRect().center()).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, clickPosition);
    QCOMPARE(clicked.count(), 1);
    button->setProperty("isDisabled", true);
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, clickPosition);
    QCOMPARE(clicked.count(), 1);
    button->setProperty("isDisabled", false);
    button->setProperty("textLabel", QString());
    QTRY_VERIFY(!label());
    QTRY_VERIFY(originalLabel.isNull());
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, clickPosition);
    QCOMPARE(clicked.count(), 2);
    button->setParentItem(nullptr);
}


static QVariantList fakeButtons()
{
    // id, group, priority — mirrors the Region Selector row.
    const QList<std::tuple<int, int, int>> spec = {
        {int(ToolId::Shape), 0, 999}, {int(ToolId::Arrow), 0, 998}, {int(ToolId::Text), 0, 996},
        {int(ToolId::Mosaic), 0, 70}, {int(ToolId::Eraser), 0, 60}, {int(ToolId::StepBadge), 0, 50},
        {int(ToolId::EmojiSticker), 0, 40}, {int(ToolId::Undo), 1, kNeverCollapse},
        {int(ToolId::Redo), 1, kNeverCollapse}, {int(ToolId::OCR), 1, 20},
        {int(ToolId::QRCode), 1, 10}, {int(ToolId::MultiRegion), 1, 30},
        {int(ToolId::Pin), 2, kNeverCollapse}, {int(ToolId::Save), 2, kNeverCollapse},
        {int(ToolId::Copy), 2, kNeverCollapse}};
    QVariantList out;
    int previous = -1;
    for (const auto& [id, group, priority] : spec) {
        out.append(QVariantMap{{"id", id}, {"group", group}, {"collapsePriority", priority},
                               {"groupStart", previous != -1 && previous != group},
                               {"separatorBefore", previous != -1 && previous != group}});
        previous = group;
    }
    return out;
}

static QList<int> ids(const QVariantList& list)
{
    QList<int> r;
    for (const auto& v : list) r.append(v.toMap().value("id").toInt());
    return r;
}

void TestToolbarOverflow::collapsesByPriority()
{
    const QVariantList buttons = fakeButtons();
    // One pixel short: More takes one button's room, so two collapse — QR then OCR,
    // not Copy and Save from the tail. Overflow keeps row order.
    auto layout = computeToolbarOverflow(buttons, toolbarContentWidth(buttons, true, false, {}) - 1, -1);
    QCOMPARE(ids(layout.overflowButtons), (QList<int>{int(ToolId::OCR), int(ToolId::QRCode)}));
    // Exactly four collapse: QR, OCR, Multi-Region, Emoji. Overflow keeps row order.
    QVariantList withoutFour;
    for (const auto& v : buttons) {
        const int id = v.toMap().value("id").toInt();
        if (id != int(ToolId::QRCode) && id != int(ToolId::OCR) && id != int(ToolId::MultiRegion)
            && id != int(ToolId::EmojiSticker))
            withoutFour.append(v);
    }
    const int width = toolbarContentWidth(restampGroupStarts(withoutFour), true, true, {});
    layout = computeToolbarOverflow(buttons, width, -1);
    QCOMPARE(ids(layout.overflowButtons),
             (QList<int>{int(ToolId::EmojiSticker), int(ToolId::OCR), int(ToolId::QRCode),
                         int(ToolId::MultiRegion)}));
    for (int id : {int(ToolId::Pin), int(ToolId::Save), int(ToolId::Copy), int(ToolId::Undo),
                   int(ToolId::Redo)})
        QVERIFY(ids(layout.visibleButtons).contains(id));
    QVERIFY(layout.width <= width);
}

void TestToolbarOverflow::activeToolStaysVisible()
{
    const QVariantList buttons = fakeButtons();
    const int width = toolbarContentWidth(buttons, true, false, {}) - 5 * 30;
    const auto layout = computeToolbarOverflow(buttons, width, int(ToolId::EmojiSticker));
    QVERIFY(ids(layout.visibleButtons).contains(int(ToolId::EmojiSticker)));
    QVERIFY(!ids(layout.overflowButtons).contains(int(ToolId::EmojiSticker)));
    QVERIFY(layout.width <= width);
}

void TestToolbarOverflow::groupGapCountsInWidth()
{
    const QVariantList buttons = fakeButtons();
    ToolbarLayoutMetrics m;
    const int withGap = toolbarContentWidth(buttons, true, false, m);
    m.groupGap = 0;
    QCOMPARE(withGap - toolbarContentWidth(buttons, true, false, m), 2 * ToolbarLayoutMetrics{}.groupGap);
}


void TestToolbarOverflow::finishGroupIsTinted()
{
    QmlWindowedToolbar toolbar;
    toolbar.ensureView();
    toolbar.applyOverflowLayout(QRect(10, 10, 1900, 1000));
    QCoreApplication::processEvents();
    QCOMPARE(toolbar.m_rootItem->property("groupGap").toInt(), ToolbarLayoutMetrics{}.groupGap);
    auto find = [&](int id) -> QQuickItem* {
        // Loader items live in the visual tree, which need not match QObject ownership.
        const QString name = QStringLiteral("toolbarFinishTint_%1").arg(id);
        QList<QQuickItem*> pending{toolbar.m_rootItem};
        while (!pending.isEmpty()) {
            auto* item = pending.takeLast();
            if (item->objectName() == name) return item;
            pending.append(item->childItems());
        }
        return nullptr;
    };
    QVERIFY(find(int(ToolId::Save)));
    QVERIFY(find(int(ToolId::Save))->isVisible());
    QVERIFY(find(int(ToolId::Arrow)));
    QVERIFY(!find(int(ToolId::Arrow))->isVisible());
}


void TestToolbarOverflow::captureToolbarOverflows()
{
    RegionToolbarViewModel vm;
    QmlFloatingToolbar toolbar(&vm);
    toolbar.applyOverflowLayout(QRect(0, 0, 300, 400));
    QCoreApplication::processEvents();
    QVERIFY(toolbar.m_view->width() <= 300);
    const QVariantList overflow = toolbar.m_rootItem->property("overflowButtons").toList();
    QVERIFY(ids(overflow).contains(int(ToolId::QRCode)));
    for (int id : {int(ToolId::Copy), int(ToolId::Save), int(ToolId::Pin)})
        QVERIFY(ids(toolbar.m_rootItem->property("displayButtons").toList()).contains(id));
}

void TestToolbarOverflow::selectionPlacementStaysOnScreen_data()
{
    QTest::addColumn<QRect>("selection");
    QTest::newRow("left-edge") << QRect(0, 200, 120, 80);
    QTest::newRow("right-edge") << QRect(1800, 200, 120, 80);
    QTest::newRow("bottom-edge") << QRect(600, 1000, 200, 80);
    QTest::newRow("tiny") << QRect(900, 500, 20, 20);
}

void TestToolbarOverflow::selectionPlacementStaysOnScreen()
{
    QFETCH(QRect, selection);
    const QRect viewport(0, 0, 1920, 1080);
    RegionToolbarViewModel vm;
    QmlFloatingToolbar toolbar(&vm);
    toolbar.applyOverflowLayout(viewport.adjusted(10, 10, -10, -10));
    const QSize size = toolbar.m_view->size();
    const QPoint topLeft = QmlFloatingToolbar::resolveTopLeftForSelection(
        selection, size, viewport, QmlFloatingToolbar::HorizontalAlignment::Center);
    QVERIFY(viewport.contains(QRect(topLeft, size)));
    // A narrow viewport (e.g. a portrait side monitor) still fits after collapsing.
    const QRect narrow(0, 0, 360, 800);
    toolbar.applyOverflowLayout(narrow.adjusted(10, 10, -10, -10));
    const QSize narrowSize = toolbar.m_view->size();
    QVERIFY(narrow.contains(QRect(QmlFloatingToolbar::resolveTopLeftForSelection(
        QRect(300, 300, 50, 50), narrowSize, narrow, QmlFloatingToolbar::HorizontalAlignment::Center),
        narrowSize)));
}

void TestToolbarOverflow::overflowClosesOnRelayout()
{
    RegionToolbarViewModel vm;
    QmlFloatingToolbar toolbar(&vm);
    toolbar.applyOverflowLayout(QRect(0, 0, 300, 400));
    toolbar.show();
    toolbar.onOverflowRequested(0, 0, 28, 24);
    QVERIFY(toolbar.m_overflow && toolbar.m_overflow->isVisible());
    toolbar.applyOverflowLayout(QRect(0, 0, 260, 400));
    QVERIFY(!toolbar.m_overflow->isVisible());
    toolbar.hide();
}

void TestToolbarOverflow::overflowReturnsFocusToMore()
{
    RegionToolbarViewModel vm;
    QmlFloatingToolbar toolbar(&vm);
    toolbar.applyOverflowLayout(QRect(0, 0, 300, 400));
    toolbar.show();
    toolbar.onOverflowRequested(0, 0, 28, 24);
    QVERIFY(toolbar.m_overflow->isVisible());
    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QVERIFY(toolbar.m_overflow->handleKey(&esc));
    QVERIFY(!toolbar.m_overflow->isVisible());
    auto* more = toolbar.m_rootItem->findChild<QQuickItem*>("toolbarMoreButton");
    QVERIFY(more);
    // The toolbar window never takes keyboard focus; More holds focus within it.
    QTRY_VERIFY(more->hasFocus());
    toolbar.hide();
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
    QTRY_VERIFY(toolbar.m_view->isExposed());
    QTest::mouseClick(toolbar.m_view, Qt::LeftButton, Qt::NoModifier,
                      more->mapToScene(more->boundingRect().center()).toPoint());
    QTRY_VERIFY(toolbar.m_overflow->m_rootItem);
    QCOMPARE(toolbar.m_overflow->m_view->status(), QQuickView::Ready);
    QVERIFY(toolbar.m_overflow->m_view->isVisible());
    QVERIFY(toolbar.m_overflow->m_view->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
    QVERIFY(toolbar.m_view->flags().testFlag(Qt::WindowDoesNotAcceptFocus));
    QEvent leave(QEvent::Leave);
    QApplication::sendEvent(toolbar.m_view, &leave);
    QVERIFY(toolbar.m_overflow->m_view->isVisible());
    const auto buttons = toolbar.m_overflow->m_rootItem->property("buttons").toList();
    int undoIndex = -1;
    for (int i = 0; i < buttons.size(); ++i)
        if (buttons[i].toMap().value("id").toInt() == int(ToolId::Undo)) undoIndex = i;
    QVERIFY(undoIndex >= 0);
    toolbar.m_overflow->m_rootItem->setProperty("selectedIndex", undoIndex);
    QMetaObject::invokeMethod(toolbar.m_overflow->m_rootItem, "activateSelected");
    QCOMPARE(undo.count(), 0);
    toolbar.viewModel()->setCanUndo(true);
    QMetaObject::invokeMethod(toolbar.m_overflow->m_rootItem, "activateSelected");
    QCOMPARE(undo.count(), 1);
    QVERIFY(!toolbar.m_overflow->m_view->isVisible());
    QCOMPARE(closed.count(), 0);
    toolbar.onOverflowRequested(8, 4, 28, 24);
    QTest::keyClick(&host, Qt::Key_Down);
    QVERIFY(toolbar.m_overflow->m_rootItem->property("selectedIndex").toInt() >= 0);
    QTest::keyClick(&host, Qt::Key_Escape);
    QVERIFY(!toolbar.m_overflow->m_view->isVisible());
    QCOMPARE(closed.count(), 0);
    toolbar.onOverflowRequested(8, 4, 28, 24);
    toolbar.m_overflow->m_rootItem->setProperty("maximumHeight", 80);
    toolbar.m_overflow->m_rootItem->setProperty("selectedIndex", buttons.size() - 2);
    QMetaObject::invokeMethod(toolbar.m_overflow->m_rootItem, "moveSelection", Q_ARG(QVariant, 1));
    QCoreApplication::processEvents();
    auto* list = toolbar.m_overflow->m_rootItem->findChild<QQuickItem*>("toolbarOverflowList");
    QVERIFY(list);
    QVERIFY(list->property("contentY").toReal() > 0);
    QSignalSpy done(toolbar.viewModel(), &PinToolbarViewModel::doneClicked);
    QTest::keyClick(&host, Qt::Key_Return);
    QCOMPARE(done.count(), 1);
    QCOMPARE(closed.count(), 0);
    toolbar.onOverflowRequested(8, 4, 28, 24);
    toolbar.hide();
    QVERIFY(!toolbar.m_overflow->m_view->isVisible());

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
        QVERIFY(toolbar.m_overflow->m_view->grabWindow().save(QDir(artifactDir).filePath("toolbar-overflow.png")));
    }
    toolbar.close();
    QVERIFY(!toolbar.overflowWindow());
}
}

QTEST_MAIN(SnapTray::TestToolbarOverflow)
#include "tst_ToolbarOverflow.moc"
