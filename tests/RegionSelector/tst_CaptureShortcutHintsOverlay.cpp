#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QRegularExpression>
#include <QTranslator>

#include "tst_CaptureShortcutHintsOverlay.h"
#include "region/CaptureShortcutHintsOverlay.h"

namespace {

QString translationFilePath()
{
    return QDir(QString::fromUtf8(SNAPTRAY_TEST_TRANSLATION_DIR))
        .filePath(QStringLiteral("snaptray_zh_TW.qm"));
}

QString translationsDirPath()
{
    return QDir(QString::fromUtf8(SNAPTRAY_SOURCE_DIR))
        .filePath(QStringLiteral("translations"));
}

QString captureShortcutHintsOverlaySourcePath()
{
    return QDir(QString::fromUtf8(SNAPTRAY_SOURCE_DIR))
        .filePath(QStringLiteral("src/region/CaptureShortcutHintsOverlay.cpp"));
}

QString shortcutHintsViewModelSourcePath()
{
    return QDir(QString::fromUtf8(SNAPTRAY_SOURCE_DIR))
        .filePath(QStringLiteral("src/qml/ShortcutHintsViewModel.cpp"));
}

QString readTextFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        return {};
    }

    return QString::fromUtf8(file.readAll());
}

QStringList expectedHintSourceTexts()
{
    return {
        QStringLiteral("Cancel capture"),
        QStringLiteral("Confirm selection (after selection)"),
        QStringLiteral("Replay capture history"),
        QStringLiteral("Toggle multi-region mode"),
        QStringLiteral("Switch RGB/HEX (when magnifier visible)"),
        QStringLiteral("Copy color value (before selection)"),
        QStringLiteral("Move selection by 1 pixel (after selection)"),
        QStringLiteral("Resize selection by 1 pixel (after selection)")
    };
}

QStringList extractNoopStrings(const QString& sourcePath)
{
    const QString contents = readTextFile(sourcePath);
    if (contents.isEmpty()) {
        return {};
    }

    const QRegularExpression pattern(
        QStringLiteral(
            R"rx(QT_TRANSLATE_NOOP\("CaptureShortcutHintsOverlay",\s*"([^"]+)"\))rx"));

    QStringList matches;
    QRegularExpressionMatchIterator it = pattern.globalMatch(contents);
    while (it.hasNext()) {
        matches.append(it.next().captured(1));
    }
    return matches;
}

} // namespace

void TestCaptureShortcutHintsOverlay::testRuntimeTranslationLookup()
{
    QTranslator translator;
    QVERIFY2(translator.load(translationFilePath()),
             qPrintable(QStringLiteral("Failed to load translation file: %1")
                            .arg(translationFilePath())));
    QVERIFY(QCoreApplication::installTranslator(&translator));

    const QStringList expectedSources = expectedHintSourceTexts();
    const QStringList expectedDescriptions{
        QString::fromUtf8("取消擷取"),
        QString::fromUtf8("確認選取範圍（完成選取後）"),
        QString::fromUtf8("重播截圖歷程"),
        QString::fromUtf8("切換多區域模式"),
        QString::fromUtf8("切換 RGB/HEX（顯示放大鏡時）"),
        QString::fromUtf8("複製顏色值（選取前）"),
        QString::fromUtf8("移動選取範圍 1 像素（完成選取後）"),
        QString::fromUtf8("調整選取範圍大小 1 像素（完成選取後）")
    };

    QCOMPARE(expectedSources.size(), expectedDescriptions.size());
    for (int i = 0; i < expectedSources.size(); ++i) {
        QCOMPARE(QCoreApplication::translate("CaptureShortcutHintsOverlay",
                                             qPrintable(expectedSources.at(i))),
                 expectedDescriptions.at(i));
    }

    QCoreApplication::removeTranslator(&translator);
}

void TestCaptureShortcutHintsOverlay::testTranslationSourcesPresentInAllCatalogs()
{
    const QDir translationsDir(translationsDirPath());
    QVERIFY2(translationsDir.exists(),
             qPrintable(QStringLiteral("Translations directory not found: %1")
                            .arg(translationsDirPath())));

    const QStringList translationFiles =
        translationsDir.entryList(QStringList{QStringLiteral("snaptray_*.ts")}, QDir::Files);
    QVERIFY(!translationFiles.isEmpty());

    const QString contextMarker = QStringLiteral("<name>CaptureShortcutHintsOverlay</name>");
    const QStringList expectedSources = expectedHintSourceTexts();

    for (const QString& fileName : translationFiles) {
        const QString path = translationsDir.filePath(fileName);
        const QString contents = readTextFile(path);
        QVERIFY2(!contents.isEmpty(),
                 qPrintable(QStringLiteral("Failed to read translation source: %1").arg(path)));
        QVERIFY2(contents.contains(contextMarker),
                 qPrintable(QStringLiteral("Missing CaptureShortcutHintsOverlay context in %1")
                                .arg(path)));

        for (const QString& sourceText : expectedSources) {
            const QString sourceMarker =
                QStringLiteral("<source>%1</source>").arg(sourceText);
            QVERIFY2(contents.contains(sourceMarker),
                     qPrintable(QStringLiteral("Missing shortcut hint source '%1' in %2")
                                    .arg(sourceText, path)));
        }
    }
}

void TestCaptureShortcutHintsOverlay::testOverlayAndQmlHintSourcesStayInSync()
{
    const QStringList expectedSources = expectedHintSourceTexts();
    const QStringList overlaySources =
        extractNoopStrings(captureShortcutHintsOverlaySourcePath());
    const QStringList qmlSources =
        extractNoopStrings(shortcutHintsViewModelSourcePath());

    QCOMPARE(overlaySources, expectedSources);
    QCOMPARE(qmlSources, expectedSources);
}

void TestCaptureShortcutHintsOverlay::testRowCount()
{
    CaptureShortcutHintsOverlay overlay;
    QCOMPARE(overlay.rowCount(), 8);
}

void TestCaptureShortcutHintsOverlay::testRowCountWithoutMagnifierHints()
{
    CaptureShortcutHintsOverlay overlay;
    overlay.setMagnifierEnabled(false);
    QCOMPARE(overlay.rowCount(), 6);
}

void TestCaptureShortcutHintsOverlay::testLayoutMetrics()
{
    CaptureShortcutHintsOverlay overlay;
    const auto metrics = overlay.layoutMetrics();

    QVERIFY(metrics.keyColumnWidth > 0);
    QVERIFY(metrics.textColumnWidth > 0);
    QVERIFY(metrics.rowHeight > 0);
    QVERIFY(metrics.panelWidth > 0);
    QVERIFY(metrics.panelHeight > 0);
}

void TestCaptureShortcutHintsOverlay::testLayoutMetricsWithoutMagnifierHints()
{
    CaptureShortcutHintsOverlay overlay;
    overlay.setMagnifierEnabled(false);
    const auto metrics = overlay.layoutMetrics();

    QVERIFY(metrics.keyColumnWidth > 0);
    QVERIFY(metrics.textColumnWidth > 0);
    QVERIFY(metrics.rowHeight > 0);
    QVERIFY(metrics.panelWidth > 0);
    QVERIFY(metrics.panelHeight > 0);
}

void TestCaptureShortcutHintsOverlay::testPanelRectWithinViewport()
{
    CaptureShortcutHintsOverlay overlay;

    const QList<QSize> viewports{
        QSize(420, 280),
        QSize(900, 600),
        QSize(2560, 1440)
    };

    for (const QSize& viewport : viewports) {
        const QRect panelRect = overlay.panelRectForViewport(viewport);
        QVERIFY2(panelRect.isValid(), "Panel rect should be valid");
        QVERIFY2(panelRect.left() >= 0, "Panel rect should stay inside viewport");
        QVERIFY2(panelRect.top() >= 0, "Panel rect should stay inside viewport");
        QVERIFY2(panelRect.right() < viewport.width(), "Panel rect right edge should stay inside viewport");
        QVERIFY2(panelRect.bottom() < viewport.height(), "Panel rect bottom edge should stay inside viewport");
    }
}

void TestCaptureShortcutHintsOverlay::testRepaintRectCoversPanelEdges()
{
    CaptureShortcutHintsOverlay overlay;

    const QSize viewport(900, 600);
    const QRect panelRect = overlay.panelRectForViewport(viewport);
    const QRect repaintRect = overlay.repaintRectForViewport(viewport);

    QVERIFY2(panelRect.isValid(), "Panel rect should be valid");
    QVERIFY2(repaintRect.isValid(), "Repaint rect should be valid");
    QVERIFY2(repaintRect.contains(panelRect),
             "Repaint rect should include the logical panel rect");
    QVERIFY2(repaintRect.left() < panelRect.left(),
             "Repaint rect should cover antialiased pixels left of the panel");
    QVERIFY2(repaintRect.top() < panelRect.top(),
             "Repaint rect should cover antialiased pixels above the panel");
    QVERIFY2(repaintRect.right() > panelRect.right(),
             "Repaint rect should cover antialiased pixels right of the panel");
    QVERIFY2(repaintRect.bottom() > panelRect.bottom(),
             "Repaint rect should cover antialiased pixels below the panel");
    QVERIFY2(QRect(QPoint(), viewport).contains(repaintRect),
             "Repaint rect should stay inside the viewport");
}

QTEST_MAIN(TestCaptureShortcutHintsOverlay)
