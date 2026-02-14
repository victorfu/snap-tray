#include <QtTest>

#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QTemporaryDir>

#include "cli/CaptureOutputHelper.h"

using SnapTray::CLI::CaptureMetadata;
using SnapTray::CLI::CaptureOutputOptions;
using SnapTray::CLI::CLIResult;
using SnapTray::CLI::emitCaptureOutput;

class tst_CaptureOutputHelper : public QObject
{
    Q_OBJECT

private slots:
    void emitCaptureOutput_rawReturnsPngData();
    void emitCaptureOutput_saveWritesPngFile();
};

static QPixmap makeScreenshot()
{
    QPixmap screenshot(8, 6);
    screenshot.fill(Qt::red);
    return screenshot;
}

void tst_CaptureOutputHelper::emitCaptureOutput_rawReturnsPngData()
{
    CaptureOutputOptions options;
    options.toRaw = true;

    const CLIResult result = emitCaptureOutput(makeScreenshot(), options, CaptureMetadata{});

    QCOMPARE(result.code, CLIResult::Code::Success);
    QVERIFY(result.message.isEmpty());
    QVERIFY(!result.data.isEmpty());

    static const QByteArray kPngSignature("\x89PNG\r\n\x1a\n", 8);
    QCOMPARE(result.data.left(8), kPngSignature);

    QImage decoded;
    QVERIFY(decoded.loadFromData(result.data, "PNG"));
    QCOMPARE(decoded.size(), QSize(8, 6));
}

void tst_CaptureOutputHelper::emitCaptureOutput_saveWritesPngFile()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    const QString outputPath = tempDir.path() + "/capture.png";

    CaptureOutputOptions options;
    options.outputFile = outputPath;

    const CLIResult result = emitCaptureOutput(makeScreenshot(), options, CaptureMetadata{});

    QCOMPARE(result.code, CLIResult::Code::Success);
    QVERIFY(result.message.contains(QString("Screenshot saved to: %1").arg(outputPath)));
    QVERIFY(result.data.isEmpty());

    QVERIFY(QFileInfo::exists(outputPath));
    QImage savedImage(outputPath);
    QVERIFY(!savedImage.isNull());
    QCOMPARE(savedImage.size(), QSize(8, 6));
}

QTEST_MAIN(tst_CaptureOutputHelper)
#include "tst_CaptureOutputHelper.moc"
