#include <QtTest/QtTest>
#include <QtQml/qqmlextensionplugin.h>
Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)
#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QSignalSpy>
#include <QQuickItem>
#include <QQuickView>
#include "capture/ICaptureEngine.h"
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
#include "capture/QtCaptureEngine.h"
#endif
#ifdef Q_OS_WIN
#include "capture/WGCCaptureEngine.h"
#endif
#include "qml/RecordingPrompter.h"

#include "qml/ScreenPickerViewModel.h"

class TestScreenPickerViewModel : public QObject
{
    Q_OBJECT

private slots:
    void setSources_populatesScreens();
    void chooseScreen_emitsChosenScreen();
    void chooseScreen_outOfRangeDoesNotEmit();
    void cancel_emitsCancelled();
    void prompterRequiresSeparateScreen();
    void livePrompterStaysOutsideCapturedFrames_data();
    void livePrompterStaysOutsideCapturedFrames();
};

void TestScreenPickerViewModel::setSources_populatesScreens()
{
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        QSKIP("No screen available for ScreenPickerViewModel test");
    }

    SnapTray::ScreenSource sourceA;
    sourceA.id = QStringLiteral("screen-a");
    sourceA.name = QStringLiteral("Display A");
    sourceA.resolutionText = QStringLiteral("1920 x 1080");
    sourceA.geometry = QRect(0, 0, 1920, 1080);
    sourceA.thumbnail = QImage(16, 9, QImage::Format_ARGB32_Premultiplied);
    sourceA.screen = primaryScreen;

    SnapTray::ScreenSource sourceB = sourceA;
    sourceB.id = QStringLiteral("screen-b");
    sourceB.name = QStringLiteral("Display B");
    sourceB.resolutionText = QStringLiteral("2560 x 1440");
    sourceB.geometry = QRect(0, 0, 2560, 1440);

    SnapTray::ScreenPickerViewModel viewModel({sourceA, sourceB});

    const QVariantList screens = viewModel.screens();
    QCOMPARE(screens.size(), 2);

    const QVariantMap first = screens.at(0).toMap();
    QCOMPARE(first.value(QStringLiteral("name")).toString(), QStringLiteral("Display A"));
    QCOMPARE(first.value(QStringLiteral("resolution")).toString(), QStringLiteral("1920 x 1080"));
    QVERIFY(first.value(QStringLiteral("hasThumbnail")).toBool());
    QCOMPARE(first.value(QStringLiteral("thumbnailId")).toString(), QStringLiteral("screen_picker_0"));
}

void TestScreenPickerViewModel::chooseScreen_emitsChosenScreen()
{
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        QSKIP("No screen available for ScreenPickerViewModel test");
    }

    qRegisterMetaType<QScreen*>("QScreen*");

    SnapTray::ScreenSource source;
    source.id = QStringLiteral("screen-a");
    source.name = QStringLiteral("Display A");
    source.resolutionText = QStringLiteral("1920 x 1080");
    source.geometry = QRect(0, 0, 1920, 1080);
    source.screen = primaryScreen;

    SnapTray::ScreenPickerViewModel viewModel({source});
    QSignalSpy chosenSpy(&viewModel, &SnapTray::ScreenPickerViewModel::screenChosen);
    viewModel.chooseScreen(0);

    QCOMPARE(chosenSpy.count(), 1);
    QCOMPARE(qvariant_cast<QScreen*>(chosenSpy.at(0).at(0)), primaryScreen);
}

void TestScreenPickerViewModel::chooseScreen_outOfRangeDoesNotEmit()
{
    QScreen* primaryScreen = QGuiApplication::primaryScreen();
    if (!primaryScreen) {
        QSKIP("No screen available for ScreenPickerViewModel test");
    }

    SnapTray::ScreenSource source;
    source.id = QStringLiteral("screen-a");
    source.name = QStringLiteral("Display A");
    source.resolutionText = QStringLiteral("1920 x 1080");
    source.geometry = QRect(0, 0, 1920, 1080);
    source.screen = primaryScreen;

    SnapTray::ScreenPickerViewModel viewModel({source});
    QSignalSpy chosenSpy(&viewModel, &SnapTray::ScreenPickerViewModel::screenChosen);
    viewModel.chooseScreen(-1);
    viewModel.chooseScreen(1);

    QCOMPARE(chosenSpy.count(), 0);
}

void TestScreenPickerViewModel::cancel_emitsCancelled()
{
    SnapTray::ScreenPickerViewModel viewModel;
    QSignalSpy cancelledSpy(&viewModel, &SnapTray::ScreenPickerViewModel::cancelled);

    viewModel.cancel();

    QCOMPARE(cancelledSpy.count(), 1);
}

void TestScreenPickerViewModel::prompterRequiresSeparateScreen()
{
    auto* screen = QGuiApplication::primaryScreen();
    QVERIFY(screen);
    SnapTray::ScreenSource source;
    source.screen = screen;
    SnapTray::ScreenPickerViewModel vm({source});
    QSignalSpy chosen(&vm, &SnapTray::ScreenPickerViewModel::screenChosen);
    QVERIFY(!vm.prompterScreen());
    vm.setProperty("prompterDisplay", 0);
    vm.setProperty("prompterScript", QStringLiteral("一段テスト"));
    vm.chooseScreen(0);
    QCOMPARE(chosen.size(), 0);
    QVERIFY(!vm.prompterError().isEmpty());
    SnapTray::RecordingPrompter rejected(screen, screen, vm.prompterScript(), 28);
    QCOMPARE(rejected.captureExclusionId(), quintptr(0));
    QSignalSpy focus(&vm, &SnapTray::ScreenPickerViewModel::editingScriptChanged);
    vm.setEditingScript(true);
    vm.cancel();
    QCOMPARE(focus.size(), 2);
    vm.setProperty("prompterDisplay", -1);
    QVERIFY(vm.prompterScript().isEmpty());
    vm.chooseScreen(0);
    QCOMPARE(chosen.size(), 1);
}

void TestScreenPickerViewModel::livePrompterStaysOutsideCapturedFrames_data()
{
    QTest::addColumn<QString>("backend");
    QTest::addColumn<int>("captureIndex");
    QTest::newRow("default-primary") << QStringLiteral("default") << 0;
    QTest::newRow("default-secondary") << QStringLiteral("default") << 1;
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    QTest::newRow("qt-fallback") << QStringLiteral("qt") << 1;
#endif
#ifdef Q_OS_WIN
    QTest::newRow("graphics-capture") << QStringLiteral("wgc") << 1;
#endif
}

void TestScreenPickerViewModel::livePrompterStaysOutsideCapturedFrames()
{
    QFETCH(QString, backend);
    QFETCH(int, captureIndex);
    if (!qEnvironmentVariableIsSet("SNAPTRAY_TEST_RECORDING_DISPLAYS"))
        QSKIP("Requires an explicitly designated dual-display capture test desktop");
    const auto screens = QGuiApplication::screens();
    QVERIFY(screens.size() >= 2);
    auto* capture = screens.at(captureIndex);
    auto* display = screens.at(1 - captureIndex);
    QVERIFY(SnapTray::RecordingPrompter::separate(capture, display));
    QQuickWindow target;
    target.setFlags(Qt::Window | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    target.setScreen(capture);
    target.setGeometry(capture->geometry());
    target.setColor(Qt::green);
    target.show();
    QVERIFY(QTest::qWaitForWindowExposed(&target));
    const QString script = QStringLiteral("TEST 提詞稿 テスト ").repeated(80);
    SnapTray::RecordingPrompter reader(capture, display, script, 28);
    QVERIFY(reader.captureExclusionId());
    QQuickView* panel = nullptr;
    for (auto* window : QGuiApplication::allWindows()) {
        auto* view = qobject_cast<QQuickView*>(window);
        if (view && view->rootObject() && view->rootObject()->property("script").isValid()) panel = view;
    }
    QVERIFY(panel);
    QVERIFY(QTest::qWaitForWindowExposed(panel));
    const QImage blank = panel->grabWindow();
    QVERIFY(!blank.isNull());
    std::unique_ptr<ICaptureEngine> engine;
    if (backend == QStringLiteral("default")) engine.reset(ICaptureEngine::createBestEngine());
#if defined(Q_OS_MACOS) || defined(Q_OS_WIN)
    if (backend == QStringLiteral("qt")) engine = std::make_unique<QtCaptureEngine>();
#endif
#ifdef Q_OS_WIN
    if (backend == QStringLiteral("wgc")) {
        if (!WGCCaptureEngine::isAvailable()) QSKIP("Windows Graphics Capture is unavailable");
        engine = std::make_unique<WGCCaptureEngine>();
    }
#endif
    QVERIFY(engine);
    qInfo() << "Testing recording backend" << engine->engineName();
    QVERIFY(engine->setRegion(capture->geometry(), capture));
    engine->setSeparateDisplayWindow(reader.captureExclusionId());
    engine->setExcludedCaptureWindowIds({reader.captureExclusionId()});
    QVERIFY(engine->start());
    reader.setReading(true);
    QTRY_COMPARE(panel->rootObject()->property("script").toString(), script);
    QTRY_VERIFY(panel->grabWindow() != blank);
    QImage frame;
    const auto capturesColor = [&](QColor color) {
        const QImage next = engine->captureFrame();
        if (!next.isNull()) frame = next;
        if (frame.isNull()) return false;
        // Sample the whole output, including the area corresponding to the
        // script. Two target colors distinguish live frames from a stale one.
        for (int y = 8; y < frame.height() - 8; y += 12) {
            for (int x = 8; x < frame.width() - 8; x += 12) {
                const QColor actual = frame.pixelColor(x, y);
                if (qAbs(actual.red() - color.red()) > 12 || qAbs(actual.green() - color.green()) > 12
                    || qAbs(actual.blue() - color.blue()) > 12) return false;
            }
        }
        return true;
    };
    QTRY_VERIFY(capturesColor(Qt::green));
    reader.setReading(false);
    QVERIFY(!panel->rootObject()->isEnabled());
    target.setColor(Qt::blue);
    reader.setReading(true);
    QTRY_VERIFY(capturesColor(Qt::blue));
    if (engine->engineName() == QStringLiteral("X11 shared memory")) {
        panel->setPosition(capture->geometry().center());
        QTRY_VERIFY(!engine->isRunning());
    }
    engine->stop();
}

QTEST_MAIN(TestScreenPickerViewModel)
#include "tst_ScreenPickerViewModel.moc"
