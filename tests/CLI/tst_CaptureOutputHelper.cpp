#include <QtTest>

#include <QFileInfo>
#include <QImage>
#include <QPixmap>
#include <QTemporaryDir>
#include <QApplication>
#include <QProcess>

#include "cli/CaptureOutputHelper.h"
#include "settings/Settings.h"

using SnapTray::CLI::CaptureMetadata;
using SnapTray::CLI::CaptureOutputOptions;
using SnapTray::CLI::CLIResult;
using SnapTray::CLI::emitCaptureOutput;

class tst_CaptureOutputHelper : public QObject
{
    Q_OBJECT

private slots:
    void emitCaptureOutput_rawReturnsPngData();
    void rawStdoutPreservesPngBytes();
    void emitCaptureOutput_saveWritesPngFile();
    void autoNamedSavesKeepBothImages();
    void explicitOutputStillOverwrites();
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

void tst_CaptureOutputHelper::rawStdoutPreservesPngBytes()
{
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("--raw-output-probe")});
    QVERIFY(child.waitForFinished(10000));
    QCOMPARE(child.exitStatus(), QProcess::NormalExit);
    QCOMPARE(child.exitCode(), 0);
    const QByteArray bytes = child.readAllStandardOutput();
    CaptureOutputOptions options;
    options.toRaw = true;
    QCOMPARE(bytes, emitCaptureOutput(makeScreenshot(), options).data);
    QImage decoded;
    QVERIFY(decoded.loadFromData(bytes, "PNG"));
    QCOMPARE(decoded.size(), QSize(8, 6));
    QCOMPARE(decoded.pixelColor(0, 0), QColor(Qt::red));
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

void tst_CaptureOutputHelper::autoNamedSavesKeepBothImages()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    auto settings = SnapTray::getSettings();
    settings.setValue("files/filenameTemplate", "same.png");
    settings.sync();
    CaptureOutputOptions options;
    options.savePath = dir.path();
    QPixmap red(8, 6), blue(8, 6);
    red.fill(Qt::red);
    blue.fill(Qt::blue);
    const auto a = emitCaptureOutput(red, options);
    const auto b = emitCaptureOutput(blue, options);
    QVERIFY(a.isSuccess());
    QVERIFY(b.isSuccess());
    QVERIFY(a.message.contains(dir.filePath("same.png")));
    QVERIFY(b.message.contains(dir.filePath("same_1.png")));
    QCOMPARE(QImage(dir.filePath("same.png")).pixelColor(0, 0), QColor(Qt::red));
    QCOMPARE(QImage(dir.filePath("same_1.png")).pixelColor(0, 0), QColor(Qt::blue));
}

void tst_CaptureOutputHelper::explicitOutputStillOverwrites()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    CaptureOutputOptions options;
    options.outputFile = dir.filePath("explicit.png");
    QPixmap red(8, 6), blue(8, 6);
    red.fill(Qt::red);
    blue.fill(Qt::blue);
    QVERIFY(emitCaptureOutput(red, options).isSuccess());
    QVERIFY(emitCaptureOutput(blue, options).isSuccess());
    QCOMPARE(QImage(options.outputFile).pixelColor(0, 0), QColor(Qt::blue));
}

int main(int argc, char** argv)
{
    QApplication app(argc, argv);
    if (app.arguments().contains(QStringLiteral("--raw-output-probe"))) {
        CaptureOutputOptions options;
        options.toRaw = true;
        const auto result = emitCaptureOutput(makeScreenshot(), options);
        return result.isSuccess() && SnapTray::CLI::writeRawOutputToStdout(result.data) ? 0 : 1;
    }
    tst_CaptureOutputHelper test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_CaptureOutputHelper.moc"
