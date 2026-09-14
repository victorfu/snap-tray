#include <QtTest>
#include "colorwidgets/ColorDialog.h"
#include "colorwidgets/GradientSlider.h"

using snaptray::colorwidgets::ColorDialog;
using snaptray::colorwidgets::GradientSlider;

class TestColorDialog : public QObject
{
    Q_OBJECT
private slots:
    void hsvThenRgbPreservesColor()
    {
        ColorDialog dialog(Qt::red);
        auto* hue = dialog.findChild<GradientSlider*>("hueSlider");
        auto* red = dialog.findChild<GradientSlider*>("redSlider");
        auto* green = dialog.findChild<GradientSlider*>("greenSlider");
        auto* blue = dialog.findChild<GradientSlider*>("blueSlider");
        QVERIFY(hue && red && green && blue);
        QSignalSpy changed(&dialog, &ColorDialog::colorChanged);
        hue->setValue(120);
        QCOMPARE(dialog.color().toRgb(), QColor(Qt::green));
        QCOMPARE(red->value(), 0);
        QCOMPARE(green->value(), 255);
        blue->setValue(1);
        QCOMPARE(dialog.color(), QColor(0, 255, 1));
        QCOMPARE(changed.count(), 2);
    }

    void rgbThenHsvPreservesColor()
    {
        ColorDialog dialog(Qt::blue);
        auto* red = dialog.findChild<GradientSlider*>("redSlider");
        auto* hue = dialog.findChild<GradientSlider*>("hueSlider");
        auto* saturation = dialog.findChild<GradientSlider*>("saturationSlider");
        QVERIFY(red && hue && saturation);
        QSignalSpy changed(&dialog, &ColorDialog::colorChanged);
        red->setValue(255);
        QCOMPARE(dialog.color(), QColor(Qt::magenta));
        QCOMPARE(hue->value(), 300);
        saturation->setValue(254);
        QCOMPARE(dialog.color(), QColor::fromHsv(300, 254, 255));
        QCOMPARE(changed.count(), 2);
    }
};

QTEST_MAIN(TestColorDialog)
#include "tst_ColorDialog.moc"
