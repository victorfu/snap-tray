#include <QtTest/QtTest>

#include "qml/BeautifyPanelBackend.h"
#include "qml/QmlBeautifyPanel.h"
#include "qml/QmlOverlayManager.h"

#include <QQuickItem>
#include <QScreen>
#include <QQuickView>
#include <QQmlContext>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {

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
    void testStyleCardAndShadowToggle();
    void testTallPanelScrollsOnShortScreen();
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
    if (QScreen* screen = view->screen()) {
        QVERIFY(view->geometry().bottom() <= screen->availableGeometry().bottom());
    }

    // Stays as left while the panel is reused.
    panel.hide();
    panel.showNear(QRect(100, 100, 400, 300));
    QVERIFY(root->property("customizeExpanded").toBool());
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

    // A value no style has shows "Custom".
    backend->setPadding(150);
    auto* custom = findItem(root, QStringLiteral("beautifyCustomStyleCaption"));
    QTRY_VERIFY(custom->isVisible());
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

    root->setProperty("customizeExpanded", false);
    QTRY_COMPARE(root->height(), collapsed);
    QVERIFY(!scroller->property("interactive").toBool());
}

QTEST_MAIN(tst_BeautifyPanelQml)
#include "tst_BeautifyPanelQml.moc"
