#include <QtTest>
#include <optional>
#include <QClipboard>
#include <QProcess>
#include <QScopeGuard>
#ifdef Q_OS_WIN
#include <windows.h>
#include <shellapi.h>
#endif
#include <QGuiApplication>
#include <QMimeData>
#include <QPainter>
#include <QTemporaryDir>
#include "PlatformFeatures.h"
#include "platform/ClipboardImageFile.h"
#include "platform/LinuxClipboardOwner.h"
#include "settings/FileSettingsManager.h"

namespace {
using Mode = FileSettingsManager::ClipboardImageMode;
using Result = PlatformFeatures::ClipboardCopyResult;

QImage finalSnapshot()
{
    QImage image(1800, 1200, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter paint(&image);
    paint.fillRect(10, 10, 1740, 1140, QColor("#ddccff")); // export background and padding
    paint.drawText(40, 60, QStringLiteral("Private detail"));
    paint.fillRect(20, 25, 250, 60, Qt::black); // opaque annotation
    paint.drawText(40, 150, QStringLiteral("Public annotation"));
    return image;
}
}

class TestClipboardImageFile : public QObject
{
    Q_OBJECT
private slots:
    void unavailableAndFileManagerReferences()
    {
        QMimeData data;
        QVERIFY(!SnapTray::referencedClipboardFiles(&data));
        const QString copiedFiles = QStringLiteral("x-special/gnome-copied-files");
        data.setData(copiedFiles, {});
        QVERIFY(!SnapTray::referencedClipboardFiles(&data));
        const auto url = QUrl::fromLocalFile(QStringLiteral("/example/剪貼簿.png"));
        data.setData(copiedFiles, "move\n" + url.toEncoded());
        QVERIFY(!SnapTray::referencedClipboardFiles(&data));
        data.setData(copiedFiles, "copy\nrelative.png");
        QVERIFY(!SnapTray::referencedClipboardFiles(&data));
        data.clear();
        data.setData("text/uri-list", {});
        QVERIFY(!SnapTray::referencedClipboardFiles(&data));
        data.setUrls({QUrl("relative.png")});
        QVERIFY(!SnapTray::referencedClipboardFiles(&data));
        data.clear();
        data.setData(copiedFiles, "cut\n" + url.toEncoded());
        QCOMPARE(SnapTray::referencedClipboardFiles(&data).value(), QList<QUrl>{url});
        data.setData(copiedFiles, "copy\n" + url.toEncoded());
        QCOMPARE(SnapTray::referencedClipboardFiles(&data).value(), QList<QUrl>{url});
        data.clear();
        data.setText(QStringLiteral("replacement"));
        QVERIFY(SnapTray::referencedClipboardFiles(&data)->isEmpty());
    }

    void fileLifetimeAndQuota()
    {
        QTemporaryDir dir;
        const QString store = dir.filePath(QStringLiteral("剪貼簿"));
        const auto image = finalSnapshot();
        auto first = SnapTray::prepareClipboardImageFile(image, store);
        QVERIFY(first);
        QCOMPARE(QImage(first->path).convertToFormat(image.format()), image);
        QCOMPARE(QImage(first->path).pixelColor(50, 50), QColor(Qt::black));
        const auto url = QUrl::fromLocalFile(first->path);
        first->offeredToClipboard = true;
        first.reset();
        QFile held(url.toLocalFile());
        QVERIFY(held.open(QIODevice::ReadWrite));
        QVERIFY(held.setFileTime(QDateTime::currentDateTimeUtc().addDays(-8), QFileDevice::FileModificationTime));
        held.close();
        SnapTray::pruneClipboardImages(store, [] { return std::nullopt; });
        QVERIFY(held.exists());
        SnapTray::pruneClipboardImages(store, [&] { return QList<QUrl>{url}; });
        QVERIFY(held.exists());
        QVERIFY(!SnapTray::prepareClipboardImageFile(image, store, held.size()));
        QVERIFY(!SnapTray::prepareClipboardImageFile(image, held.fileName()));
        QVERIFY(!SnapTray::prepareClipboardImageFile({}, store));
        auto cancelled = SnapTray::prepareClipboardImageFile(image, store);
        QVERIFY(cancelled);
        const QString abandoned = cancelled->path;
        QVERIFY(abandoned != held.fileName());
        cancelled.reset();
        QVERIFY(!QFile::exists(abandoned));
        SnapTray::pruneClipboardImages(store, [] { return QList<QUrl>{}; });
        QVERIFY(!held.exists());
    }

    void quotaEvictsOldestUnreferencedFile()
    {
        QTemporaryDir dir;
        const QImage image = finalSnapshot();
        auto pending = SnapTray::prepareClipboardImageFile(image, dir.path());
        auto active = SnapTray::prepareClipboardImageFile(image, dir.path());
        auto oldest = SnapTray::prepareClipboardImageFile(image, dir.path());
        auto recent = SnapTray::prepareClipboardImageFile(image, dir.path());
        QVERIFY(pending && active && oldest && recent);
        const auto quota = 4 * pending->png.size();
        const auto url = QUrl::fromLocalFile(active->path);
        const QString evicted = oldest->path;
        const QString kept = recent->path;
        active->offeredToClipboard = oldest->offeredToClipboard = recent->offeredToClipboard = true;
        active.reset();
        oldest.reset();
        recent.reset();
        // The active file is oldest, so ordering alone cannot protect it.
        QFile protectedFile(url.toLocalFile());
        QVERIFY(protectedFile.open(QIODevice::ReadWrite));
        QVERIFY(protectedFile.setFileTime(QDateTime::currentDateTimeUtc().addDays(-2), QFileDevice::FileModificationTime));
        protectedFile.close();
        QFile old(evicted);
        QVERIFY(old.open(QIODevice::ReadWrite));
        QVERIFY(old.setFileTime(QDateTime::currentDateTimeUtc().addDays(-1), QFileDevice::FileModificationTime));
        old.close();
        bool read = false;
        SnapTray::pruneClipboardImages(dir.path(), [&] { read = true; return QList<QUrl>{}; });
        QVERIFY(!read);
        QVERIFY(old.exists());
        QVERIFY(!SnapTray::prepareClipboardImageFile(image, dir.path(), quota, [] { return std::nullopt; }));
        QVERIFY(SnapTray::prepareClipboardImageFile(image, dir.path(), quota, [&] { return QList<QUrl>{url}; }));
        QVERIFY(!old.exists());
        QVERIFY(QFile::exists(kept));
        QVERIFY(QFile::exists(pending->path));
        QVERIFY(QFile::exists(url.toLocalFile()));
        QFile unrelated(dir.filePath("Screenshot-other.png"));
        QVERIFY(unrelated.open(QIODevice::WriteOnly));
        QVERIFY(unrelated.setFileTime(QDateTime::currentDateTimeUtc().addDays(-8), QFileDevice::FileModificationTime));
        unrelated.close();
        QFile interrupted(kept + ".ABCDEF");
        QVERIFY(interrupted.open(QIODevice::WriteOnly));
        QVERIFY(interrupted.setFileTime(QDateTime::currentDateTimeUtc().addDays(-8), QFileDevice::FileModificationTime));
        interrupted.close();
        SnapTray::pruneClipboardImages(dir.path(), [] { return QList<QUrl>{}; });
        QVERIFY(unrelated.exists());
        QVERIFY(!interrupted.exists());
    }

#if defined(Q_OS_WIN) || defined(Q_OS_MACOS)
    void textCopyDuringFilePreparationSupersedesImage()
    {
        auto& manager = FileSettingsManager::instance();
        const auto initialMode = manager.clipboardImageMode();
        const auto restore = qScopeGuard([&] { manager.setClipboardImageMode(initialMode); });
        manager.setClipboardImageMode(Mode::ImageAndFile);
        std::optional<Result> result;
        PlatformFeatures::instance().copyImageToClipboardForGuiAsync(finalSnapshot(), this, [&](Result r) { result = r; });
        // Dispatch the queued start without delivering the worker completion.
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
        auto* clipboard = QGuiApplication::clipboard();
        clipboard->setText(QStringLiteral("newer text"));
        QTRY_VERIFY_WITH_TIMEOUT(result.has_value(), 15000);
        QCOMPARE(*result, Result::Superseded);
        QCOMPARE(clipboard->text(), QStringLiteral("newer text"));
    }
#endif

#ifdef Q_OS_WIN
    void nativeWindowsFormats_data()
    {
        QTest::addColumn<bool>("includePixels");
        QTest::newRow("file-only") << false;
        QTest::newRow("image-and-file") << true;
    }

    void nativeWindowsFormats()
    {
        QFETCH(bool, includePixels);
        auto& manager = FileSettingsManager::instance();
        const auto initialMode = manager.clipboardImageMode();
        const auto restore = qScopeGuard([&] { manager.setClipboardImageMode(initialMode); });
        manager.setClipboardImageMode(includePixels ? Mode::ImageAndFile : Mode::FileOnly);
        const auto image = finalSnapshot();
        QVERIFY(PlatformFeatures::instance().copyImageToClipboardPersistently(image));
        const auto urls = QGuiApplication::clipboard()->mimeData()->urls();
        QCOMPARE(urls.size(), 1);
        const auto path = urls.first().toLocalFile();
        QCOMPARE(QFileInfo(path).absolutePath(), SnapTray::clipboardImageDirectory());
        QVERIFY(QFileInfo(path).fileName().startsWith("Screenshot-"));
        const auto cleanup = qScopeGuard([&] {
            QGuiApplication::clipboard()->clear();
            QFile::remove(path);
        });
        QCOMPARE(QImage(path).convertToFormat(image.format()), image);
        QVERIFY(OpenClipboard(nullptr));
        const auto close = qScopeGuard([] { CloseClipboard(); });
        auto drop = static_cast<HDROP>(GetClipboardData(CF_HDROP));
        QVERIFY(drop);
        QCOMPARE(DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0), 1u);
        std::wstring fileName(DragQueryFileW(drop, 0, nullptr, 0) + 1, L'\0');
        const auto length = DragQueryFileW(drop, 0, fileName.data(), UINT(fileName.size()));
        QVERIFY(length);
        QCOMPARE(QDir::fromNativeSeparators(QString::fromWCharArray(fileName.data(), length)), path);
        QCOMPARE(bool(IsClipboardFormatAvailable(RegisterClipboardFormatW(L"PNG")) || IsClipboardFormatAvailable(CF_DIB)
                      || IsClipboardFormatAvailable(CF_DIBV5)), includePixels);
    }
#endif

    void isolatedClipboard()
    {
#ifndef CLIPBOARD_PROBE_PATH
        QSKIP("Requires the X11 clipboard owner");
#else
        if (qEnvironmentVariable("SNAPTRAY_TEST_X11_CLIPBOARD") != "1"
            || QGuiApplication::platformName() != QStringLiteral("xcb"))
            QSKIP("Requires a private Xvfb clipboard");
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const auto dataHome = qgetenv("XDG_DATA_HOME");
        const auto appImage = qgetenv("APPIMAGE");
        const auto restore = qScopeGuard([&] {
            if (dataHome.isNull()) qunsetenv("XDG_DATA_HOME");
            else qputenv("XDG_DATA_HOME", dataHome);
            if (appImage.isNull()) qunsetenv("APPIMAGE");
            else qputenv("APPIMAGE", appImage);
        });
        qputenv("XDG_DATA_HOME", dir.path().toUtf8());
        qputenv("APPIMAGE", CLIPBOARD_PROBE_PATH);
        auto& manager = FileSettingsManager::instance();
        const auto initialMode = manager.clipboardImageMode();
        const auto restoreMode = qScopeGuard([&] { manager.setClipboardImageMode(initialMode); });
        QCOMPARE(initialMode, Mode::ImageOnly);
        auto& platform = PlatformFeatures::instance();
        auto* clipboard = QGuiApplication::clipboard();
        const auto image = finalSnapshot();
        QVERIFY(platform.copyImageToClipboardForGui(image));
        QVERIFY(clipboard->mimeData()->urls().isEmpty());
        manager.setClipboardImageMode(Mode::ImageAndFile);
        // Both jobs can encode, but there is only room for one committed PNG.
        auto sample = SnapTray::prepareClipboardImageFile(image, SnapTray::clipboardImageDirectory());
        QVERIFY(sample);
        const auto size = sample->png.size();
        sample.reset();
        QFile ballast(QDir(SnapTray::clipboardImageDirectory()).filePath("Screenshot-quota.png"));
        QVERIFY(ballast.open(QIODevice::WriteOnly));
        QVERIFY(ballast.resize(SnapTray::kClipboardImageQuota - size));
        ballast.close();
        std::optional<Result> first, latest;
        platform.copyImageToClipboardForGuiAsync(image, this, [&](Result r) { first = r; });
        // Start the PNG worker, then request the second copy before its completion is delivered.
        QCoreApplication::sendPostedEvents(this, QEvent::MetaCall);
        platform.copyImageToClipboardForGuiAsync(image, this, [&](Result r) { latest = r; });
        QTRY_VERIFY_WITH_TIMEOUT(first && latest, 15000);
        QCOMPARE(*first, Result::Superseded);
        QCOMPARE(*latest, Result::Success);
        // Destroying a caller after its worker starts must also release its
        // allocation and allow the next queued copy to finish.
        clipboard->setText(QStringLiteral("release previous reference"));
        auto context = std::make_unique<QObject>();
        bool called = false;
        platform.copyImageToClipboardForGuiAsync(image, context.get(), [&](Result) { called = true; });
        QCoreApplication::sendPostedEvents(context.get(), QEvent::MetaCall);
        context.reset();
        latest.reset();
        platform.copyImageToClipboardForGuiAsync(image, this, [&](Result r) { latest = r; });
        QTRY_VERIFY_WITH_TIMEOUT(latest.has_value(), 15000);
        QCOMPARE(*latest, Result::Success);
        QVERIFY(!called);
        QCOMPARE(QDir(SnapTray::clipboardImageDirectory()).entryList({"Screenshot-*.png"}, QDir::Files).size(), 2);
        QVERIFY(ballast.remove());
        const auto urls = clipboard->mimeData()->urls();
        QCOMPARE(urls.size(), 1);
        QVERIFY(urls.first().isLocalFile());
        QCOMPARE(QImage(urls.first().toLocalFile()).convertToFormat(image.format()), image);
        QCOMPARE(clipboard->image().convertToFormat(image.format()), image);
        QVERIFY(!platform.copyImageToClipboardPersistently({}));
        QCOMPARE(clipboard->mimeData()->urls(), urls);
        // Failure to start an owner is still a preparation failure: the previous
        // clipboard survives and the rejected snapshot does not consume quota.
        const int fileCount = QDir(SnapTray::clipboardImageDirectory()).entryList({"*.png"}, QDir::Files).size();
        qputenv("APPIMAGE", dir.filePath("missing-owner").toUtf8());
        QString error;
        QVERIFY(!platform.copyImageToClipboardPersistently(image, &error));
        QCOMPARE(error, QStringLiteral("Failed to copy to clipboard"));
        QCOMPARE(clipboard->mimeData()->urls(), urls);
        QCOMPARE(QDir(SnapTray::clipboardImageDirectory()).entryList({"*.png"}, QDir::Files).size(), fileCount);
        qputenv("APPIMAGE", CLIPBOARD_PROBE_PATH);
        manager.setClipboardImageMode(Mode::FileOnly);
        std::optional<Result> next;
        platform.copyImageToClipboardForGuiAsync(image, this, [&](Result copied) {
            if (copied != Result::Success) {
                next = copied;
                return;
            }
            platform.copyImageToClipboardForGuiAsync(image, this, [&](Result r) { next = r; });
        });
        QTRY_VERIFY_WITH_TIMEOUT(next.has_value(), 15000);
        QCOMPARE(*next, Result::Success);
        QTRY_COMPARE(clipboard->mimeData()->urls().size(), 1);
        QTRY_VERIFY(!clipboard->mimeData()->hasImage());
        clipboard->setText(QStringLiteral("replacement"));
        QVERIFY(QFileInfo::exists(urls.first().toLocalFile()));

        // A replacement that won during exit handoff keeps ownership.
        const auto currentOwner = SnapTray::linuxClipboardOwnerWindow();
        QVERIFY(currentOwner);
        bool started = false;
        QVERIFY(!SnapTray::copyFileToLinuxClipboard(urls.first().toLocalFile(), true,
            QString::fromUtf8(CLIPBOARD_PROBE_PATH), started, *currentOwner + 1));
        QVERIFY(!started);
        QCOMPARE(clipboard->text(), QStringLiteral("replacement"));

        QProcess gui;
        auto environment = QProcessEnvironment::systemEnvironment();
        // The detached owner must serve the encoded PNG even when its decoder
        // cannot allocate enough memory for the uncompressed image.
        environment.insert("QT_IMAGEIO_MAXALLOC", "1");
        gui.setProcessEnvironment(environment);
        gui.start(QString::fromUtf8(CLIPBOARD_PROBE_PATH), {QStringLiteral("--gui-file-copy")});
        // Keep serving Clipboard requests while the other GUI takes ownership.
        QTRY_COMPARE_WITH_TIMEOUT(gui.state(), QProcess::NotRunning, 15000);
        QCOMPARE(gui.exitCode(), 0);
        QTRY_COMPARE(clipboard->image().pixelColor(0, 0), QColor(Qt::blue));
        const auto retained = clipboard->mimeData()->urls();
        QCOMPARE(retained.size(), 1);
        QCOMPARE(QImage(retained.first().toLocalFile()).pixelColor(0, 0), QColor(Qt::blue));
        clipboard->setText(QStringLiteral("finished"));
        clipboard->clear();
        QTRY_VERIFY(SnapTray::linuxClipboardOwnerWindow() == 0u);
        QFile expired(urls.first().toLocalFile());
        QVERIFY(expired.open(QIODevice::ReadWrite));
        QVERIFY(expired.setFileTime(QDateTime::currentDateTimeUtc().addDays(-8), QFileDevice::FileModificationTime));
        expired.close();
        PlatformFeatures::cleanClipboardImageFiles();
        QVERIFY(!expired.exists());
#endif
    }
};

QTEST_MAIN(TestClipboardImageFile)
#include "tst_ClipboardImageFile.moc"
