#include <QtTest/QtTest>

#include "history/HistoryStore.h"
#include "qml/HistoryModel.h"

#include <QImage>
#include <QSignalSpy>
#include <QTemporaryDir>

namespace {

QImage makeImage(const QSize& size, const QColor& color)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

} // namespace

class tst_HistoryModel : public QObject
{
    Q_OBJECT

private slots:
    void testRefreshAndRoles();
};

void tst_HistoryModel::testRefreshAndRoles()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    qputenv("SNAPTRAY_HISTORY_DIR", tempDir.path().toLocal8Bit());

    SnapTray::CaptureSessionWriteRequest firstRequest;
    firstRequest.canvasImage = makeImage(QSize(640, 360), Qt::black);
    firstRequest.resultImage = makeImage(QSize(320, 180), Qt::green);
    firstRequest.selectionRect = QRect(10, 20, 320, 180);
    firstRequest.annotationsJson = QByteArrayLiteral("{\"items\":[]}");
    firstRequest.devicePixelRatio = 2.0;
    firstRequest.canvasLogicalSize = QSize(320, 180);
    firstRequest.createdAt = QDateTime(QDate(2026, 1, 1), QTime(12, 0, 1, 0));
    QVERIFY(SnapTray::HistoryStore::writeCaptureSession(firstRequest).has_value());

    SnapTray::CaptureSessionWriteRequest secondRequest;
    secondRequest.canvasImage = makeImage(QSize(400, 240), Qt::darkBlue);
    secondRequest.resultImage = makeImage(QSize(200, 100), Qt::red);
    secondRequest.selectionRect = QRect(0, 0, 200, 100);
    secondRequest.annotationsJson = QByteArrayLiteral("{\"items\":[]}");
    secondRequest.devicePixelRatio = 2.0;
    secondRequest.canvasLogicalSize = QSize(200, 120);
    secondRequest.createdAt = QDateTime(QDate(2026, 1, 1), QTime(12, 0, 0, 0));
    QVERIFY(SnapTray::HistoryStore::writeCaptureSession(secondRequest).has_value());

    SnapTray::HistoryModel model;
    QCOMPARE(model.rowCount(), 2);

    const QModelIndex first = model.index(0, 0);
    QVERIFY(first.isValid());
    QCOMPARE(model.data(first, SnapTray::HistoryModel::CanEditRole).toBool(), true);
    QCOMPARE(model.data(first, SnapTray::HistoryModel::ReplayAvailableRole).toBool(), true);
    QCOMPARE(model.data(first, SnapTray::HistoryModel::ImageSizeRole).toSize(), QSize(160, 90));
    QVERIFY(model.data(first, SnapTray::HistoryModel::TooltipTextRole).toString().contains(QStringLiteral("Time:")));
    QVERIFY(model.data(first, SnapTray::HistoryModel::TooltipTextRole).toString().contains(QStringLiteral("Size:")));

    const QModelIndex second = model.index(1, 0);
    QVERIFY(second.isValid());
    QCOMPARE(model.data(second, SnapTray::HistoryModel::CanEditRole).toBool(), true);
    QCOMPARE(model.data(second, SnapTray::HistoryModel::ImageSizeRole).toSize(), QSize(100, 50));

    QSignalSpy countSpy(&model, &SnapTray::HistoryModel::countChanged);
    QVERIFY(countSpy.isValid());
    model.refresh();
    QVERIFY(countSpy.count() >= 1);
    QCOMPARE(model.rowCount(), 2);

    qunsetenv("SNAPTRAY_HISTORY_DIR");
}

QTEST_MAIN(tst_HistoryModel)
#include "tst_HistoryModel.moc"
