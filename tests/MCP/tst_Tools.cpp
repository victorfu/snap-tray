#include <QtTest>

#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJsonObject>
#include <QJsonValue>
#include <QScreen>
#include <QTemporaryDir>

#include "PinWindowManager.h"
#include "mcp/MCPTools.h"

using SnapTray::MCP::ToolCallContext;
using SnapTray::MCP::ToolCallResult;

class tst_Tools : public QObject
{
    Q_OBJECT

private slots:
    void captureScreenshot_succeedsWithOutputPath();
    void captureScreenshot_succeedsWithScreenAndRegion();
    void captureScreenshot_rejectsInvalidRegion();
    void captureScreenshot_rejectsNegativeScreen();
    void pinImage_requiresPath();
    void pinImage_requiresManager();
    void pinImage_requiresXAndYTogether();
    void pinImage_acceptsValidImageAndPosition();
    void shareUpload_requiresPath();
    void shareUpload_rejectsMissingFile();
};

namespace {

QString createTestImageFile(const QTemporaryDir& tempDir, const QString& fileName = QStringLiteral("input.png"))
{
    const QString imagePath = QDir(tempDir.path()).filePath(fileName);
    QImage image(32, 32, QImage::Format_ARGB32);
    image.fill(QColor(40, 160, 220));
    if (!image.save(imagePath, "PNG")) {
        return QString();
    }
    return imagePath;
}

} // namespace

void tst_Tools::captureScreenshot_succeedsWithOutputPath()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screen available in test environment.");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());

    ToolCallContext context;
    const QString outputPath = QDir(tempDir.path()).filePath("full.png");
    const ToolCallResult result = SnapTray::MCP::callTool(
        "capture_screenshot",
        QJsonObject{
            {"output_path", outputPath},
        },
        context);

    if (!result.success) {
        QSKIP(qPrintable(QString("Screen capture unavailable in this environment: %1").arg(result.errorMessage)));
    }

    QCOMPARE(result.output.value("file_path").toString(), outputPath);
    QVERIFY(QFileInfo::exists(outputPath));
    QVERIFY(result.output.value("width").toInt() > 0);
    QVERIFY(result.output.value("height").toInt() > 0);
}

void tst_Tools::captureScreenshot_succeedsWithScreenAndRegion()
{
    const QList<QScreen*> screens = QGuiApplication::screens();
    if (screens.isEmpty()) {
        QSKIP("No screen available in test environment.");
    }

    const QRect available = screens.first()->geometry();
    const int regionWidth = qMax(1, qMin(120, available.width()));
    const int regionHeight = qMax(1, qMin(90, available.height()));

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString outputPath = QDir(tempDir.path()).filePath("region.png");

    ToolCallContext context;
    const ToolCallResult result = SnapTray::MCP::callTool(
        "capture_screenshot",
        QJsonObject{
            {"screen", 0},
            {"region",
                QJsonObject{
                    {"x", 0},
                    {"y", 0},
                    {"width", regionWidth},
                    {"height", regionHeight},
                }},
            {"output_path", outputPath},
        },
        context);

    if (!result.success) {
        QSKIP(qPrintable(QString("Region capture unavailable in this environment: %1").arg(result.errorMessage)));
    }

    QCOMPARE(result.output.value("screen_index").toInt(), 0);
    QCOMPARE(result.output.value("width").toInt(), regionWidth);
    QCOMPARE(result.output.value("height").toInt(), regionHeight);
    QVERIFY(QFileInfo::exists(outputPath));
}

void tst_Tools::captureScreenshot_rejectsInvalidRegion()
{
    ToolCallContext context;
    const ToolCallResult result = SnapTray::MCP::callTool(
        "capture_screenshot",
        QJsonObject{
            {"region",
                QJsonObject{
                    {"x", 0},
                    {"y", 0},
                    {"width", 0},
                    {"height", 20},
                }},
        },
        context);

    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains("must be > 0"));
}

void tst_Tools::captureScreenshot_rejectsNegativeScreen()
{
    ToolCallContext context;
    const ToolCallResult result = SnapTray::MCP::callTool(
        "capture_screenshot",
        QJsonObject{
            {"screen", -1},
        },
        context);

    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains("non-negative"));
}

void tst_Tools::pinImage_requiresPath()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screen available in test environment.");
    }

    PinWindowManager pinWindowManager;
    ToolCallContext context;
    context.pinWindowManager = &pinWindowManager;
    const ToolCallResult result = SnapTray::MCP::callTool("pin_image", QJsonObject{}, context);

    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains("image_path"));
}

void tst_Tools::pinImage_requiresManager()
{
    ToolCallContext context;
    const ToolCallResult result = SnapTray::MCP::callTool(
        "pin_image",
        QJsonObject{
            {"image_path", "/tmp/not-exist.png"},
        },
        context);

    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains("PinWindowManager"));
}

void tst_Tools::pinImage_requiresXAndYTogether()
{
    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString imagePath = createTestImageFile(tempDir);
    QVERIFY(!imagePath.isEmpty());

    PinWindowManager pinWindowManager;
    ToolCallContext context;
    context.pinWindowManager = &pinWindowManager;

    const ToolCallResult result = SnapTray::MCP::callTool(
        "pin_image",
        QJsonObject{
            {"image_path", imagePath},
            {"x", 120},
        },
        context);

    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains("provided together"));
}

void tst_Tools::pinImage_acceptsValidImageAndPosition()
{
    if (QGuiApplication::screens().isEmpty()) {
        QSKIP("No screen available in test environment.");
    }

    QTemporaryDir tempDir;
    QVERIFY(tempDir.isValid());
    const QString imagePath = createTestImageFile(tempDir);
    QVERIFY(!imagePath.isEmpty());

    PinWindowManager pinWindowManager;
    ToolCallContext context;
    context.pinWindowManager = &pinWindowManager;

    const ToolCallResult result = SnapTray::MCP::callTool(
        "pin_image",
        QJsonObject{
            {"image_path", imagePath},
            {"x", 120},
            {"y", 80},
            {"center", false},
        },
        context);

    if (!result.success) {
        QSKIP(qPrintable(QString("Pin window creation unavailable in this environment: %1").arg(result.errorMessage)));
    }

    QVERIFY(result.output.value("accepted").toBool());
    QCOMPARE(result.output.value("x").toInt(), 120);
    QCOMPARE(result.output.value("y").toInt(), 80);
    pinWindowManager.closeAllWindows();
}

void tst_Tools::shareUpload_requiresPath()
{
    ToolCallContext context;
    const ToolCallResult result = SnapTray::MCP::callTool("share_upload", QJsonObject{}, context);

    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains("image_path"));
}

void tst_Tools::shareUpload_rejectsMissingFile()
{
    ToolCallContext context;
    const ToolCallResult result = SnapTray::MCP::callTool(
        "share_upload",
        QJsonObject{
            {"image_path", "/tmp/not-exist.png"},
        },
        context);

    QVERIFY(!result.success);
    QVERIFY(result.errorMessage.contains("not found"));
}

QTEST_MAIN(tst_Tools)
#include "tst_Tools.moc"
