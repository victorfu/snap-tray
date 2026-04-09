#include <QtTest/QtTest>

#include "qml/BeautifyPanelBackend.h"
#include "qml/QmlOverlayManager.h"

#include <QQuickItem>
#include <QQuickView>
#include <QQmlContext>
#include <QSignalSpy>

class tst_BeautifyPanelQml : public QObject
{
    Q_OBJECT

private slots:
    void testDismissButton_ClickEmitsCloseRequested();
};

void tst_BeautifyPanelQml::testDismissButton_ClickEmitsCloseRequested()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("Beautify panel QML interaction test requires a real screen");
    }

    SnapTray::BeautifyPanelBackend backend;
    QSignalSpy closeSpy(&backend, &SnapTray::BeautifyPanelBackend::closeRequested);
    QVERIFY(closeSpy.isValid());

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

    const QPointF buttonCenter = dismissButton->mapToScene(
        QPointF(dismissButton->width() / 2.0, dismissButton->height() / 2.0));
    QTest::mouseClick(&view, Qt::LeftButton, Qt::NoModifier, buttonCenter.toPoint());

    QTRY_COMPARE(closeSpy.count(), 1);
}

QTEST_MAIN(tst_BeautifyPanelQml)
#include "tst_BeautifyPanelQml.moc"
