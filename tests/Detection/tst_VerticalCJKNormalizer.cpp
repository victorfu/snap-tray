#include <QtTest>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QPainter>
#include <QTransform>
#include <cstdlib>

#include "utils/VerticalCJKNormalizer.h"
#include "VerticalCJKSamples.h"

using VerticalCJKNormalizer::Rejection;
using VerticalCJKSamples::Style;
using VerticalCJKSamples::cjkFamily;
using VerticalCJKSamples::renderVertical;

Q_DECLARE_METATYPE(VerticalCJKNormalizer::Rejection)

namespace {

QImage renderLines(const QStringList& lines, const QFont& font, int width)
{
    const QFontMetrics metrics(font);
    const int lineHeight = metrics.lineSpacing() + metrics.lineSpacing() / 3;
    QImage image(width, 2 * lineHeight + lines.size() * lineHeight, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setFont(font);
    painter.setPen(Qt::black);
    for (int i = 0; i < lines.size(); ++i) {
        painter.drawText(QPoint(8, lineHeight + i * lineHeight + metrics.ascent()), lines[i]);
    }
    return image;
}

} // namespace

class TestVerticalCJKNormalizer : public QObject
{
    Q_OBJECT
private slots:
    void acceptsSingleUprightColumn_data();
    void acceptsSingleUprightColumn();
    void outputReadsLeftToRightInSourceOrder();
    void rejectsOutOfScopeLayouts_data();
    void rejectsOutOfScopeLayouts();
    void rejectsBlankAndInvalidImages();
    void boundsHugeSources();
};

void TestVerticalCJKNormalizer::acceptsSingleUprightColumn_data()
{
    QTest::addColumn<QString>("text");
    QTest::addColumn<int>("pixelSize");
    QTest::addColumn<QColor>("foreground");
    QTest::addColumn<QColor>("background");
    QTest::addColumn<int>("extraWidth");

    QTest::newRow("zh-Hant") << QStringLiteral("直排文字測試") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("zh-Hans") << QStringLiteral("竖排文字识别") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("ja") << QStringLiteral("縦書きテキスト") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("ja-kana") << QStringLiteral("ひらがなのしくみといろは") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("ja-mixed") << QStringLiteral("吾輩は猫である。名前はまだ無い。") << 32 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("zh-Hant-poem") << QStringLiteral("春眠不覺曉處處聞啼鳥") << 24 << QColor(Qt::black) << QColor(Qt::white) << 0;
    // Short columns and separated strokes are ordinary single columns.
    QTest::newRow("two-chars-ri-ben") << QStringLiteral("日本") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("two-chars-ri-yue") << QStringLiteral("日月") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("separated-strokes-xiao-chuan") << QStringLiteral("小川") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("separated-strokes-chuan-chuan") << QStringLiteral("川川") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("detached-strokes") << QStringLiteral("三川小六二") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("punctuation") << QStringLiteral("你好，世界。") << 40 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("dark-background") << QStringLiteral("直排文字測試") << 40 << QColor(235, 235, 235) << QColor(30, 30, 30) << 0;
    QTest::newRow("low-contrast") << QStringLiteral("直排文字測試") << 40 << QColor(165, 165, 165) << QColor(205, 205, 205) << 0;
    QTest::newRow("small") << QStringLiteral("直排文字測試") << 14 << QColor(Qt::black) << QColor(Qt::white) << 0;
    QTest::newRow("large") << QStringLiteral("直排文字測試") << 120 << QColor(Qt::black) << QColor(Qt::white) << 0;
    // A loose selection: wide blank margins must not defeat the column checks.
    QTest::newRow("wide-selection") << QStringLiteral("直排文字測試") << 40 << QColor(Qt::black) << QColor(Qt::white) << 400;
}

void TestVerticalCJKNormalizer::acceptsSingleUprightColumn()
{
    QFETCH(QString, text);
    QFETCH(int, pixelSize);
    QFETCH(QColor, foreground);
    QFETCH(QColor, background);
    QFETCH(int, extraWidth);
    const QString family = cjkFamily(text);
    if (family.isEmpty()) {
        QSKIP("No installed CJK font covers this sample");
    }
    Style style;
    style.pixelSize = pixelSize;
    style.foreground = foreground;
    style.background = background;
    style.extraWidth = extraWidth;
    const auto result = VerticalCJKNormalizer::normalize(renderVertical(text, family, style));
    QCOMPARE(result.rejection, Rejection::None);
    QVERIFY(result.isValid());
    QCOMPARE(result.glyphCount, static_cast<int>(text.size()));
    // Glyphs now sit side by side; even two of them make a wide row.
    QVERIFY2(result.image.width() > result.image.height(),
             qPrintable(QStringLiteral("%1x%2").arg(result.image.width()).arg(result.image.height())));
}

void TestVerticalCJKNormalizer::outputReadsLeftToRightInSourceOrder()
{
    // Solid blocks of distinct gray levels make the order checkable without OCR.
    const QList<int> grays{0, 60, 120};
    const int cell = 50;
    QImage source(80, 40 + grays.size() * (cell + 10), QImage::Format_RGB32);
    source.fill(Qt::white);
    {
        QPainter painter(&source);
        for (int i = 0; i < grays.size(); ++i) {
            painter.fillRect(QRect(15, 20 + i * (cell + 10), cell, cell), QColor(grays[i], grays[i], grays[i]));
        }
    }
    const auto result = VerticalCJKNormalizer::normalize(source);
    QVERIFY(result.isValid());
    QCOMPARE(result.glyphCount, static_cast<int>(grays.size()));
    const int midY = result.image.height() / 2;
    QList<int> seen;
    int previous = 255;
    for (int x = 0; x < result.image.width(); ++x) {
        const int gray = qGray(result.image.pixel(x, midY));
        if (gray < 200 && previous >= 200) {
            seen.append(gray);
        }
        previous = gray;
    }
    QCOMPARE(seen.size(), grays.size());
    for (int i = 0; i < grays.size(); ++i) {
        QVERIFY2(std::abs(seen[i] - grays[i]) <= 4, qPrintable(QString::number(seen[i])));
    }
}

void TestVerticalCJKNormalizer::rejectsOutOfScopeLayouts_data()
{
    QTest::addColumn<QImage>("image");
    QTest::addColumn<Rejection>("expected");

    const QString vertical = QStringLiteral("直排文字測試範例");
    const QString family = cjkFamily(vertical);
    QFont latin(QStringLiteral("Helvetica"));
    latin.setPixelSize(16);

    QTest::newRow("horizontal-line")
        << renderLines({QStringLiteral("A normal horizontal sentence")}, latin, 320) << Rejection::NotTallColumn;
    QTest::newRow("narrow-ui-list")
        << renderLines({QStringLiteral("Open"), QStringLiteral("Save As"), QStringLiteral("Export"),
                        QStringLiteral("Settings"), QStringLiteral("Recent"), QStringLiteral("Quit"),
                        QStringLiteral("About"), QStringLiteral("Help")},
                       latin, 90)
        << Rejection::NotGlyphLike;
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPixelSize(14);
    QTest::newRow("code")
        << renderLines({QStringLiteral("int x = 0;"), QStringLiteral("x += 1;"), QStringLiteral("if (x)"),
                        QStringLiteral("  run();"), QStringLiteral("return x;"), QStringLiteral("}")},
                       mono, 110)
        << Rejection::NotGlyphLike;

    QFont rotatedFont(QStringLiteral("Helvetica"));
    rotatedFont.setPixelSize(28);
    const QImage horizontal = renderLines({QStringLiteral("Rotated horizontal words")}, rotatedFont, 360);
    QTest::newRow("rotated-horizontal-latin")
        << horizontal.transformed(QTransform().rotate(90)) << Rejection::NotGlyphLike;
    const QImage descenders = renderLines({QStringLiteral("Typography quietly jumps")}, rotatedFont, 360);
    QTest::newRow("rotated-latin-descenders")
        << descenders.transformed(QTransform().rotate(-90)) << Rejection::NotGlyphLike;

    if (!family.isEmpty()) {
        Style twoColumns;
        twoColumns.columns = 2;
        QTest::newRow("multi-column-vertical")
            << renderVertical(vertical + vertical, family, twoColumns) << Rejection::MultipleColumns;
        // The 小川 counterpart: separated strokes in each of two real columns.
        const QString strokes = QStringLiteral("小川三川小川");
        if (!cjkFamily(strokes).isEmpty()) {
            QTest::newRow("multi-column-separated-strokes")
                << renderVertical(strokes + strokes, cjkFamily(strokes), twoColumns) << Rejection::MultipleColumns;
        }
    }
}

void TestVerticalCJKNormalizer::rejectsOutOfScopeLayouts()
{
    QFETCH(QImage, image);
    QFETCH(Rejection, expected);
    const auto result = VerticalCJKNormalizer::normalize(image);
    QCOMPARE(result.rejection, expected);
    QVERIFY(result.image.isNull());
    QVERIFY(!result.isValid());
}

void TestVerticalCJKNormalizer::rejectsBlankAndInvalidImages()
{
    QCOMPARE(VerticalCJKNormalizer::normalize(QImage()).rejection, Rejection::InvalidImage);
    QImage blank(60, 400, QImage::Format_RGB32);
    blank.fill(QColor(240, 240, 240));
    QCOMPARE(VerticalCJKNormalizer::normalize(blank).rejection, Rejection::NoForeground);
    QImage transparent(60, 400, QImage::Format_ARGB32);
    transparent.fill(Qt::transparent);
    QCOMPARE(VerticalCJKNormalizer::normalize(transparent).rejection, Rejection::NoForeground);
}

void TestVerticalCJKNormalizer::boundsHugeSources()
{
    QImage huge(2000, 12000, QImage::Format_RGB32);
    huge.fill(Qt::white);
    {
        QPainter painter(&huge);
        for (int i = 0; i < 8; ++i) {
            painter.fillRect(QRect(300, 200 + i * 1400, 1200, 1200), Qt::black);
        }
    }
    const auto result = VerticalCJKNormalizer::normalize(huge);
    QVERIFY(result.isValid());
    QCOMPARE(result.glyphCount, 8);
    // Work is done on a downscaled copy, so output stays bounded too.
    QVERIFY(result.image.height() <= VerticalCJKNormalizer::kMaxSourceDimension);
}

QTEST_MAIN(TestVerticalCJKNormalizer)
#include "tst_VerticalCJKNormalizer.moc"
