#include <QtTest/QtTest>

#include "beautify/FrameStyleLibrary.h"
#include "qml/BeautifyPanelBackend.h"
#include "qml/QmlBeautifyPanel.h"
#include "qml/QmlOverlayManager.h"
#include "settings/BeautifySettingsManager.h"
#include "settings/Settings.h"

#include <QAccessible>
#include <QQuickItem>
#include <QScreen>
#include <QScopeGuard>
#include <QQuickView>
#include <QQmlContext>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

// QmlBeautifyPanel keeps this gap to the screen edges.
constexpr int kScreenEdgeMargin = 10;

// Repeater delegates are visual children, not QObject children.
QQuickItem* findItem(QQuickItem* node, const QString& name)
{
    if (!node)
        return nullptr;
    if (node->objectName() == name)
        return node;
    for (QQuickItem* child : node->childItems()) {
        if (QQuickItem* found = findItem(child, name))
            return found;
    }
    return nullptr;
}

} // namespace

class tst_BeautifyPanelQml : public QObject
{
    Q_OBJECT

private slots:
    void testDismissButton_DoesNotOverlapDragArea();
    void testSliderBindingSurvivesReopen_data();
    void testSliderBindingSurvivesReopen();
    void testResultsComeFirst_CustomizeCollapsed();
    void testScreenSizedPanelPosition_data();
    void testScreenSizedPanelPosition();
    void testPositionNearRightEdge_data();
    void testPositionNearRightEdge();
    void testStyleCardAndShadowToggle();
    void testTallPanelScrollsOnShortScreen();
    void testCustomizeNearScreenBottomStaysOnScreen();
    void testSaveStyleByName();
    void testRenameFollowsTheStyleWhenAnotherIsDeleted();
    void testSaveExplainsAFullList();
};

void tst_BeautifyPanelQml::testSliderBindingSurvivesReopen_data()
{
    QTest::addColumn<QString>("rowName");
    QTest::addColumn<QByteArray>("property");
    QTest::addColumn<int>("movedValue");
    QTest::newRow("padding") << QString("beautifyPaddingSlider") << QByteArray("padding") << 80;
    QTest::newRow("corners") << QString("beautifyCornerSlider") << QByteArray("cornerRadius") << 14;
    QTest::newRow("blur") << QString("beautifyBlurSlider") << QByteArray("shadowBlur") << 24;
}

void tst_BeautifyPanelQml::testSliderBindingSurvivesReopen()
{
    QFETCH(QString, rowName);
    QFETCH(QByteArray, property);
    QFETCH(int, movedValue);
    SnapTray::QmlBeautifyPanel panel;
    BeautifySettings saved;
    saved.padding = 64;
    saved.cornerRadius = 8;
    saved.shadowBlur = 12;
    saved.shadowEnabled = true;
    panel.setSettings(saved);
    panel.showNear(QRect(100, 100, 400, 300));
    auto* view = qobject_cast<QQuickView*>(panel.window());
    QVERIFY(view && view->rootObject());
    auto* backend = panel.findChild<SnapTray::BeautifyPanelBackend*>();
    QVERIFY(backend);
    auto* row = view->rootObject()->findChild<QObject*>(rowName);
    QVERIFY(row);
    auto* slider = row->findChild<QObject*>(QStringLiteral("settingsSliderInput"));
    QVERIFY(slider);
    const int savedValue = backend->property(property.constData()).toInt();
    slider->setProperty("value", movedValue);
    QVERIFY(QMetaObject::invokeMethod(slider, "moved", Qt::DirectConnection));
    QCOMPARE(backend->property(property.constData()).toInt(), movedValue);
    backend->requestClose();
    QVERIFY(!panel.isVisible());
    panel.setSettings(saved);
    panel.showNear(QRect(100, 100, 400, 300));
    QCOMPARE(panel.window(), static_cast<QWindow*>(view));
    QCOMPARE(row->property("value").toInt(), savedValue);
    QCOMPARE(slider->property("value").toInt(), savedValue);
    QCOMPARE(backend->property(property.constData()).toInt(), savedValue);
}

void tst_BeautifyPanelQml::testDismissButton_DoesNotOverlapDragArea()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("Beautify panel QML interaction test requires a real screen");
    }

    SnapTray::BeautifyPanelBackend backend;

    QQuickView view(SnapTray::QmlOverlayManager::instance().engine(), nullptr);
    view.rootContext()->setContextProperty(QStringLiteral("beautifyBackend"), &backend);
    view.setResizeMode(QQuickView::SizeViewToRootObject);
    view.setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/panels/BeautifyPanel.qml")));

    if (view.status() != QQuickView::Ready) {
        const auto errors = view.errors();
        QStringList messages;
        for (const auto& error : errors) {
            messages.append(error.toString());
        }
        QFAIL(qPrintable(messages.join('\n')));
    }

    view.show();
    if (!QTest::qWaitForWindowExposed(&view)) {
        QSKIP("Beautify panel test window could not be exposed");
    }

    auto* rootItem = view.rootObject();
    QVERIFY(rootItem);

    auto* dismissButton = rootItem->findChild<QQuickItem*>(QStringLiteral("beautifyPanelDismissButton"));
    QVERIFY(dismissButton);
    QTRY_VERIFY(dismissButton->width() > 0.0);
    QTRY_VERIFY(dismissButton->height() > 0.0);

    auto* dragArea = rootItem->findChild<QQuickItem*>(QStringLiteral("beautifyPanelDragArea"));
    QVERIFY(dragArea);
    QTRY_VERIFY(dragArea->width() > 0.0);

    const QRectF dismissRect = dismissButton->mapRectToScene(
        QRectF(0.0, 0.0, dismissButton->width(), dismissButton->height()));
    const QRectF dragRect = dragArea->mapRectToScene(
        QRectF(0.0, 0.0, dragArea->width(), dragArea->height()));

    QVERIFY2(!dragRect.intersects(dismissRect),
             "Dismiss button hit area must stay outside the draggable title bar region.");
    QVERIFY2(dragRect.right() <= dismissRect.left(),
             "Drag area should end before the dismiss button begins.");
}

void tst_BeautifyPanelQml::testResultsComeFirst_CustomizeCollapsed()
{
    // #227: styles and backgrounds are visible; the pixel controls wait under
    // Customize, and opening it grows the panel without moving it off screen.
    SnapTray::QmlBeautifyPanel panel;
    panel.setSettings(BeautifySettings{});
    panel.showNear(QRect(100, 100, 400, 300));
    auto* view = qobject_cast<QQuickView*>(panel.window());
    QVERIFY(view && view->rootObject());
    QQuickItem* root = view->rootObject();

    for (int i = 0; i < 4; ++i) {
        auto* card = findItem(root, QStringLiteral("beautifyStyle%1").arg(i));
        QVERIFY(card);
        QVERIFY(card->isVisible());
    }
    for (int i = 0; i < 8; ++i) {
        QVERIFY(findItem(root, QStringLiteral("beautifyBackground%1").arg(i)));
    }
    auto* standard = findItem(root, QStringLiteral("beautifyStyle1"));
    QVERIFY(standard->property("chosen").toBool());
    auto* custom = findItem(root, QStringLiteral("beautifyCustomStyleCaption"));
    QVERIFY(custom);
    QVERIFY(!custom->isVisible());

    auto* content = findItem(root, QStringLiteral("beautifyCustomizeContent"));
    QVERIFY(content);
    QVERIFY(!content->isVisible());
    const qreal collapsed = root->height();

    root->setProperty("customizeExpanded", true);
    QVERIFY(content->isVisible());
    QTRY_VERIFY(root->height() > collapsed);
    QTRY_COMPARE(qreal(view->height()), root->height());
    // The panel moves up after it grows; on macOS the move lands a little later.
    if (QScreen* screen = view->screen()) {
        QTRY_VERIFY(view->geometry().bottom() <= screen->availableGeometry().bottom());
    }

    // Stays as left while the panel is reused.
    panel.hide();
    panel.showNear(QRect(100, 100, 400, 300));
    QVERIFY(root->property("customizeExpanded").toBool());
}

void tst_BeautifyPanelQml::testScreenSizedPanelPosition_data()
{
    QTest::addColumn<int>("extraSize");
    QTest::newRow("one-pixel-room") << -1;
    QTest::newRow("exact-fit") << 0;
    QTest::newRow("oversized") << 100;
}

void tst_BeautifyPanelQml::testScreenSizedPanelPosition()
{
    QFETCH(int, extraSize);
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QRect available = screen->availableGeometry();
    const QRect anchor(available.center(), QSize(1, 1));
    SnapTray::QmlBeautifyPanel panel;
    panel.setSettings(BeautifySettings{});
    panel.showNear(anchor);
    auto* view = qobject_cast<QQuickView*>(panel.window());
    QVERIFY(view && view->rootObject());
    QVERIFY(QTest::qWaitForWindowExposed(view));
    panel.hide();

    // Force the boundary sizes on every host, including large desktop screens.
    // Keep maxHeight unchanged on reopen so these dimensions survive until polish.
    const QSize insetSize = available.size()
        - QSize(2 * kScreenEdgeMargin, 2 * kScreenEdgeMargin)
        + QSize(extraSize, extraSize);
    view->rootObject()->setSize(insetSize);
    view->resize(insetSize);
    panel.showNear(anchor);
    const int remainingSpace = qMax(0, -extraSize);
    QCOMPARE(view->position(), available.topLeft()
        + QPoint(kScreenEdgeMargin, kScreenEdgeMargin + remainingSpace));

    // Dragging uses the whole available screen without the opening margin.
    const QSize dragSize = available.size() + QSize(extraSize, extraSize);
    view->rootObject()->setSize(dragSize);
    view->resize(dragSize);
    QVERIFY(QMetaObject::invokeMethod(&panel, "onDragStarted", Qt::DirectConnection));
    QVERIFY(QMetaObject::invokeMethod(&panel, "onDragMoved", Qt::DirectConnection,
                                     Q_ARG(double, 0.0), Q_ARG(double, 0.0)));
    QCOMPARE(view->position(), available.topLeft() + QPoint(remainingSpace, remainingSpace));
    QVERIFY(QMetaObject::invokeMethod(&panel, "onDragFinished", Qt::DirectConnection));
    QCOMPARE(view->y(), available.top() + kScreenEdgeMargin);
}

void tst_BeautifyPanelQml::testPositionNearRightEdge_data()
{
    QTest::addColumn<int>("overflow");
    QTest::newRow("fits-at-right-edge") << 0;
    QTest::newRow("one-pixel-overflow") << 1;
}

void tst_BeautifyPanelQml::testPositionNearRightEdge()
{
    QFETCH(int, overflow);
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QRect available = screen->availableGeometry();
    SnapTray::QmlBeautifyPanel panel;
    panel.setSettings(BeautifySettings{});
    const int panelWidth = panel.window()->width();
    constexpr int kAnchorGap = 8;
    if (available.width() < 2 * (panelWidth + kAnchorGap + kScreenEdgeMargin)) {
        QSKIP("Screen must fit the panel on either side of the anchor");
    }
    const int rightmost = available.right() + 1 - kScreenEdgeMargin - panelWidth;
    const QRect anchor(QPoint(rightmost - kAnchorGap + overflow, available.center().y()), QSize(1, 1));
    panel.showNear(anchor);
    QCOMPARE(panel.window()->x(), overflow == 0
        ? rightmost : anchor.left() - panelWidth - kAnchorGap);
}

void tst_BeautifyPanelQml::testStyleCardAndShadowToggle()
{
    SnapTray::QmlBeautifyPanel panel;
    panel.setSettings(BeautifySettings{});
    panel.showNear(QRect(100, 100, 400, 300));
    auto* view = qobject_cast<QQuickView*>(panel.window());
    QVERIFY(view && view->rootObject());
    QVERIFY(QTest::qWaitForWindowExposed(view));
    QQuickItem* root = view->rootObject();
    auto* backend = panel.findChild<SnapTray::BeautifyPanelBackend*>();
    QVERIFY(backend);

    auto* flat = findItem(root, QStringLiteral("beautifyStyle3"));
    QVERIFY(flat);
    const QPointF center = flat->mapToScene(QPointF(flat->width() / 2, flat->height() / 3));
    QTest::mouseClick(view, Qt::LeftButton, Qt::NoModifier, center.toPoint());
    QTRY_COMPARE(backend->currentStyle(), 3);
    QVERIFY(flat->property("chosen").toBool());
    QVERIFY(!backend->shadowEnabled());

    // Keyboard: Space on a focused card applies it.
    auto* compact = findItem(root, QStringLiteral("beautifyStyle0"));
    compact->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Space);
    QTRY_COMPARE(backend->currentStyle(), 0);
    // Return does the same.
    auto* standard = findItem(root, QStringLiteral("beautifyStyle1"));
    QVERIFY(standard);
    standard->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Return);
    QTRY_COMPARE(backend->currentStyle(), 1);
    compact->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Space);
    QTRY_COMPARE(backend->currentStyle(), 0);

    // Background swatches take Space and Return too.
    auto* swatch2 = findItem(root, QStringLiteral("beautifyBackground2"));
    auto* swatch5 = findItem(root, QStringLiteral("beautifyBackground5"));
    QVERIFY(swatch2 && swatch5);
    swatch2->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Space);
    QTRY_COMPARE(backend->currentPreset(), 2);
    swatch5->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Return);
    QTRY_COMPARE(backend->currentPreset(), 5);
    QCOMPARE(backend->currentStyle(), 0); // a background leaves the style alone

    // The shared switch now drives the shadow, and the blur slider follows it.
    auto* toggle = root->findChild<QObject*>(QStringLiteral("beautifyShadowToggle"));
    auto* blur = findItem(root, QStringLiteral("beautifyBlurSlider"));
    QVERIFY(toggle && blur);
    QVERIFY(toggle->property("checked").toBool());
    QVERIFY(blur->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(toggle, "toggled", Q_ARG(bool, false)));
    QVERIFY(!backend->shadowEnabled());
    QVERIFY(!toggle->property("checked").toBool());
    QVERIFY(!blur->isEnabled());
    // Compact without its shadow is no longer Compact.
    QCOMPARE(backend->currentStyle(), -1);
    auto* custom = findItem(root, QStringLiteral("beautifyCustomStyleCaption"));
    QVERIFY(custom);
    QTRY_VERIFY(custom->isVisible());
    QVERIFY(!compact->property("chosen").toBool());

    // A value no style has shows "Custom".
    backend->applyStyle(1);
    QTRY_VERIFY(!custom->isVisible());
    backend->setPadding(150);
    QTRY_VERIFY(custom->isVisible());

    // The Customize header opens and closes with Space and Return.
    auto* customize = findItem(root, QStringLiteral("beautifyCustomizeToggle"));
    QVERIFY(customize);
    const bool expanded = root->property("customizeExpanded").toBool();
    customize->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Space);
    QTRY_COMPARE(root->property("customizeExpanded").toBool(), !expanded);
    QTest::keyClick(view, Qt::Key_Return);
    QTRY_COMPARE(root->property("customizeExpanded").toBool(), expanded);
}

void tst_BeautifyPanelQml::testTallPanelScrollsOnShortScreen()
{
    // With Customize open the panel is taller than a 768 px laptop screen.
    // It must stop at the height it is given and scroll, so Copy/Save stay reachable.
    SnapTray::QmlBeautifyPanel panel;
    panel.setSettings(BeautifySettings{});
    panel.showNear(QRect(100, 100, 400, 300));
    auto* view = qobject_cast<QQuickView*>(panel.window());
    QVERIFY(view && view->rootObject());
    QQuickItem* root = view->rootObject();
    // showNear limits the panel to the anchor screen, less the edge margins.
    if (QScreen* screen = QGuiApplication::screenAt(QPoint(300, 250))) {
        QCOMPARE(root->property("maxHeight").toInt(),
                 screen->availableGeometry().height() - 2 * kScreenEdgeMargin);
    }
    const qreal collapsed = root->property("naturalHeight").toReal();
    QVERIFY(collapsed > 0);
    root->setProperty("customizeExpanded", true);
    // The Column lays out on the next polish.
    QTRY_VERIFY(root->property("naturalHeight").toReal() > collapsed + 200);
    const qreal natural = root->property("naturalHeight").toReal();
    root->setProperty("maxHeight", int(natural) - 200);
    QTRY_COMPARE(root->height(), natural - 200);
    auto* scroller = findItem(root, QStringLiteral("beautifyPanelScroller"));
    QVERIFY(scroller);
    QVERIFY(scroller->property("interactive").toBool());
    QCOMPARE(scroller->property("contentHeight").toReal(), natural);

    // Resizing the window must not reset the limit it was given.
    QTRY_COMPARE(qreal(view->height()), natural - 200);
    QCOMPARE(root->property("maxHeight").toInt(), int(natural) - 200);

    root->setProperty("customizeExpanded", false);
    QTRY_COMPARE(root->height(), collapsed);
    QVERIFY(!scroller->property("interactive").toBool());
}

void tst_BeautifyPanelQml::testCustomizeNearScreenBottomStaysOnScreen()
{
    // Anchored at the bottom of the screen, opening Customize grows the panel;
    // it moves up and keeps the edge margin.
    QScreen* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    const QRect available = screen->availableGeometry();
    SnapTray::QmlBeautifyPanel panel;
    panel.setSettings(BeautifySettings{});
    panel.showNear(QRect(available.left() + 50, available.bottom() - 120, 200, 100));
    auto* view = qobject_cast<QQuickView*>(panel.window());
    QVERIFY(view && view->rootObject());
    QVERIFY(QTest::qWaitForWindowExposed(view));
    QQuickItem* root = view->rootObject();
    const qreal collapsed = root->height();

    root->setProperty("customizeExpanded", true);
    QTRY_VERIFY(root->height() > collapsed);
    QTRY_COMPARE(qreal(view->height()), root->height());
    QTRY_COMPARE(view->geometry().bottom(), available.bottom() - kScreenEdgeMargin);
    QVERIFY(view->geometry().top() >= available.top() + kScreenEdgeMargin);

    // Leave room above the panel so top-edge fallback cannot hide an off-by-one
    // in keepOnScreen(), even when the expanded panel fills a short screen.
    const int reducedHeight = qMin(view->height() - 1, available.height() / 2);
    view->setY(available.bottom() - kScreenEdgeMargin);
    root->setProperty("maxHeight", reducedHeight);
    QTRY_COMPARE(view->height(), reducedHeight);
    QTRY_COMPARE(view->geometry().bottom(), available.bottom() - kScreenEdgeMargin);
}

void tst_BeautifyPanelQml::testSaveStyleByName()
{
    // #227: "Save style…" asks for a name inline; Esc cancels the name without
    // closing the panel; Return saves; the saved card can be deleted by key.
    auto settings = SnapTray::getSettings();
    settings.remove(QStringLiteral("beautify/customStyles"));
    settings.remove(QStringLiteral("beautify/frameStyles"));
    settings.sync();
    SnapTray::FrameStyleLibrary::instance().reload();
    auto cleanup = qScopeGuard([] {
        auto s = SnapTray::getSettings();
        s.remove(QStringLiteral("beautify/customStyles"));
        s.remove(QStringLiteral("beautify/frameStyles"));
        s.sync();
        SnapTray::FrameStyleLibrary::instance().reload();
    });

    SnapTray::QmlBeautifyPanel panel;
    BeautifySettings look;
    look.padding = 101; // no built-in style: "Custom"
    panel.setSettings(look);
    panel.showNear(QRect(100, 100, 400, 300));
    auto* view = qobject_cast<QQuickView*>(panel.window());
    QVERIFY(view && view->rootObject());
    QVERIFY(QTest::qWaitForWindowExposed(view));
    view->requestActivate();
    QQuickItem* root = view->rootObject();
    auto* backend = panel.findChild<SnapTray::BeautifyPanelBackend*>();
    QVERIFY(backend);

    auto* saveButton = findItem(root, QStringLiteral("beautifySaveStyleButton"));
    QVERIFY(saveButton && saveButton->isVisible());
    QVERIFY(QMetaObject::invokeMethod(root, "startNamingStyle"));
    QVERIFY(root->property("namingStyle").toBool());
    auto* input = findItem(root, QStringLiteral("beautifyStyleNameInput"));
    QVERIFY(input);
    QCOMPARE(input->property("text").toString(), QStringLiteral("My style 1"));
    QTRY_VERIFY(input->hasActiveFocus());

    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(!root->property("namingStyle").toBool());
    QVERIFY(panel.isVisible()); // Esc cancelled the name, not the panel
    QCOMPARE(backend->customStyleModel().size(), 0);

    QVERIFY(QMetaObject::invokeMethod(root, "startNamingStyle"));
    QTRY_VERIFY(input->hasActiveFocus());
    input->setProperty("text", QStringLiteral("Release notes"));
    QTest::keyClick(view, Qt::Key_Return);
    QVERIFY(!root->property("namingStyle").toBool());
    QCOMPARE(backend->customStyleModel().size(), 1);
    QCOMPARE(backend->currentCustomStyle(), 0);
    QVERIFY(!saveButton->isVisible()); // already a saved style

    QQuickItem* card = nullptr;
    QTRY_VERIFY((card = findItem(root, QStringLiteral("beautifySavedStyle0"))) != nullptr);
    QVERIFY(card->property("chosen").toBool());

    // Rename: F2 on the focused card opens the name field with its name.
    backend->setPadding(150);
    QVERIFY(backend->saveCustomStyle(QStringLiteral("Slides")));
    // A new model rebuilds the cards.
    QTRY_VERIFY(findItem(root, QStringLiteral("beautifySavedStyle1")) != nullptr);
    card = findItem(root, QStringLiteral("beautifySavedStyle0"));
    QVERIFY(card);
    card->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_F2);
    QVERIFY(root->property("namingStyle").toBool());
    QCOMPARE(root->property("renamingIndex").toInt(), 0);
    QCOMPARE(input->property("text").toString(), QStringLiteral("Release notes"));
    auto* save = findItem(root, QStringLiteral("beautifyStyleNameSave"));
    auto* taken = findItem(root, QStringLiteral("beautifyStyleNameTaken"));
    QVERIFY(save && taken);
    // Another style's name: refused, with a message, and Return keeps the field open.
    input->setProperty("text", QStringLiteral("slides"));
    QTRY_VERIFY(taken->isVisible());
    QVERIFY(!save->isEnabled());
    QTRY_VERIFY(input->hasActiveFocus());
    QTest::keyClick(view, Qt::Key_Return);
    QVERIFY(root->property("namingStyle").toBool());
    QCOMPARE(backend->customStyleModel().at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Release notes"));
    // Esc cancels the rename only.
    QTest::keyClick(view, Qt::Key_Escape);
    QVERIFY(!root->property("namingStyle").toBool());
    QVERIFY(panel.isVisible());
    // The pencil button opens it too; Return renames.
    auto* pencil = findItem(card, QStringLiteral("beautifyStyleRename"));
    QVERIFY(pencil);
    QAccessibleInterface* pencilIface = QAccessible::queryAccessibleInterface(pencil);
    QVERIFY(pencilIface && pencilIface->actionInterface());
    pencilIface->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
    QVERIFY(root->property("namingStyle").toBool());
    QTRY_VERIFY(input->hasActiveFocus());
    input->setProperty("text", QStringLiteral("Changelog"));
    QVERIFY(!taken->isVisible());
    QTest::keyClick(view, Qt::Key_Return);
    QVERIFY(!root->property("namingStyle").toBool());
    QCOMPARE(backend->customStyleModel().at(0).toMap().value(QStringLiteral("name")).toString(),
             QStringLiteral("Changelog"));
    // Focus returns to the renamed card.
    QTRY_VERIFY(findItem(root, QStringLiteral("beautifySavedStyle0"))
                && findItem(root, QStringLiteral("beautifySavedStyle0"))->hasActiveFocus());
    backend->deleteCustomStyle(1);

    QTRY_VERIFY(findItem(root, QStringLiteral("beautifySavedStyle1")) == nullptr);
    card = findItem(root, QStringLiteral("beautifySavedStyle0"));
    QVERIFY(card);
    card->forceActiveFocus();
    QTest::keyClick(view, Qt::Key_Delete);
    QCOMPARE(backend->customStyleModel().size(), 0);

    QTest::keyClick(view, Qt::Key_Escape); // now Esc closes the panel again
    QTRY_VERIFY(!panel.isVisible());
}

void tst_BeautifyPanelQml::testRenameFollowsTheStyleWhenAnotherIsDeleted()
{
    // Renaming B while A is deleted must still rename B, not the style that
    // moved into B's place; deleting B itself ends the rename.
    auto settings = SnapTray::getSettings();
    settings.remove(QStringLiteral("beautify/customStyles"));
    settings.remove(QStringLiteral("beautify/frameStyles"));
    settings.sync();
    SnapTray::FrameStyleLibrary::instance().reload();
    auto cleanup = qScopeGuard([] {
        auto s = SnapTray::getSettings();
        s.remove(QStringLiteral("beautify/customStyles"));
        s.remove(QStringLiteral("beautify/frameStyles"));
        s.sync();
        SnapTray::FrameStyleLibrary::instance().reload();
    });

    SnapTray::QmlBeautifyPanel panel;
    panel.setSettings(BeautifySettings{});
    panel.showNear(QRect(100, 100, 400, 300));
    auto* view = qobject_cast<QQuickView*>(panel.window());
    QVERIFY(view && view->rootObject());
    QQuickItem* root = view->rootObject();
    auto* backend = panel.findChild<SnapTray::BeautifyPanelBackend*>();
    QVERIFY(backend);
    for (const QString& name : {QStringLiteral("A"), QStringLiteral("B"), QStringLiteral("C")}) {
        QVERIFY(backend->saveCustomStyle(name));
    }
    auto* input = findItem(root, QStringLiteral("beautifyStyleNameInput"));
    QVERIFY(input);

    QVERIFY(QMetaObject::invokeMethod(root, "startRenamingStyle", Q_ARG(QVariant, 1)));
    QCOMPARE(root->property("renamingIndex").toInt(), 1);
    backend->deleteCustomStyle(0);
    QCOMPARE(root->property("renamingIndex").toInt(), 0);
    input->setProperty("text", QStringLiteral("B2"));
    QVariant ok;
    QVERIFY(QMetaObject::invokeMethod(root, "finishNamingStyle", Q_RETURN_ARG(QVariant, ok),
                                      Q_ARG(QVariant, true)));
    QVERIFY(ok.toBool());
    auto stored = BeautifySettingsManager::instance().loadFrameStyles();
    QCOMPARE(stored.size(), 2);
    QCOMPARE(stored.at(0).name, QStringLiteral("B2"));
    QCOMPARE(stored.at(1).name, QStringLiteral("C"));

    QVERIFY(QMetaObject::invokeMethod(root, "startRenamingStyle", Q_ARG(QVariant, 1)));
    QVERIFY(root->property("namingStyle").toBool());
    backend->deleteCustomStyle(1);
    QVERIFY(!root->property("namingStyle").toBool());
    QCOMPARE(root->property("renamingIndex").toInt(), -1);
}

void tst_BeautifyPanelQml::testSaveExplainsAFullList()
{
    // The shared list can fill up from another panel or the recording editor
    // while a name is typed here: Save is refused with the reason shown.
    auto settings = SnapTray::getSettings();
    settings.remove(QStringLiteral("beautify/customStyles"));
    settings.remove(QStringLiteral("beautify/frameStyles"));
    settings.sync();
    SnapTray::FrameStyleLibrary::instance().reload();
    auto cleanup = qScopeGuard([] {
        auto s = SnapTray::getSettings();
        s.remove(QStringLiteral("beautify/customStyles"));
        s.remove(QStringLiteral("beautify/frameStyles"));
        s.sync();
        SnapTray::FrameStyleLibrary::instance().reload();
    });

    SnapTray::QmlBeautifyPanel panel;
    panel.setSettings(BeautifySettings{});
    panel.showNear(QRect(100, 100, 400, 300));
    auto* view = qobject_cast<QQuickView*>(panel.window());
    QVERIFY(view && view->rootObject());
    QQuickItem* root = view->rootObject();
    auto* input = findItem(root, QStringLiteral("beautifyStyleNameInput"));
    auto* save = findItem(root, QStringLiteral("beautifyStyleNameSave"));
    auto* full = findItem(root, QStringLiteral("beautifyStyleNameFull"));
    QVERIFY(input && save && full);

    QVERIFY(QMetaObject::invokeMethod(root, "startNamingStyle"));
    QVERIFY(root->property("namingStyle").toBool());
    QVERIFY(!full->isVisible());
    auto& library = SnapTray::FrameStyleLibrary::instance();
    for (int i = 0; i < BeautifySettingsManager::kMaxCustomStyles; ++i) {
        QVERIFY(library.save(QStringLiteral("Elsewhere %1").arg(i), SnapTray::FrameStyle()));
    }
    input->setProperty("text", QStringLiteral("Mine"));
    QTRY_VERIFY(full->isVisible());
    QVERIFY(!save->isEnabled());
    QVariant ok;
    QVERIFY(QMetaObject::invokeMethod(root, "finishNamingStyle", Q_RETURN_ARG(QVariant, ok),
                                      Q_ARG(QVariant, true)));
    QVERIFY(!ok.toBool());
    QVERIFY(root->property("namingStyle").toBool()); // still open, with the reason
    QCOMPARE(library.count(), BeautifySettingsManager::kMaxCustomStyles);

    // A saved name updates that style, which still fits.
    input->setProperty("text", QStringLiteral("elsewhere 3"));
    QTRY_VERIFY(!full->isVisible());
    QVERIFY(save->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(root, "finishNamingStyle", Q_RETURN_ARG(QVariant, ok),
                                      Q_ARG(QVariant, true)));
    QVERIFY(ok.toBool());
    QVERIFY(!root->property("namingStyle").toBool());
}

QTEST_MAIN(tst_BeautifyPanelQml)
#include "tst_BeautifyPanelQml.moc"
