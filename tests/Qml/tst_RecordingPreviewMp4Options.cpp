#include <QtTest>

#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QTemporaryDir>

#include "qml/RecordingPreviewBackend.h"
#include "recording/Mp4ExportOptions.h"
#include "settings/RecordingSettingsManager.h"

// The preview's MP4 export options (#190) against the real QML.
class tst_RecordingPreviewMp4Options : public QObject
{
    Q_OBJECT

private slots:
    void panelSetsAndRemembersTheOptions();
    void panelOnlyForMp4();

private:
    static QQuickView* openPreview(RecordingPreviewBackend& backend);
    static QQuickItem* item(QQuickView* view, const char* name);
};

QQuickView* tst_RecordingPreviewMp4Options::openPreview(RecordingPreviewBackend& backend)
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

QQuickItem* tst_RecordingPreviewMp4Options::item(QQuickView* view, const char* name)
{
    return view->rootObject()->findChild<QQuickItem*>(QString::fromLatin1(name));
}

void tst_RecordingPreviewMp4Options::panelSetsAndRemembersTheOptions()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QJsonObject remembered = RecordingSettingsManager::instance().mp4ExportOptions();
    auto restore = qScopeGuard([remembered]() { RecordingSettingsManager::instance().setMp4ExportOptions(remembered); });
    RecordingSettingsManager::instance().setMp4ExportOptions({});

    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QQuickItem* open = item(view, "previewMp4Options");
    QQuickItem* panel = item(view, "previewMp4Panel");
    QQuickItem* formatRow = item(view, "previewFormatRow");
    QVERIFY(open && panel && formatRow);
    QVERIFY(open->isVisible());
    QVERIFY(!panel->isVisible());
    QVERIFY(QMetaObject::invokeMethod(open, "clicked"));
    QTRY_VERIFY(panel->isVisible());
    QVERIFY(!formatRow->isEnabled());

    QQuickItem* resolution = item(view, "previewMp4Resolution");
    QQuickItem* frameRate = item(view, "previewMp4FrameRate");
    QQuickItem* hevc = item(view, "previewMp4Hevc");
    QVERIFY(resolution && frameRate && hevc);
    // Size and frame rate follow what this system's transcoder can change.
    QCOMPARE(resolution->isEnabled(), backend.mp4OutputOptionsAvailable());
    QCOMPARE(frameRate->isEnabled(), backend.mp4OutputOptionsAvailable());
    QCOMPARE(hevc->isEnabled(), backend.mp4HevcAvailable());

    QVERIFY(QMetaObject::invokeMethod(item(view, "previewMp4Quality"), "clicked"));
    QCOMPARE(backend.mp4Quality(), int(SnapTray::Mp4ExportOptions::Quality::Low));
    if (backend.mp4OutputOptionsAvailable()) {
        for (int i = 0; i < 3; ++i) QVERIFY(QMetaObject::invokeMethod(resolution, "clicked"));
        QCOMPARE(backend.mp4Resolution(), int(SnapTray::Mp4ExportOptions::Resolution::CustomWidth));
        QQuickItem* width = item(view, "previewMp4Width");
        QVERIFY(width);
        QTRY_VERIFY(width->isVisible());
    }
    if (backend.mp4HevcAvailable()) {
        QVERIFY(QMetaObject::invokeMethod(hevc, "clicked"));
        QCOMPARE(backend.mp4Codec(), int(SnapTray::Mp4ExportOptions::Codec::Hevc));
    }
    // Remembered for the next recording.
    const auto saved = SnapTray::Mp4ExportOptions::fromJson(RecordingSettingsManager::instance().mp4ExportOptions());
    QCOMPARE(int(saved.quality), backend.mp4Quality());
    QCOMPARE(int(saved.codec), backend.mp4Codec());

    QVERIFY(QMetaObject::invokeMethod(item(view, "previewMp4OptionsDone"), "clicked"));
    QTRY_VERIFY(!panel->isVisible());
    QVERIFY(formatRow->isEnabled());
    backend.requestDiscard();
}

void tst_RecordingPreviewMp4Options::panelOnlyForMp4()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    RecordingPreviewBackend backend(dir.filePath(QStringLiteral("missing.mp4")));
    backend.setSelectedFormat(RecordingPreviewBackend::GIF);
    QQuickView* view = openPreview(backend);
    QVERIFY(view);
    QQuickItem* open = item(view, "previewMp4Options");
    QVERIFY(open);
    QVERIFY(!open->isVisible());
    backend.setSelectedFormat(RecordingPreviewBackend::MP4);
    QTRY_VERIFY(open->isVisible());
    backend.requestDiscard();
}

QTEST_MAIN(tst_RecordingPreviewMp4Options)
#include "tst_RecordingPreviewMp4Options.moc"
