#include <QtTest/QtTest>
#include <QPainter>
#include <QImage>
#include "annotations/StepBadgeAnnotation.h"

namespace {
constexpr int kContrastProbeImageSize = 100;
constexpr QPoint kContrastProbeCenter(50, 50);
constexpr int kContrastProbeHalfSize = 8;
constexpr int kDarkPixelChannelMax = 100;
constexpr int kLightPixelChannelMin = 220;
constexpr int kMinExpectedTextPixels = 12;

QRect contrastProbeRect()
{
    return QRect(kContrastProbeCenter.x() - kContrastProbeHalfSize,
                 kContrastProbeCenter.y() - kContrastProbeHalfSize,
                 (kContrastProbeHalfSize * 2) + 1,
                 (kContrastProbeHalfSize * 2) + 1);
}

QImage renderBadgeForTextContrastProbe(const QColor& fillColor)
{
    StepBadgeAnnotation badge(kContrastProbeCenter, fillColor, 8, StepBadgeAnnotation::kBadgeRadiusLarge);

    QImage image(kContrastProbeImageSize, kContrastProbeImageSize, QImage::Format_ARGB32);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    badge.draw(painter);
    painter.end();
    return image;
}

int countDarkPixelsInProbe(const QImage& image)
{
    int count = 0;
    const QRect probeRect = contrastProbeRect();
    for (int y = probeRect.top(); y <= probeRect.bottom(); ++y) {
        for (int x = probeRect.left(); x <= probeRect.right(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() > 0
                && pixel.red() <= kDarkPixelChannelMax
                && pixel.green() <= kDarkPixelChannelMax
                && pixel.blue() <= kDarkPixelChannelMax) {
                ++count;
            }
        }
    }
    return count;
}

int countLightPixelsInProbe(const QImage& image)
{
    int count = 0;
    const QRect probeRect = contrastProbeRect();
    for (int y = probeRect.top(); y <= probeRect.bottom(); ++y) {
        for (int x = probeRect.left(); x <= probeRect.right(); ++x) {
            const QColor pixel = image.pixelColor(x, y);
            if (pixel.alpha() > 0
                && pixel.red() >= kLightPixelChannelMin
                && pixel.green() >= kLightPixelChannelMin
                && pixel.blue() >= kLightPixelChannelMin) {
                ++count;
            }
        }
    }
    return count;
}
} // namespace

/**
 * @brief Tests for StepBadgeAnnotation class
 *
 * Covers:
 * - Construction with various parameters
 * - Number manipulation
 * - Radius accessors
 * - Size calculations
 * - Bounding rectangle calculations
 * - Clone functionality
 * - Drawing tests
 */
class TestStepBadgeAnnotation : public QObject
{
    Q_OBJECT

private slots:
    // Construction tests
    void testConstruction_Default();
    void testConstruction_WithNumber();
    void testConstruction_WithColor();
    void testConstruction_WithRadius();
    void testConstruction_AllSizes();

    // Number tests
    void testSetNumber();
    void testNumber_SingleDigit();
    void testNumber_MultiDigit();
    void testNumber_LargeNumber();

    // Radius tests
    void testRadius_Small();
    void testRadius_Medium();
    void testRadius_Large();
    void testRadiusForSize();

    // Bounding rect tests
    void testBoundingRect_Small();
    void testBoundingRect_Medium();
    void testBoundingRect_Large();
    void testBoundingRect_ContainsPosition();

    // Clone tests
    void testClone_CreatesNewInstance();
    void testClone_PreservesPosition();
    void testClone_PreservesNumber();
    void testClone_PreservesColor();
    void testClone_PreservesRadius();
    void testClone_PreservesTransform();

    // Transform tests
    void testTransform_Default();
    void testSetRotation_Normalizes();
    void testSetMirror();

    // Drawing tests
    void testDraw_Basic();
    void testDraw_VerifyCircle();
    void testDraw_WithTransform();
    void testDraw_TextColorContrast_data();
    void testDraw_TextColorContrast();

    // Constants tests
    void testConstants();
};

// ============================================================================
// Construction Tests
// ============================================================================

void TestStepBadgeAnnotation::testConstruction_Default()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red);

    QCOMPARE(badge.number(), 1);
    QCOMPARE(badge.radius(), StepBadgeAnnotation::kBadgeRadiusMedium);
    QVERIFY(!badge.boundingRect().isEmpty());
}

void TestStepBadgeAnnotation::testConstruction_WithNumber()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 5);

    QCOMPARE(badge.number(), 5);
}

void TestStepBadgeAnnotation::testConstruction_WithColor()
{
    StepBadgeAnnotation badge(QPoint(50, 50), Qt::blue, 1);

    // Verify by drawing - image must be large enough to contain the badge
    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    badge.draw(painter);
    painter.end();

    // Should have some blue pixels (the badge circle fill)
    bool hasBlue = false;
    for (int y = 0; y < image.height() && !hasBlue; ++y) {
        for (int x = 0; x < image.width() && !hasBlue; ++x) {
            QColor pixel = image.pixelColor(x, y);
            if (pixel.blue() > 200 && pixel.red() < 100 && pixel.green() < 100) {
                hasBlue = true;
            }
        }
    }
    QVERIFY(hasBlue);
}

void TestStepBadgeAnnotation::testConstruction_WithRadius()
{
    StepBadgeAnnotation badgeSmall(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusSmall);
    StepBadgeAnnotation badgeLarge(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusLarge);

    QCOMPARE(badgeSmall.radius(), StepBadgeAnnotation::kBadgeRadiusSmall);
    QCOMPARE(badgeLarge.radius(), StepBadgeAnnotation::kBadgeRadiusLarge);
}

void TestStepBadgeAnnotation::testConstruction_AllSizes()
{
    StepBadgeAnnotation badgeSmall(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusSmall);
    StepBadgeAnnotation badgeMedium(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusMedium);
    StepBadgeAnnotation badgeLarge(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusLarge);

    QVERIFY(badgeSmall.boundingRect().width() < badgeMedium.boundingRect().width());
    QVERIFY(badgeMedium.boundingRect().width() < badgeLarge.boundingRect().width());
}

// ============================================================================
// Number Tests
// ============================================================================

void TestStepBadgeAnnotation::testSetNumber()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1);

    badge.setNumber(10);
    QCOMPARE(badge.number(), 10);
}

void TestStepBadgeAnnotation::testNumber_SingleDigit()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 7);
    QCOMPARE(badge.number(), 7);
}

void TestStepBadgeAnnotation::testNumber_MultiDigit()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 42);
    QCOMPARE(badge.number(), 42);
}

void TestStepBadgeAnnotation::testNumber_LargeNumber()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 999);
    QCOMPARE(badge.number(), 999);
}

// ============================================================================
// Radius Tests
// ============================================================================

void TestStepBadgeAnnotation::testRadius_Small()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusSmall);
    QCOMPARE(badge.radius(), StepBadgeAnnotation::kBadgeRadiusSmall);
}

void TestStepBadgeAnnotation::testRadius_Medium()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusMedium);
    QCOMPARE(badge.radius(), StepBadgeAnnotation::kBadgeRadiusMedium);
}

void TestStepBadgeAnnotation::testRadius_Large()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusLarge);
    QCOMPARE(badge.radius(), StepBadgeAnnotation::kBadgeRadiusLarge);
}

void TestStepBadgeAnnotation::testRadiusForSize()
{
    QCOMPARE(StepBadgeAnnotation::radiusForSize(StepBadgeSize::Small), StepBadgeAnnotation::kBadgeRadiusSmall);
    QCOMPARE(StepBadgeAnnotation::radiusForSize(StepBadgeSize::Medium), StepBadgeAnnotation::kBadgeRadiusMedium);
    QCOMPARE(StepBadgeAnnotation::radiusForSize(StepBadgeSize::Large), StepBadgeAnnotation::kBadgeRadiusLarge);
}

// ============================================================================
// Bounding Rect Tests
// ============================================================================

void TestStepBadgeAnnotation::testBoundingRect_Small()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusSmall);

    QRect rect = badge.boundingRect();
    QVERIFY(!rect.isEmpty());
    // Should be approximately 2 * radius
    QVERIFY(rect.width() >= 2 * StepBadgeAnnotation::kBadgeRadiusSmall);
    QVERIFY(rect.height() >= 2 * StepBadgeAnnotation::kBadgeRadiusSmall);
}

void TestStepBadgeAnnotation::testBoundingRect_Medium()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusMedium);

    QRect rect = badge.boundingRect();
    QVERIFY(!rect.isEmpty());
    QVERIFY(rect.width() >= 2 * StepBadgeAnnotation::kBadgeRadiusMedium);
}

void TestStepBadgeAnnotation::testBoundingRect_Large()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusLarge);

    QRect rect = badge.boundingRect();
    QVERIFY(!rect.isEmpty());
    QVERIFY(rect.width() >= 2 * StepBadgeAnnotation::kBadgeRadiusLarge);
}

void TestStepBadgeAnnotation::testBoundingRect_ContainsPosition()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1);

    QRect rect = badge.boundingRect();
    QVERIFY(rect.contains(QPoint(100, 100)));
}

// ============================================================================
// Clone Tests
// ============================================================================

void TestStepBadgeAnnotation::testClone_CreatesNewInstance()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 5);

    auto cloned = badge.clone();
    QVERIFY(cloned != nullptr);
    QVERIFY(cloned.get() != &badge);
}

void TestStepBadgeAnnotation::testClone_PreservesPosition()
{
    StepBadgeAnnotation badge(QPoint(150, 200), Qt::red, 1);

    auto cloned = badge.clone();
    QCOMPARE(cloned->boundingRect().center(), badge.boundingRect().center());
}

void TestStepBadgeAnnotation::testClone_PreservesNumber()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 42);

    auto cloned = badge.clone();
    auto* clonedBadge = dynamic_cast<StepBadgeAnnotation*>(cloned.get());

    QVERIFY(clonedBadge != nullptr);
    QCOMPARE(clonedBadge->number(), 42);
}

void TestStepBadgeAnnotation::testClone_PreservesColor()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::green, 1);

    auto cloned = badge.clone();

    // Draw both and compare
    QImage img1(50, 50, QImage::Format_ARGB32);
    QImage img2(50, 50, QImage::Format_ARGB32);
    img1.fill(Qt::white);
    img2.fill(Qt::white);

    QPainter p1(&img1);
    QPainter p2(&img2);
    badge.draw(p1);
    cloned->draw(p2);

    QCOMPARE(img1, img2);
}

void TestStepBadgeAnnotation::testClone_PreservesRadius()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusLarge);

    auto cloned = badge.clone();
    auto* clonedBadge = dynamic_cast<StepBadgeAnnotation*>(cloned.get());

    QVERIFY(clonedBadge != nullptr);
    QCOMPARE(clonedBadge->radius(), StepBadgeAnnotation::kBadgeRadiusLarge);
}

void TestStepBadgeAnnotation::testClone_PreservesTransform()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1);
    badge.setRotation(270.0);
    badge.setMirror(true, true);

    auto cloned = badge.clone();
    auto* clonedBadge = dynamic_cast<StepBadgeAnnotation*>(cloned.get());

    QVERIFY(clonedBadge != nullptr);
    QCOMPARE(clonedBadge->rotation(), 270.0);
    QCOMPARE(clonedBadge->mirrorX(), true);
    QCOMPARE(clonedBadge->mirrorY(), true);
}

// ============================================================================
// Transform Tests
// ============================================================================

void TestStepBadgeAnnotation::testTransform_Default()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1);

    QCOMPARE(badge.rotation(), 0.0);
    QCOMPARE(badge.mirrorX(), false);
    QCOMPARE(badge.mirrorY(), false);
}

void TestStepBadgeAnnotation::testSetRotation_Normalizes()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1);

    badge.setRotation(450.0);
    QCOMPARE(badge.rotation(), 90.0);

    badge.setRotation(-90.0);
    QCOMPARE(badge.rotation(), 270.0);
}

void TestStepBadgeAnnotation::testSetMirror()
{
    StepBadgeAnnotation badge(QPoint(100, 100), Qt::red, 1);

    badge.setMirror(true, false);
    QCOMPARE(badge.mirrorX(), true);
    QCOMPARE(badge.mirrorY(), false);

    badge.setMirror(false, true);
    QCOMPARE(badge.mirrorX(), false);
    QCOMPARE(badge.mirrorY(), true);
}

// ============================================================================
// Drawing Tests
// ============================================================================

void TestStepBadgeAnnotation::testDraw_Basic()
{
    StepBadgeAnnotation badge(QPoint(25, 25), Qt::red, 1);

    QImage image(50, 50, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    badge.draw(painter);
    painter.end();

    // Verify something was drawn
    bool hasColor = false;
    for (int y = 0; y < image.height() && !hasColor; ++y) {
        for (int x = 0; x < image.width() && !hasColor; ++x) {
            if (image.pixel(x, y) != qRgb(255, 255, 255)) {
                hasColor = true;
            }
        }
    }
    QVERIFY(hasColor);
}

void TestStepBadgeAnnotation::testDraw_VerifyCircle()
{
    StepBadgeAnnotation badge(QPoint(50, 50), Qt::red, 1, StepBadgeAnnotation::kBadgeRadiusLarge);

    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);

    badge.draw(painter);
    painter.end();

    // Check for red pixels in the center area (the circle)
    bool hasRedInCenter = false;
    for (int y = 40; y < 60 && !hasRedInCenter; ++y) {
        for (int x = 40; x < 60 && !hasRedInCenter; ++x) {
            QColor pixel = image.pixelColor(x, y);
            if (pixel.red() > 200 && pixel.green() < 100 && pixel.blue() < 100) {
                hasRedInCenter = true;
            }
        }
    }
    QVERIFY(hasRedInCenter);
}

void TestStepBadgeAnnotation::testDraw_WithTransform()
{
    StepBadgeAnnotation badge(QPoint(50, 50), Qt::red, 12, StepBadgeAnnotation::kBadgeRadiusLarge);
    badge.setRotation(90.0);
    badge.setMirror(true, false);

    QImage image(100, 100, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    badge.draw(painter);
    painter.end();

    bool hasNonWhitePixel = false;
    for (int y = 0; y < image.height() && !hasNonWhitePixel; ++y) {
        for (int x = 0; x < image.width() && !hasNonWhitePixel; ++x) {
            if (image.pixel(x, y) != qRgb(255, 255, 255)) {
                hasNonWhitePixel = true;
            }
        }
    }
    QVERIFY(hasNonWhitePixel);
}

void TestStepBadgeAnnotation::testDraw_TextColorContrast_data()
{
    QTest::addColumn<QColor>("fillColor");
    QTest::addColumn<bool>("expectDarkText");

    QTest::newRow("yellow_fill_uses_dark_text") << QColor(255, 240, 120) << true;
    QTest::newRow("white_fill_uses_dark_text") << QColor(Qt::white) << true;
    QTest::newRow("red_fill_keeps_light_text") << QColor(Qt::red) << false;
}

void TestStepBadgeAnnotation::testDraw_TextColorContrast()
{
    QFETCH(QColor, fillColor);
    QFETCH(bool, expectDarkText);

    const QImage image = renderBadgeForTextContrastProbe(fillColor);
    const int darkPixelCount = countDarkPixelsInProbe(image);
    const int lightPixelCount = countLightPixelsInProbe(image);

    if (expectDarkText) {
        QVERIFY2(darkPixelCount >= kMinExpectedTextPixels,
                 qPrintable(QString("Expected dark text pixels for fill %1, dark=%2, light=%3")
                            .arg(fillColor.name())
                            .arg(darkPixelCount)
                            .arg(lightPixelCount)));
        return;
    }

    QVERIFY2(lightPixelCount >= kMinExpectedTextPixels,
             qPrintable(QString("Expected light text pixels for fill %1, dark=%2, light=%3")
                        .arg(fillColor.name())
                        .arg(darkPixelCount)
                        .arg(lightPixelCount)));
}

// ============================================================================
// Constants Tests
// ============================================================================

void TestStepBadgeAnnotation::testConstants()
{
    QVERIFY(StepBadgeAnnotation::kBadgeRadiusSmall > 0);
    QVERIFY(StepBadgeAnnotation::kBadgeRadiusMedium > StepBadgeAnnotation::kBadgeRadiusSmall);
    QVERIFY(StepBadgeAnnotation::kBadgeRadiusLarge > StepBadgeAnnotation::kBadgeRadiusMedium);
}

QTEST_MAIN(TestStepBadgeAnnotation)
#include "tst_StepBadgeAnnotation.moc"
