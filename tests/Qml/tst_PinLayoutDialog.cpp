#include <QtTest>
#include <QQuickView>
#include <QQuickItem>
#include <QAccessible>
#include <QtQml/qqmlextensionplugin.h>
#include "pinwindow/RegionLayoutManager.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class TestPinLayoutDialog : public QObject {
    Q_OBJECT
private slots:
    void controlsAndOutputFollowModel() {
        RegionLayoutManager model;
        QVector<LayoutRegion> regions(2);
        for (int i = 0; i < 2; ++i) {
            regions[i].rect = QRect(i * 110, 0, 100, 100);
            regions[i].originalRect = regions[i].rect;
            regions[i].image = QImage(100, 100, QImage::Format_ARGB32);
            regions[i].image.fill(Qt::red);
        }
        model.enterLayoutMode(regions, {210, 100});
        QQuickView view;
        view.setInitialProperties({{"viewModel", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/SnapTrayQml/dialogs/PinLayoutDialog.qml"));
        QCOMPARE(view.status(), QQuickView::Ready);
        QVERIFY(view.rootObject());
        auto* mode = view.rootObject()->findChild<QQuickItem*>("layoutMode");
        auto* output = view.rootObject()->findChild<QQuickItem*>("layoutOutputSize");
        auto* apply = view.rootObject()->findChild<QQuickItem*>("layoutApply");
        QVERIFY(mode && output && apply);
        QVERIFY(QMetaObject::invokeMethod(mode, "activated", Q_ARG(int, 3)));
        QCOMPARE(model.arrangementMode(), 3);
        QCOMPARE(output->property("text").toString(), QStringLiteral("Output: 100 × 208 pixels"));
        auto* accessible = QAccessible::queryAccessibleInterface(apply);
        QVERIFY(accessible);
        QCOMPARE(accessible->role(), QAccessible::Button);
        QCOMPARE(accessible->text(QAccessible::Name), QStringLiteral("Apply"));
        QSignalSpy confirm(&model, &RegionLayoutManager::confirmRequested);
        accessible->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(confirm.count(), 1);
        model.setArrangementGap(10000);
        QVERIFY(!apply->isEnabled());
        accessible->actionInterface()->doAction(QAccessibleActionInterface::pressAction());
        QCOMPARE(confirm.count(), 1);
        model.setArrangementMode(0);
        QVERIFY(apply->isEnabled());
        QSignalSpy cancel(&model, &RegionLayoutManager::cancelRequested);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "closeRequested"));
        QCOMPARE(cancel.count(), 1);
        view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));
        if (qEnvironmentVariableIsSet("SNAPTRAY_LAYOUT_EVIDENCE")) {
            const QImage image = view.grabWindow();
            QVERIFY(image.save(qEnvironmentVariable("SNAPTRAY_LAYOUT_EVIDENCE")));
        }
    }
};
QTEST_MAIN(TestPinLayoutDialog)
#include "tst_PinLayoutDialog.moc"
