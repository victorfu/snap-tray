#include <QtTest/QtTest>

#include "qml/BeautifyPanelBackend.h"
#include "qml/QmlBeautifyPanel.h"
#include "qml/QmlOverlayManager.h"

#include <QQuickItem>
#include <QQuickView>
#include <QQmlContext>
#include <QSignalSpy>
#include <QtQml/qqmlextensionplugin.h>

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class tst_BeautifyPanelQml : public QObject
{
    Q_OBJECT

private slots:
    void testDismissButton_DoesNotOverlapDragArea();
    void testSliderBindingSurvivesReopen_data();
    void testSliderBindingSurvivesReopen();
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

QTEST_MAIN(tst_BeautifyPanelQml)
#include "tst_BeautifyPanelQml.moc"
