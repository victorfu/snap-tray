#include <QtTest/QtTest>

#include "recording/WindowTimeline.h"
#include "recording/WindowTimelineSidecar.h"

#include <QFile>
#include <QTemporaryDir>

using SnapTray::WindowSample;
using SnapTray::WindowTimeline;
namespace Sidecar = SnapTray::WindowTimelineSidecar;

class tst_WindowTimelineSidecar : public QObject
{
    Q_OBJECT

private slots:
    void pathSitsNextToTheVideo();
    void writeReadRoundTrip();
    void readMissingOrCorruptIsEmpty();
    void removeIsSafe();
};

void tst_WindowTimelineSidecar::pathSitsNextToTheVideo()
{
    QCOMPARE(Sidecar::pathFor(QStringLiteral("C:/tmp/SnapTray_Recording_x.mp4")),
             QStringLiteral("C:/tmp/SnapTray_Recording_x.mp4.windows.json"));
}

void tst_WindowTimelineSidecar::writeReadRoundTrip()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(1920, 1080));
    WindowSample window;
    window.windowId = 42;
    window.rect = QRect(0, 0, 960, 1080);
    window.ownerApp = QStringLiteral("Code");
    timeline.append(0, {window});

    QVERIFY(Sidecar::write(video, timeline));
    QVERIFY(QFile::exists(Sidecar::pathFor(video)));
    const auto read = Sidecar::read(video);
    QVERIFY(read.has_value());
    QCOMPARE(read->frameSize(), QSize(1920, 1080));
    QCOMPARE(read->entries().front().windows.front().windowId, quint32(42));
}

void tst_WindowTimelineSidecar::readMissingOrCorruptIsEmpty()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    QVERIFY(!Sidecar::read(video).has_value());
    QFile corrupt(Sidecar::pathFor(video));
    QVERIFY(corrupt.open(QIODevice::WriteOnly));
    corrupt.write("{not json");
    corrupt.close();
    QVERIFY(!Sidecar::read(video).has_value());
}

void tst_WindowTimelineSidecar::removeIsSafe()
{
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QString video = dir.filePath(QStringLiteral("rec.mp4"));
    Sidecar::remove(video); // nothing there: no warning, no crash
    WindowTimeline timeline;
    timeline.setFrameSize(QSize(16, 16));
    QVERIFY(Sidecar::write(video, timeline));
    Sidecar::remove(video);
    QVERIFY(!QFile::exists(Sidecar::pathFor(video)));
}

QTEST_GUILESS_MAIN(tst_WindowTimelineSidecar)
#include "tst_WindowTimelineSidecar.moc"
