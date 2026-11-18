#include "qml/RecordingFilmstrip.h"
#include "qml/RecordingPreviewBackend.h"

#include <QPainter>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QtTest>

class tst_RecordingFilmstrip : public QObject
{
    Q_OBJECT

    static QColor sample(RecordingFilmstrip& strip, int x)
    {
        QImage surface(strip.size().toSize(), QImage::Format_ARGB32_Premultiplied);
        surface.fill(Qt::transparent);
        QPainter painter(&surface);
        strip.paint(&painter);
        painter.end();
        return surface.pixelColor(x, surface.height() / 2);
    }

private slots:
    void longRotatedRecording()
    {
        const QString path = QFINDTESTDATA("../Video/fixtures/filmstrip-rotated-vfr.mp4");
        QVERIFY(!path.isEmpty());
        RecordingPreviewBackend editor(path);
        editor.updateVideoSize({120, 160});
        editor.updateDuration(3276900);
        RecordingFilmstrip strip;
        strip.setSize({224, 40});
        strip.setBackend(&editor);
#ifdef Q_OS_MACOS
        QTRY_VERIFY_WITH_TIMEOUT(sample(strip, 28).red() > 200, 3000);
        QTRY_VERIFY(sample(strip, 196).green() > 100); // 2457 seconds into the source
        QCOMPARE(sample(strip, 1).alpha(), 0); // portrait image is fitted inside the tile
#else
        // Readers without display-transform support leave the clip usable.
        QTest::qWait(300);
        QVERIFY(!strip.hasTiles());
#endif
    }

    void replacingMediaInvalidatesEveryLevel()
    {
        const QString source = QFINDTESTDATA("../Video/fixtures/edit-vfr.mp4");
        QTemporaryDir dir;
        const QString path = dir.filePath("source.mp4");
        QVERIFY(QFile::copy(source, path));
        RecordingPreviewBackend editor(path);
        editor.updateVideoSize({160, 120});
        editor.updateDuration(1700);
        RecordingFilmstrip strip;
        strip.setSize({224, 24});
        strip.setBackend(&editor);
        QTRY_VERIFY(strip.hasTiles());
        strip.setWidth(448);
        QTRY_VERIFY(strip.hasTiles());
        const auto replaceMedia = [&] {
            QSaveFile replacement(path);
            return replacement.open(QIODevice::WriteOnly) && replacement.write("different media") == 15
                && replacement.commit();
        };
        // Windows may hold the file through the last in-flight native read.
        QTRY_VERIFY(replaceMedia());
        QTRY_VERIFY(!strip.hasTiles());
        strip.setWidth(224);
        QTest::qWait(150);
        QCOMPARE(sample(strip, 28).alpha(), 0);
    }

    void samplesSourceTimeAndInvalidatesSensitivePixels()
    {
        const QString path = QFINDTESTDATA("../Video/fixtures/edit-vfr.mp4");
        QVERIFY(!path.isEmpty());
        RecordingPreviewBackend editor(path);
        editor.setSelectedFormat(RecordingPreviewBackend::GIF);
        editor.updateVideoSize({160, 120});
        editor.updateDuration(1700);
        editor.updatePosition(637);

        RecordingFilmstrip strip;
        strip.setSize({448, 24}); // eight source-time cells
        QElapsedTimer elapsed;
        elapsed.start();
        strip.setBackend(&editor);
        const auto requestMs = elapsed.elapsed();
        QVERIFY2(requestMs < 100, "Scheduling filmstrip work blocked the editor for 100 ms");
        QTRY_VERIFY_WITH_TIMEOUT(sample(strip, 28).red() > 200, 3000);
        qInfo() << "First visible filmstrip tile (ms):" << elapsed.elapsed() << "GUI request (ms):" << requestMs;
        QTRY_VERIFY(sample(strip, 308).green() > 100); // held green at 1062 ms
        QTRY_VERIFY(sample(strip, 420).blue() > 200);  // blue at 1487 ms
        QCOMPARE(editor.position(), 637);

        QVERIFY(editor.splitAt(850));
        editor.setPartDeleted(0, true);
        editor.setPartSpeed(1, 2.0);
        QTRY_VERIFY(sample(strip, 308).green() > 100);
        QCOMPARE(sample(strip, 28).alpha(), 0);

        const int mask = editor.addMask();
        QVERIFY(mask >= 0);
        QCOMPARE(sample(strip, 308).alpha(), 0); // invalidated synchronously
        QTRY_COMPARE(sample(strip, 308), QColor(Qt::black));
        editor.removeMask(mask);
        strip.setWidth(112);
        strip.setWidth(448);
        QTRY_VERIFY(sample(strip, 308).green() > 100);

        RecordingPreviewBackend missing(QStringLiteral("missing-filmstrip-source.mp4"));
        strip.setBackend(&missing);
        QCOMPARE(sample(strip, 308).alpha(), 0);
        QTest::qWait(150);
        QCOMPARE(sample(strip, 308).alpha(), 0);
    }
};

QTEST_MAIN(tst_RecordingFilmstrip)
#include "tst_RecordingFilmstrip.moc"
