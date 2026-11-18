#include <QtTest>
#include <QQuickView>
#include <QWidget>
#include "qml/QmlDialog.h"
#include <QQuickItem>
#include <QQmlEngine>
#include <QtQml/qqmlextensionplugin.h>
#include "qml/DialogImageProvider.h"
#include "qml/RedactionReview.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

class TestRedactionReview : public QObject
{
    Q_OBJECT
private slots:
    void completion_data()
    {
        QTest::addColumn<bool>("accept");
        QTest::newRow("apply") << true;
        QTest::newRow("window-close") << false;
    }

    void completion()
    {
        QFETCH(bool, accept);
        QWidget owner;
        QImage input(100, 40, QImage::Format_RGB32);
        input.fill(Qt::white);
        const QVector<QRect> faces{{0, 0, 20, 20}};
        const QVector<QRect> text{{40, 0, 40, 20}};
        int calls = 0;
        auto* dialog = SnapTray::reviewRedactions(&owner, input, faces, text,
            [&](bool accepted, QVector<QRect> selectedFaces, QVector<QRect> selectedText) {
                ++calls;
                QCOMPARE(accepted, accept);
                QVERIFY(selectedFaces.isEmpty());
                QCOMPARE(selectedText, accept ? text : QVector<QRect>{});
            });
        auto* review = dialog->findChild<SnapTray::RedactionReview*>();
        QVERIFY(review);
        review->select(0, false);
        if (accept)
            emit review->apply();
        else
            dialog->close();
        QCOMPARE(calls, 1);
    }

    void selectionAndCancel()
    {
        const QVector<QRect> regions{{0, 0, 20, 30}, {30, 20, 30, 20}};
        QImage input(100, 100, QImage::Format_RGB32);
        input.fill(Qt::green);
        SnapTray::RedactionReview model(input, regions);
        QQuickView view;
        view.engine()->addImageProvider("dialog", new SnapTray::DialogImageProvider);
        view.setInitialProperties({{"viewModel", QVariant::fromValue(&model)}});
        view.setSource(QUrl("qrc:/SnapTrayQml/dialogs/RedactionReview.qml"));
        QCOMPARE(view.status(), QQuickView::Ready);
        QCOMPARE(model.thumbnails().size(), 2);
        model.select(0, false);
        QCOMPARE(model.selected(regions, 0), QVector<QRect>{regions[1]});
        QSignalSpy cancel(&model, &SnapTray::RedactionReview::cancel);
        QSignalSpy apply(&model, &SnapTray::RedactionReview::apply);
        QVERIFY(QMetaObject::invokeMethod(view.rootObject(), "closeRequested"));
        QCOMPARE(cancel.count(), 1);
        QCOMPARE(apply.count(), 0);
    }
};
QTEST_MAIN(TestRedactionReview)
#include "tst_RedactionReview.moc"
