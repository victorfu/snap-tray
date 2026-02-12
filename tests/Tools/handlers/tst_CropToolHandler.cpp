#include <QtTest/QtTest>

#include "tools/handlers/CropToolHandler.h"

class TestCropToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void testClampToImage_EndpointInclusiveX();
    void testClampToImage_EndpointInclusiveY();
    void testClampToImage_BeyondEndpoint_RemainsEmpty();
};

void TestCropToolHandler::testClampToImage_EndpointInclusiveX()
{
    CropToolHandler handler;
    handler.setImageSize(QSize(100, 60));

    const QRect clamped = handler.clampToImage(QRect(QPoint(100, 10), QPoint(100, 40)).normalized());
    QCOMPARE(clamped, QRect(99, 10, 1, 31));
}

void TestCropToolHandler::testClampToImage_EndpointInclusiveY()
{
    CropToolHandler handler;
    handler.setImageSize(QSize(100, 60));

    const QRect clamped = handler.clampToImage(QRect(QPoint(20, 60), QPoint(50, 60)).normalized());
    QCOMPARE(clamped, QRect(20, 59, 31, 1));
}

void TestCropToolHandler::testClampToImage_BeyondEndpoint_RemainsEmpty()
{
    CropToolHandler handler;
    handler.setImageSize(QSize(100, 60));

    const QRect clamped = handler.clampToImage(QRect(QPoint(101, 10), QPoint(120, 40)).normalized());
    QVERIFY(clamped.isEmpty());
}

QTEST_MAIN(TestCropToolHandler)
#include "tst_CropToolHandler.moc"
