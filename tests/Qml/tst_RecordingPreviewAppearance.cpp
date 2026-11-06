#include <QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>

#include "IVideoEncoder.h"
#include "beautify/FrameStyleLibrary.h"
#include "qml/RecordingPreviewBackend.h"
#include "recording/RecordingAppearance.h"
#include "settings/RecordingSettingsManager.h"
#include "settings/Settings.h"

// The preview's frame around the export (#184) against the real QML.
class tst_RecordingPreviewAppearance : public QObject
{
    Q_OBJECT

private slots:
    void panelEditsTheFrame();
    void undoAndResetKeepTheLook();
    void savedStylesAreShared();
    void savedStylesInThePanel();

private:
    static QQuickView* openPreview(RecordingPreviewBackend& backend);
    static QQuickItem* item(QQuickView* view, const char* name);
};

namespace {
// A short real recording, so the player shows a picture to frame.
bool writeRecording(const QString& path, const QSize& size)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder || !encoder->start(path, size, 10)) return false;
    QImage frame(size, QImage::Format_ARGB32);
    frame.fill(Qt::darkGreen);
    for (int i = 0; i < 10; ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer wait;
        wait.start();
        do {
            encoder->writeFrame(frame, i * 100);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (wait.elapsed() < 2000);
    }
    // macOS and Windows finish asynchronously.
    QSignalSpy finishedSpy(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finishedSpy.isEmpty() && !finishedSpy.wait(10000)) return false;
    return finishedSpy.first().at(0).toBool();
}
} // namespace

QQuickView* tst_RecordingPreviewAppearance::openPreview(RecordingPreviewBackend& backend)
{
    backend.show();
    QQuickView* view = nullptr;
    const auto find = [&view] {
        for (QWindow* window : QGuiApplication::topLevelWindows()) {
            if (window->isVisible() && window->title() == RecordingPreviewBackend::tr("Recording Preview")) {
                view = qobject_cast<QQuickView*>(window);
            }
        }
        return view != nullptr;
    };
    if (!QTest::qWaitFor(find, 5000)) return nullptr;
    return QTest::qWaitForWindowExposed(view) ? view : nullptr;
}

namespace {
// Repeater delegates are only in the visual tree, not among QObject children.
QQuickItem* findVisual(QQuickItem* node, const QString& name)
{
    if (!node) return nullptr;
    if (node->objectName() == name) return node;
    for (QQuickItem* child : node->childItems()) {
        if (QQuickItem* found = findVisual(child, name)) return found;
    }
    return nullptr;
}
} // namespace

QQuickItem* tst_RecordingPreviewAppearance::item(QQuickView* view, const char* name)
{
    const QString wanted = QString::fromLatin1(name);
    if (auto* found = view->rootObject()->findChild<QQuickItem*>(wanted)) return found;
    return findVisual(qobject_cast<QQuickItem*>(view->rootObject()), wanted);
}

void tst_RecordingPreviewAppearance::panelEditsTheFrame()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QJsonObject remembered = RecordingSettingsManager::instance().appearance();
    auto restore = qScopeGuard([remembered]() { RecordingSettingsManager::instance().setAppearance(remembered); });
    RecordingSettingsManager::instance().setAppearance({});

    const QString video = dir.filePath(QStringLiteral("recording.mp4"));
    if (!writeRecording(video, QSize(320, 180))) QSKIP("No native encoder for the fixture recording");
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF); // drawable everywhere
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.videoSize().isEmpty(), 10000);
    QVERIFY(!backend.appearanceEnabled()); // off until switched on
    QQuickItem* open = item(view, "previewAppearanceOptions");
    QQuickItem* panel = item(view, "previewAppearancePanel");
    QQuickItem* overlay = item(view, "previewAppearanceOverlay");
    QQuickItem* stage = item(view, "previewStage");
    QVERIFY(open && panel && overlay && stage);
    QVERIFY(open->isEnabled());
    QVERIFY(QMetaObject::invokeMethod(open, "clicked"));
    QTRY_VERIFY(panel->isVisible());

    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceToggle"), "clicked"));
    QVERIFY(backend.appearanceEnabled());
    QTRY_VERIFY(overlay->isVisible());
    QTRY_VERIFY(overlay->width() > 0);
    // The framed canvas is fitted into the stage: everything scales together.
    const QRectF canvas(overlay->x(), overlay->y(), overlay->width(), overlay->height());
    QVERIFY(canvas.width() > 0);
    const QRectF shown = stage->mapRectToItem(view->contentItem(), canvas);
    // The stage's own place in the layout (its transform scales what it draws, not where it sits).
    const QRectF stageRect = stage->parentItem()->mapRectToItem(
        view->contentItem(), QRectF(stage->x(), stage->y(), stage->width(), stage->height()));
    QVERIFY2(stageRect.adjusted(-1, -1, 1, 1).contains(shown),
             qPrintable(QStringLiteral("%1x%2 in %3x%4").arg(shown.width()).arg(shown.height())
                            .arg(stageRect.width()).arg(stageRect.height())));

    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearancePadding"), "activated", Q_ARG(int, 3)));
    QCOMPARE(backend.appearancePadding(), int(SnapTray::RecordingAppearance::Amount::Large));
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceShadow"), "clicked"));
    QCOMPARE(backend.appearanceShadow(), false);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceBorder"), "clicked"));
    QVERIFY(backend.appearanceBorder());
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceRatio"), "activated", Q_ARG(int, 1)));
    QCOMPARE(backend.appearanceAspectRatio(), 1);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceBlur"), "clicked"));
    QCOMPARE(backend.appearanceBackground(), int(SnapTray::RecordingAppearance::Background::BlurredVideo));
    // A new crop remakes the blurred background, also while paused (PR #198 review).
    QTRY_VERIFY(!backend.appearancePreviewFrame().isNull());
    QSignalSpy blurRemade(&backend, &RecordingPreviewBackend::appearancePreviewFrameChanged);
    backend.setCropRect(QRect(0, 0, 160, 90));
    QTRY_VERIFY(blurRemade.count() >= 1);
    // Framed and cropped: only the crop is shown, clipped, with no dimmed
    // outside or crop border around it (PR #198 review).
    QTRY_VERIFY(item(view, "previewZoomClip")->clip());
    QQuickItem* cropOverlay = item(view, "previewCropOverlay");
    QVERIFY(cropOverlay);
    QCOMPARE(cropOverlay->findChild<QObject*>(QStringLiteral("cropOutsideDim"))->property("count").toInt(), 0);
    QVERIFY(!cropOverlay->findChild<QQuickItem*>(QStringLiteral("cropSelectionFrame"))->isVisible());
    // A small crop in the far corner: the frame-fitting transform moves the
    // picture, never the controls. The crop chip stays at the stage's top
    // left, inside the window (PR #198 review).
    backend.setCropRect(QRect(160, 90, 160, 90));
    QQuickItem* chip = item(view, "previewCropChip");
    QVERIFY(chip);
    QTRY_VERIFY(chip->isVisible());
    QTest::qWait(50);
    const QRectF chipRect = chip->mapRectToScene(QRectF(0, 0, chip->width(), chip->height()));
    const QRectF stageArea = stage->parentItem()->mapRectToScene(
        QRectF(stage->x(), stage->y(), stage->width(), stage->height()));
    QVERIFY2(QRectF(QPointF(0, 0), QSizeF(view->size())).contains(chipRect),
             qPrintable(QStringLiteral("chip at %1,%2").arg(chipRect.x()).arg(chipRect.y())));
    QVERIFY(qAbs(chipRect.left() - stageArea.left()) < 40 && qAbs(chipRect.top() - stageArea.top()) < 40);
    blurRemade.clear();
    backend.clearCrop();
    QTRY_VERIFY(blurRemade.count() >= 1);
    // An image needs a file first: nothing changes without one.
    backend.setAppearanceBackground(int(SnapTray::RecordingAppearance::Background::Image));
    QCOMPARE(backend.appearanceBackground(), int(SnapTray::RecordingAppearance::Background::BlurredVideo));
    QVERIFY(!backend.setAppearanceImage(QUrl::fromLocalFile(dir.filePath(QStringLiteral("none.png")))));

    // Remembered for the next recording.
    const auto saved = SnapTray::RecordingAppearance::fromJson(RecordingSettingsManager::instance().appearance());
    QCOMPARE(saved, backend.appearance());

    QVERIFY(QMetaObject::invokeMethod(item(view, "editorReturnToProject"), "clicked"));
    QTRY_VERIFY(!panel->isVisible());
    // MP4 on a system that cannot draw: the frame is not offered there.
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QCOMPARE(open->isEnabled(), backend.appearanceAvailable());
    backend.requestDiscard();
}

void tst_RecordingPreviewAppearance::undoAndResetKeepTheLook()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QJsonObject remembered = RecordingSettingsManager::instance().appearance();
    auto restore = qScopeGuard([remembered]() { RecordingSettingsManager::instance().setAppearance(remembered); });
    RecordingSettingsManager::instance().setAppearance({});

    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    // GIF draws the frame everywhere (MP4 does not on macOS and Windows yet).
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateVideoSize(QSize(320, 180));
    backend.setAppearanceEnabled(true);
    backend.setAppearanceCorners(int(SnapTray::RecordingAppearance::Amount::Large));
    QVERIFY(!backend.hasEdits()); // a look, not a footage edit
    backend.undo();
    QCOMPARE(backend.appearanceCorners(), int(SnapTray::RecordingAppearance::Amount::Medium));
    backend.redo();
    QCOMPARE(backend.appearanceCorners(), int(SnapTray::RecordingAppearance::Amount::Large));
    backend.setCropRect(QRect(0, 0, 160, 90));
    backend.resetEdits();
    QVERIFY(backend.appearanceEnabled());
    QCOMPARE(backend.appearanceCorners(), int(SnapTray::RecordingAppearance::Amount::Large));
    // The framed size follows the crop.
    backend.setCropRect(QRect(0, 0, 160, 90));
    QCOMPARE(backend.exportContentSize(), QSize(160, 90));
    const QRectF canvas = backend.appearanceCanvasInView(QRectF(0, 0, 160, 90));
    QVERIFY(canvas.width() > 160 && canvas.height() > 90);
}

namespace {
// Saved styles live in the Beautify settings; each test starts without any.
auto withoutSavedStyles()
{
    const auto clear = [] {
        auto settings = SnapTray::getSettings();
        settings.remove(QStringLiteral("beautify/customStyles"));
        settings.remove(QStringLiteral("beautify/frameStyles"));
        settings.sync();
        SnapTray::FrameStyleLibrary::instance().reload();
    };
    clear();
    return qScopeGuard(clear);
}
} // namespace

void tst_RecordingPreviewAppearance::savedStylesAreShared()
{
    // #269: one list with screenshot Beautify, both ways.
    auto styles = withoutSavedStyles();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QJsonObject remembered = RecordingSettingsManager::instance().appearance();
    auto restore = qScopeGuard([remembered]() { RecordingSettingsManager::instance().setAppearance(remembered); });
    RecordingSettingsManager::instance().setAppearance({});

    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    backend.updateVideoSize(QSize(320, 180));
    QCOMPARE(backend.frameStyleModel().size(), 0);
    QCOMPARE(backend.currentFrameStyle(), -1);

    // Saved here: in the shared list, as relative sizes.
    backend.setAppearanceEnabled(true);
    backend.setAppearancePadding(int(SnapTray::RecordingAppearance::Amount::Large));
    backend.setAppearanceBorder(true);
    QVERIFY(backend.saveFrameStyle(QStringLiteral("Talk")));
    const auto& library = SnapTray::FrameStyleLibrary::instance();
    QCOMPARE(library.count(), 1);
    QCOMPARE(library.styles().at(0).style.padding, SnapTray::RecordingAppearance::kPaddingFractions[3]);
    QVERIFY(library.styles().at(0).style.borderEnabled);
    QCOMPARE(backend.currentFrameStyle(), 0);

    // Saved in Beautify (pixels on a 1440 px picture): shows up here.
    QSignalSpy listChanged(&backend, &RecordingPreviewBackend::frameStylesChanged);
    BeautifySettings shot;
    shot.backgroundType = BeautifyBackgroundType::Solid;
    shot.backgroundColor = QColor(200, 30, 60);
    shot.padding = 100;
    shot.cornerRadius = 30;
    shot.shadowBlur = 25;
    QVERIFY(SnapTray::FrameStyleLibrary::instance().save(QStringLiteral("Docs"),
                                                         SnapTray::FrameStyle::fromBeautify(shot)));
    QCOMPARE(listChanged.count(), 1);
    QCOMPARE(backend.frameStyleModel().size(), 2);
    QCOMPARE(backend.frameStyleModel().at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("Docs"));

    // Applied over a blurred-video background: that stays, the rest changes.
    backend.setAppearanceBackground(int(SnapTray::RecordingAppearance::Background::BlurredVideo));
    backend.applyFrameStyle(1);
    QCOMPARE(backend.appearanceBackground(), int(SnapTray::RecordingAppearance::Background::BlurredVideo));
    QCOMPARE(backend.appearancePadding(), int(SnapTray::RecordingAppearance::Amount::Custom));
    QCOMPARE(backend.appearanceCorners(), int(SnapTray::RecordingAppearance::Amount::Custom));
    QVERIFY(!backend.appearanceBorder());
    QCOMPARE(backend.appearanceSolidColor(), QColor(200, 30, 60));
    QCOMPARE(backend.currentFrameStyle(), 1);
    // On a 1440 px picture the recording uses the screenshot's pixels.
    const BeautifySettings pixels = backend.appearance().beautifyFor(QSize(2560, 1440));
    QCOMPARE(pixels.padding, 100);
    QCOMPARE(pixels.cornerRadius, 30);
    QCOMPARE(pixels.shadowBlur, 25);
    // An edit like any other: undo brings the previous look back.
    backend.undo();
    QCOMPARE(backend.appearancePadding(), int(SnapTray::RecordingAppearance::Amount::Large));
    // Talk's look again; the blurred-video background is the recording's own.
    QCOMPARE(backend.currentFrameStyle(), 0);

    QVERIFY(backend.renameFrameStyle(0, QStringLiteral("Talks")));
    QVERIFY(!backend.renameFrameStyle(0, QStringLiteral("docs"))); // another style's name
    QVERIFY(backend.frameStyleNameTaken(QStringLiteral("TALKS")));
    backend.deleteFrameStyle(0);
    QCOMPARE(library.count(), 1);
    QCOMPARE(library.styles().at(0).name, QStringLiteral("Docs"));
    backend.applyFrameStyle(7); // out of range: nothing
}

void tst_RecordingPreviewAppearance::savedStylesInThePanel()
{
    auto styles = withoutSavedStyles();
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QJsonObject remembered = RecordingSettingsManager::instance().appearance();
    auto restore = qScopeGuard([remembered]() { RecordingSettingsManager::instance().setAppearance(remembered); });
    RecordingSettingsManager::instance().setAppearance({});

    const QString video = dir.filePath(QStringLiteral("recording.mp4"));
    if (!writeRecording(video, QSize(320, 180))) QSKIP("No native encoder for the fixture recording");
    RecordingPreviewBackend backend(video);
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QTRY_VERIFY_WITH_TIMEOUT(!backend.videoSize().isEmpty(), 10000);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceOptions"), "clicked"));
    QTRY_VERIFY(item(view, "previewAppearancePanel")->isVisible());
    QVERIFY(item(view, "previewAppearanceStylesEmpty")->isVisible());
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceToggle"), "clicked"));
    QVERIFY(backend.appearanceEnabled());

    // Save Style… asks for a name, prefilled with a free one.
    QQuickItem* name = item(view, "previewAppearanceStyleName");
    QVERIFY(name);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceStyleSave"), "clicked"));
    QTRY_VERIFY(item(view, "previewAppearanceStyleNameRow")->isVisible());
    QCOMPARE(name->property("text").toString(), QStringLiteral("My style 1"));
    name->setProperty("text", QStringLiteral("Launch video"));
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceStyleNameSave"), "clicked"));
    QTRY_VERIFY(!item(view, "previewAppearanceStyleNameRow")->isVisible());
    QCOMPARE(SnapTray::FrameStyleLibrary::instance().count(), 1);
    QCOMPARE(SnapTray::FrameStyleLibrary::instance().styles().at(0).name, QStringLiteral("Launch video"));

    // The saved style is a chosen, named choice for screen readers.
    QQuickItem* chip = nullptr;
    QTRY_VERIFY((chip = item(view, "previewAppearanceStyle0")) != nullptr);
    QTRY_VERIFY(chip->property("selected").toBool());
    QCOMPARE(chip->property("text").toString(), QStringLiteral("Launch video"));

    // A style from Beautify with sizes between the presets reads as Custom.
    SnapTray::FrameStyle between;
    between.padding = 0.05;
    between.cornerRadius = 0.02;
    QVERIFY(SnapTray::FrameStyleLibrary::instance().save(QStringLiteral("Between"), between));
    QTRY_VERIFY(item(view, "previewAppearanceStyle1") != nullptr);
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceStyle1"), "clicked"));
    QCOMPARE(backend.currentFrameStyle(), 1);
    QTRY_COMPARE(item(view, "previewAppearancePadding")->property("currentIndex").toInt(), -1);
    QCOMPARE(item(view, "previewAppearancePadding")->property("currentValueText").toString(), QStringLiteral("Custom"));
    QVERIFY(!item(view, "previewAppearanceGradientCustom")->isVisible()); // the default gradient is a preset

    // Rename and delete act on the chosen style.
    QVERIFY(item(view, "previewAppearanceStyleRename")->isVisible());
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceStyleRename"), "clicked"));
    QTRY_COMPARE(name->property("text").toString(), QStringLiteral("Between"));
    name->setProperty("text", QStringLiteral("launch VIDEO"));
    QTRY_VERIFY(item(view, "previewAppearanceStyleNameTaken")->isVisible());
    QVERIFY(!item(view, "previewAppearanceStyleNameSave")->isEnabled());
    name->setProperty("text", QStringLiteral("Wide"));
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceStyleNameSave"), "clicked"));
    QTRY_COMPARE(SnapTray::FrameStyleLibrary::instance().styles().at(1).name, QStringLiteral("Wide"));
    QVERIFY(QMetaObject::invokeMethod(item(view, "previewAppearanceStyleDelete"), "clicked"));
    QCOMPARE(SnapTray::FrameStyleLibrary::instance().count(), 1);
    backend.requestDiscard();
}

QTEST_MAIN(tst_RecordingPreviewAppearance)
#include "tst_RecordingPreviewAppearance.moc"
