#include <QtTest>

#include "qml/PinHoverBarViewModel.h"

// #225: the pin hover bar's values, limits, compact layout and requests.
class tst_PinHoverBarViewModel : public QObject
{
    Q_OBJECT
private slots:
    void valuesRoundToPercent();
    void zoomRequestIsClamped();
    void opacityRequestIsClamped();
    void compactBelowThreshold();
    void fitsMinimum();
    void panelToggles();
    void statusToggle();
};

void tst_PinHoverBarViewModel::valuesRoundToPercent()
{
    PinHoverBarViewModel vm;
    QSignalSpy changed(&vm, &PinHoverBarViewModel::valuesChanged);
    vm.setValues(1.504, 0.1, 0.8);
    QCOMPARE(vm.zoomPercent(), 150);
    QCOMPARE(vm.minZoomPercent(), 10);
    QCOMPARE(vm.opacityPercent(), 80);
    QCOMPARE(changed.count(), 1);
    vm.setValues(1.501, 0.1, 0.8);   // same rounded values: no signal
    QCOMPARE(changed.count(), 1);
}

void tst_PinHoverBarViewModel::zoomRequestIsClamped()
{
    PinHoverBarViewModel vm;
    vm.setValues(1.0, 0.25, 1.0);
    QSignalSpy zoom(&vm, &PinHoverBarViewModel::zoomRequested);
    vm.requestZoomPercent(10);
    vm.requestZoomPercent(900);
    vm.requestZoomPercent(150);
    QCOMPARE(zoom.count(), 3);
    QCOMPARE(zoom.at(0).at(0).toReal(), 0.25);
    QCOMPARE(zoom.at(1).at(0).toReal(), 5.0);
    QCOMPARE(zoom.at(2).at(0).toReal(), 1.5);
}

void tst_PinHoverBarViewModel::opacityRequestIsClamped()
{
    PinHoverBarViewModel vm;
    QSignalSpy opacity(&vm, &PinHoverBarViewModel::opacityRequested);
    vm.requestOpacityPercent(5);
    vm.requestOpacityPercent(150);
    QCOMPARE(opacity.at(0).at(0).toReal(), 0.1);
    QCOMPARE(opacity.at(1).at(0).toReal(), 1.0);
}

void tst_PinHoverBarViewModel::compactBelowThreshold()
{
    PinHoverBarViewModel vm;
    QSignalSpy compact(&vm, &PinHoverBarViewModel::compactChanged);
    vm.setAvailableWidth(PinHoverBarViewModel::kCompactWidth - 1);
    QVERIFY(vm.compact());
    vm.setAvailableWidth(PinHoverBarViewModel::kCompactWidth - 5);
    QCOMPARE(compact.count(), 1);
    vm.setAvailableWidth(PinHoverBarViewModel::kCompactWidth);
    QVERIFY(!vm.compact());
    QCOMPARE(compact.count(), 2);
}

void tst_PinHoverBarViewModel::fitsMinimum()
{
    PinHoverBarViewModel vm;
    QVERIFY(!vm.fits(PinHoverBarViewModel::kMinimumWidth - 1));
    QVERIFY(vm.fits(PinHoverBarViewModel::kMinimumWidth));
}

void tst_PinHoverBarViewModel::panelToggles()
{
    PinHoverBarViewModel vm;
    QSignalSpy panel(&vm, &PinHoverBarViewModel::panelChanged);
    vm.openPanel(PinHoverBarViewModel::ZoomPanel);
    QCOMPARE(vm.panel(), int(PinHoverBarViewModel::ZoomPanel));
    vm.openPanel(PinHoverBarViewModel::ZoomPanel);   // the same button closes it
    QCOMPARE(vm.panel(), int(PinHoverBarViewModel::NoPanel));
    vm.openPanel(PinHoverBarViewModel::OpacityPanel);
    vm.closePanel();
    QCOMPARE(vm.panel(), int(PinHoverBarViewModel::NoPanel));
    QCOMPARE(panel.count(), 4);
}

void tst_PinHoverBarViewModel::statusToggle()
{
    PinHoverBarViewModel vm;
    QSignalSpy pause(&vm, &PinHoverBarViewModel::pauseRequested);
    QSignalSpy resume(&vm, &PinHoverBarViewModel::resumeRequested);
    vm.toggleStatus();   // no Live: nothing
    vm.setStatus(PinHoverBarViewModel::Live);
    vm.toggleStatus();
    vm.setStatus(PinHoverBarViewModel::Paused);
    vm.toggleStatus();
    QCOMPARE(pause.count(), 1);
    QCOMPARE(resume.count(), 1);
}

QTEST_MAIN(tst_PinHoverBarViewModel)
#include "tst_PinHoverBarViewModel.moc"
