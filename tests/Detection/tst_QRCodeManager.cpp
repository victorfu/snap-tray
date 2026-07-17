#include <QtTest/QtTest>

#include <QPixmap>

#include "QRCodeManager.h"

class tst_QRCodeManager : public QObject
{
    Q_OBJECT

private slots:
    void decodesRgb888UsingRgbChannelOrder();
};

void tst_QRCodeManager::decodesRgb888UsingRgbChannelOrder()
{
    const QString payload = QStringLiteral("snaptray-rgb888-channel-order");

    QREncodeOptions options;
    options.width = 320;
    options.height = 320;
    options.margin = 4;
    // ZXing converts RGB to luminance with 0.299R + 0.587G + 0.114B.
    // These colors have strong contrast in RGB order, but equal luminance if
    // the red and blue channels are accidentally interpreted as BGR.
    options.foreground = QColor(0, 0, 255);
    options.background = QColor(255, 80, 0);

    QRCodeManager manager;
    const QImage qrCode = manager.encode(payload, options);
    QVERIFY(!qrCode.isNull());

    const QRDecodeResult result = manager.decodeSync(QPixmap::fromImage(qrCode));

    QVERIFY2(result.success, qPrintable(result.error));
    QCOMPARE(result.text, payload);
    QCOMPARE(result.format, QStringLiteral("QR_CODE"));
}

QTEST_MAIN(tst_QRCodeManager)
#include "tst_QRCodeManager.moc"
