#include <QtTest/QtTest>

#include "capture/CaptureDisplayResolver.h"

using SnapTray::CaptureDisplayCandidate;
using SnapTray::CaptureDisplayResolution;
using SnapTray::findCaptureDisplayId;
using SnapTray::resolveCaptureDisplay;

Q_DECLARE_METATYPE(QList<SnapTray::CaptureDisplayCandidate>)

namespace {
constexpr quint32 kMainId = 1;
constexpr quint32 kSelectedId = 7;
const QRect kMainBounds(0, 0, 1512, 982);
const QRect kSelectedBounds(1512, -200, 2560, 1440);
} // namespace

class TestCaptureDisplayResolver : public QObject
{
    Q_OBJECT

private slots:
    void resolveNeverSubstitutesAnotherDisplay_data();
    void resolveNeverSubstitutesAnotherDisplay();
    void findIdentifiesOnlyUnambiguousGeometry_data();
    void findIdentifiesOnlyUnambiguousGeometry();
    void sckSourceHasNoMainOrFirstDisplayFallback();
};

void TestCaptureDisplayResolver::resolveNeverSubstitutesAnotherDisplay_data()
{
    QTest::addColumn<quint32>("requestedId");
    QTest::addColumn<QList<CaptureDisplayCandidate>>("shareable");
    QTest::addColumn<int>("expected");

    const CaptureDisplayCandidate main{kMainId, kMainBounds};
    const CaptureDisplayCandidate selected{kSelectedId, kSelectedBounds};

    QTest::newRow("selected present")
        << kSelectedId << QList<CaptureDisplayCandidate>{main, selected}
        << int(CaptureDisplayResolution::Resolved);
    QTest::newRow("list order changed")
        << kSelectedId << QList<CaptureDisplayCandidate>{selected, main}
        << int(CaptureDisplayResolution::Resolved);
    QTest::newRow("requested id missing, other display first")
        << kSelectedId << QList<CaptureDisplayCandidate>{main}
        << int(CaptureDisplayResolution::NotShareable);
    QTest::newRow("empty content")
        << kSelectedId << QList<CaptureDisplayCandidate>{}
        << int(CaptureDisplayResolution::NotShareable);
    QTest::newRow("identity unknown")
        << quint32(0) << QList<CaptureDisplayCandidate>{main, selected}
        << int(CaptureDisplayResolution::IdentityUnknown);
    QTest::newRow("selected moved")
        << kSelectedId
        << QList<CaptureDisplayCandidate>{main, {kSelectedId, kSelectedBounds.translated(0, 200)}}
        << int(CaptureDisplayResolution::GeometryChanged);
    QTest::newRow("selected resized")
        << kSelectedId
        << QList<CaptureDisplayCandidate>{main, {kSelectedId, QRect(1512, -200, 1920, 1080)}}
        << int(CaptureDisplayResolution::GeometryChanged);
    QTest::newRow("another display took the selected geometry")
        << kSelectedId << QList<CaptureDisplayCandidate>{main, {kMainId + 1, kSelectedBounds}}
        << int(CaptureDisplayResolution::NotShareable);
}

void TestCaptureDisplayResolver::resolveNeverSubstitutesAnotherDisplay()
{
    QFETCH(quint32, requestedId);
    QFETCH(QList<CaptureDisplayCandidate>, shareable);
    QFETCH(int, expected);

    QCOMPARE(int(resolveCaptureDisplay(requestedId, kSelectedBounds, shareable)), expected);
}

void TestCaptureDisplayResolver::findIdentifiesOnlyUnambiguousGeometry_data()
{
    QTest::addColumn<QList<CaptureDisplayCandidate>>("displays");
    QTest::addColumn<quint32>("expected");

    const CaptureDisplayCandidate main{kMainId, kMainBounds};
    const CaptureDisplayCandidate selected{kSelectedId, kSelectedBounds};

    QTest::newRow("match") << QList<CaptureDisplayCandidate>{main, selected} << kSelectedId;
    QTest::newRow("match listed first") << QList<CaptureDisplayCandidate>{selected, main}
                                        << kSelectedId;
    QTest::newRow("no match does not fall back to main")
        << QList<CaptureDisplayCandidate>{main} << quint32(0);
    QTest::newRow("no displays") << QList<CaptureDisplayCandidate>{} << quint32(0);
    QTest::newRow("ambiguous geometry")
        << QList<CaptureDisplayCandidate>{selected, {kSelectedId + 1, kSelectedBounds}}
        << quint32(0);
}

void TestCaptureDisplayResolver::findIdentifiesOnlyUnambiguousGeometry()
{
    QFETCH(QList<CaptureDisplayCandidate>, displays);
    QFETCH(quint32, expected);

    QCOMPARE(findCaptureDisplayId(kSelectedBounds, displays), expected);
}

void TestCaptureDisplayResolver::sckSourceHasNoMainOrFirstDisplayFallback()
{
    QFile source(QDir(QStringLiteral(CAPTURE_SOURCE_ROOT))
                     .filePath(QStringLiteral("SCKCaptureEngine_mac.mm")));
    QVERIFY2(source.open(QIODevice::ReadOnly), qPrintable(source.errorString()));
    const QByteArray content = source.readAll();

    QVERIFY(content.contains("resolveCaptureDisplay"));
    QVERIFY(!content.contains("CGMainDisplayID"));
    QVERIFY(!content.contains("displays.firstObject"));
}

QTEST_GUILESS_MAIN(TestCaptureDisplayResolver)
#include "tst_CaptureDisplayResolver.moc"
