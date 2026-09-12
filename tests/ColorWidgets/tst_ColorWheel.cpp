#include <QtTest>
#include "colorwidgets/ColorWheel.h"
#include <algorithm>

using snaptray::colorwidgets::ColorWheel;

namespace {
int difference(const QColor& first, const QColor& second)
{
    return std::max({qAbs(first.red() - second.red()), qAbs(first.green() - second.green()),
                     qAbs(first.blue() - second.blue())});
}
}

class TestColorWheel : public QObject
{
    Q_OBJECT
private slots:
    void testTriangleColors_data();
    void testTriangleColors();
    void testTriangleMouseMatchesTexture();
};

void TestColorWheel::testTriangleColors_data()
{
    QTest::addColumn<int>("size");
    QTest::addColumn<int>("space");
    QTest::addColumn<int>("hue");
    for (int size : {100, 200, 360}) {
        for (int space : {int(ColorWheel::ColorHSV), int(ColorWheel::ColorHSL), int(ColorWheel::ColorLCH)}) {
            for (int hue : {0, 120, 240}) {
                QTest::addRow("size-%d-space-%d-hue-%d", size, space, hue) << size << space << hue;
            }
        }
    }
}

void TestColorWheel::testTriangleColors()
{
    QFETCH(int, size);
    QFETCH(int, space);
    QFETCH(int, hue);
    ColorWheel wheel;
    wheel.resize(size, size);
    wheel.setColorSpace(static_cast<ColorWheel::ColorSpaceEnum>(space));
    wheel.setHue(hue);
    wheel.setSaturation(255);
    wheel.setValue(255);
    wheel.renderTriangle();
    const QImage& image = wheel.m_selectorImage;
    QVERIFY(image.width() != image.height());
    QVERIFY(difference(image.pixelColor(image.width() - 1, image.height() - 1), wheel.color()) <= 2);
    QVERIFY(difference(image.pixelColor(image.width() - 1, 0), QColor(Qt::white)) <= 2);
    for (int y = 0; y < image.height(); ++y) {
        QVERIFY(difference(image.pixelColor(0, y), QColor(Qt::black)) <= 2);
    }
    wheel.setSaturation(128);
    wheel.setValue(128);
    const int x = qRound((image.width() - 1) * 128.0 / 255.0);
    const qreal slice = (image.height() - 1) * 128.0 / 255.0;
    const int y = qRound(((image.height() - 1) - slice) / 2.0 + slice * 128.0 / 255.0);
    QVERIFY(difference(image.pixelColor(x, y), wheel.color()) <= 10);
}

void TestColorWheel::testTriangleMouseMatchesTexture()
{
    ColorWheel wheel;
    wheel.resize(360, 360);
    wheel.setColor(Qt::red);
    wheel.renderTriangle();
    constexpr qreal value = 0.65;
    constexpr qreal saturation = 0.7;
    const qreal side = wheel.triangleSide();
    const QPointF selector(value * wheel.triangleHeight(),
                            side * (1 - value) / 2.0 + side * value * saturation);
    QTransform transform;
    transform.translate(wheel.width() / 2.0, wheel.height() / 2.0);
    transform.rotate(wheel.selectorImageAngle());
    transform.translate(wheel.selectorImageOffset().x(), wheel.selectorImageOffset().y());
    QSignalSpy selected(&wheel, &ColorWheel::colorSelected);
    QTest::mouseClick(&wheel, Qt::LeftButton, Qt::NoModifier, transform.map(selector).toPoint());
    QCOMPARE(selected.count(), 1);
    const QImage& image = wheel.m_selectorImage;
    const int x = qRound(value * (image.width() - 1));
    const qreal slice = (image.height() - 1) * value;
    const int y = qRound(((image.height() - 1) - slice) / 2.0 + slice * saturation);
    QVERIFY(difference(image.pixelColor(x, y), wheel.color()) <= 4);
}

QTEST_MAIN(TestColorWheel)
#include "tst_ColorWheel.moc"
