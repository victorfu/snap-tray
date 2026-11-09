// Screenshots and an operation video of the pin hover controls (#225), for
// docs/developer/ui-design-guidelines.md. Runs only when its variables are set:
//   SNAPTRAY_PIN_DESIGN_DIR     screenshots
//   SNAPTRAY_PIN_TRANSLATION    a .qm to load (language added to the names)
//   SNAPTRAY_PIN_VIDEO_DIR      frames for the operation video
// The pin, its bar, panel and click-through label are separate windows; each
// is grabbed and composited at its real position over a desktop backdrop.
#include <QtTest>
#include <QEnterEvent>
#include <QFileInfo>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTranslator>
#include <QProcess>
#include <functional>
#ifdef Q_OS_WIN
#include <QtCore/qt_windows.h>
#endif
#include <QtQml/qqmlextensionplugin.h>

#include "PinWindow.h"
#include "PlatformFeatures.h"
#include "ToolbarStyle.h"
#include "pinwindow/ClickThroughExitButton.h"
#include "pinwindow/PinHoverBar.h"
#include "qml/PinHoverBarViewModel.h"
#include "ui/DesignSystem.h"

Q_IMPORT_QML_PLUGIN(SnapTrayQmlPlugin)

namespace {
constexpr int kMargin = 60;
constexpr int kCaption = 40;

QPixmap demoImage(QSize size)
{
    QPixmap pixmap(size);
    QPainter p(&pixmap);
    p.fillRect(pixmap.rect(), QColor("#fbfbfd"));
    p.fillRect(QRect(0, 0, size.width(), 34), QColor("#e9eaf0"));
    p.setPen(QColor("#4b4f5c"));
    QFont font = p.font();
    font.setPixelSize(15);
    font.setBold(true);
    p.setFont(font);
    p.drawText(QRect(16, 0, size.width(), 34), Qt::AlignVCenter, QStringLiteral("Release checklist"));
    font.setBold(false);
    font.setPixelSize(13);
    p.setFont(font);
    for (int i = 0; i < 6; ++i) {
        p.setBrush(i < 3 ? QColor("#34c759") : QColor("#d1d4dc"));
        p.setPen(Qt::NoPen);
        p.drawEllipse(QRectF(18, 52 + i * 34, 14, 14));
        p.setPen(QColor("#4b4f5c"));
        p.drawText(QRect(42, 46 + i * 34, size.width() - 60, 26), Qt::AlignVCenter,
                   QStringList{"Bump version", "Update changelog", "Tag the release", "Build installers",
                               "Notarize macOS app", "Publish release notes"}.at(i));
    }
    return pixmap;
}

void setTheme(bool dark)
{
    ToolbarStyleConfig::saveStyle(dark ? ToolbarStyleType::Dark : ToolbarStyleType::Light);
    DesignSystem::instance().refreshTheme();
}
} // namespace

class TestPinWindowEvidence : public QObject
{
    Q_OBJECT
private slots:
    void screenshots_data();
    void screenshots();
    void operationVideo();
    void nativeControlsAndZOrder();

private:
    // Composites the pin and its floating windows over a backdrop.
    static QImage compose(PinWindow& pin, const QPoint& cursor = QPoint(-1, -1), bool pressed = false,
                          const QRect& fixedArea = QRect());
    static void hover(PinWindow& pin);
    static void settle();
};

void TestPinWindowEvidence::settle()
{
    for (int i = 0; i < 4; ++i) {
        QTest::qWait(60);
        QCoreApplication::processEvents();
    }
}

void TestPinWindowEvidence::hover(PinWindow& pin)
{
    QEnterEvent enter(QPointF(100, 100), QPointF(100, 100), pin.mapToGlobal(QPointF(100, 100)));
    QCoreApplication::sendEvent(&pin, &enter);
    pin.m_hoverBar->setCursorPositionProvider([&pin] { return pin.mapToGlobal(QPoint(100, 100)); });
    pin.m_hoverBar->showNow();
    settle();
}

QImage TestPinWindowEvidence::compose(PinWindow& pin, const QPoint& cursor, bool pressed,
                                      const QRect& fixedArea)
{
    const QRect pinRect = pin.frameGeometry();
    QRect area = pinRect.adjusted(-kMargin, -kMargin, kMargin, kMargin);
    if (pin.m_hoverBar && pin.m_hoverBar->isPanelVisible())
        area = area.united(pin.m_hoverBar->panelWindow()->geometry().adjusted(-20, -20, 20, 20));
    if (fixedArea.isValid()) area = fixedArea;
    QImage out(area.size(), QImage::Format_ARGB32_Premultiplied);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    // Grabs are scaled into logical geometry at 1.5x and 2x; filter them.
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QLinearGradient backdrop(0, 0, area.width(), area.height());
    const bool dark = ToolbarStyleConfig::loadStyle() == ToolbarStyleType::Dark;
    backdrop.setColorAt(0, dark ? QColor("#1f2433") : QColor("#9fb7d6"));
    backdrop.setColorAt(1, dark ? QColor("#38283f") : QColor("#d8c3d9"));
    p.fillRect(out.rect(), backdrop);
    // Each grab is drawn into its window's logical geometry. The grab's own
    // ratio can't be trusted: the software scene graph returns a device-pixel
    // QQuickWindow grab tagged with ratio 1 (seen at 1.5x and 2x on X11).
    const auto draw = [&](const QImage& image, const QRect& globalGeometry) {
        QImage copy = image;
        copy.setDevicePixelRatio(1.0);
        p.drawImage(globalGeometry.translated(-area.topLeft()), copy);
    };
    p.setOpacity(pin.opacity());
    draw(pin.grab().toImage(), pinRect);
    p.setOpacity(1.0);
    if (auto* pill = pin.findChild<ClickThroughExitButton*>(); pill && pill->isVisible())
        draw(pill->grab().toImage(), pill->geometry());
    if (pin.m_hoverBar && pin.m_hoverBar->isBarVisible())
        draw(static_cast<QQuickWindow*>(pin.m_hoverBar->barWindow())->grabWindow(),
             pin.m_hoverBar->barGeometry());
    if (pin.m_hoverBar && pin.m_hoverBar->isPanelVisible())
        draw(static_cast<QQuickWindow*>(pin.m_hoverBar->panelWindow())->grabWindow(),
             pin.m_hoverBar->panelWindow()->geometry());
    if (cursor.x() >= 0) {
        const QPointF c = cursor - area.topLeft();
        QPainterPath arrow;
        arrow.moveTo(c);
        arrow.lineTo(c + QPointF(0, 17));
        arrow.lineTo(c + QPointF(4.5, 13));
        arrow.lineTo(c + QPointF(8, 20));
        arrow.lineTo(c + QPointF(10.5, 19));
        arrow.lineTo(c + QPointF(7, 12));
        arrow.lineTo(c + QPointF(12.5, 12));
        arrow.closeSubpath();
        if (pressed) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(255, 196, 0, 110));
            p.drawEllipse(c, 14, 14);
        }
        p.setPen(QPen(Qt::white, 1.5));
        p.setBrush(Qt::black);
        p.drawPath(arrow);
    }
    p.end();
    return out;
}

void TestPinWindowEvidence::screenshots_data()
{
    QTest::addColumn<bool>("dark");
    QTest::newRow("light") << false;
    QTest::newRow("dark") << true;
}

void TestPinWindowEvidence::screenshots()
{
    const QString out = qEnvironmentVariable("SNAPTRAY_PIN_DESIGN_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_PIN_DESIGN_DIR to capture pin screenshots");
    QFETCH(bool, dark);
    QString suffix = dark ? QStringLiteral("dark") : QStringLiteral("light");
    QTranslator translator;
    const QString qm = qEnvironmentVariable("SNAPTRAY_PIN_TRANSLATION");
    if (!qm.isEmpty()) {
        QVERIFY2(translator.load(qm), qPrintable(qm));
        QCoreApplication::installTranslator(&translator);
        suffix += QLatin1Char('-') + QFileInfo(qm).completeBaseName().section(QLatin1Char('_'), 1);
    }
    auto removeTranslator = qScopeGuard([&translator] { QCoreApplication::removeTranslator(&translator); });
    const auto original = ToolbarStyleConfig::loadStyle();
    auto restore = qScopeGuard([original] {
        ToolbarStyleConfig::saveStyle(original);
        DesignSystem::instance().refreshTheme();
    });
    setTheme(dark);
    QDir().mkpath(out);
    bool saved = true;
    const auto shot = [&](PinWindow& pin, const QString& name) {
        settle();
        saved = compose(pin).save(out + "/" + name + "-" + suffix + ".png") && saved;
    };

    PinWindow pin(demoImage(QSize(420, 260)), QPoint(240, 200), nullptr, false, false);
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));
    pin.setZoomLevel(1.5);
    pin.setOpacity(0.9);
    hover(pin);
    QVERIFY(pin.m_hoverBar->isBarVisible());
    shot(pin, "01-hover-bar");

    pin.m_hoverBarViewModel->openPanel(PinHoverBarViewModel::ZoomPanel);
    QTRY_VERIFY(pin.m_hoverBar->isPanelVisible());
    shot(pin, "02-zoom-panel");
    pin.m_hoverBarViewModel->openPanel(PinHoverBarViewModel::OpacityPanel);
    QTRY_VERIFY(pin.m_hoverBar->isPanelVisible());
    shot(pin, "03-opacity-panel");
    pin.m_hoverBarViewModel->closePanel();

    // A narrow pin keeps the two values and More.
    PinWindow narrow(demoImage(QSize(260, 170)), QPoint(240, 200), nullptr, false, false);
    narrow.show();
    QVERIFY(QTest::qWaitForWindowExposed(&narrow));
    hover(narrow);
    QVERIFY(narrow.m_hoverBar->isBarVisible());
    shot(narrow, "04-narrow");
    narrow.hide();   // pins delete themselves on close

    // The Live chip (status only; no capture engine runs here).
    pin.m_hoverBarViewModel->setStatus(PinHoverBarViewModel::Live);
    shot(pin, "05-live-chip");
    pin.m_hoverBarViewModel->setStatus(PinHoverBarViewModel::None);

    if (PlatformFeatures::instance().capabilities().supportsClickThrough) {
        pin.setClickThrough(true);
        settle();
        shot(pin, "06-click-through");
        pin.setClickThrough(false);
    }
    QVERIFY(saved);
}

void TestPinWindowEvidence::operationVideo()
{
    const QString out = qEnvironmentVariable("SNAPTRAY_PIN_VIDEO_DIR");
    if (out.isEmpty()) QSKIP("Set SNAPTRAY_PIN_VIDEO_DIR to record the operation video frames");
    const auto original = ToolbarStyleConfig::loadStyle();
    auto restore = qScopeGuard([original] {
        ToolbarStyleConfig::saveStyle(original);
        DesignSystem::instance().refreshTheme();
    });
    setTheme(false);
    QDir().mkpath(out);
    PinWindow pin(demoImage(QSize(420, 260)), QPoint(240, 200), nullptr, false, false);
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));
    int count = 0;
    QPoint cursor = pin.frameGeometry().topLeft() + QPoint(-40, 120);
    bool pressed = false;
    QString caption;
    // One area for the whole video: the pin at 150% (it grows right and down
    // from its top-left corner) and the panel above the bar.
    const QRect start = pin.frameGeometry();
    const QRect area(start.topLeft() - QPoint(kMargin, kMargin),
                     QSize(qRound(start.width() * 1.5) + 2 * kMargin, qRound(start.height() * 1.5) + 2 * kMargin));
    const QSize frameSize = area.size();
    const auto frame = [&](int hold = 1) {
        settle();
        const QImage image = compose(pin, cursor, pressed, area);
        QImage framed(frameSize.width(), frameSize.height() + kCaption, QImage::Format_RGB32);
        framed.fill(QColor("#101114"));
        QPainter p(&framed);
        p.drawImage(QPoint(0, 0), image);
        QFont font = p.font();
        font.setPixelSize(18);
        font.setBold(true);
        p.setFont(font);
        p.setPen(QColor("#f2f2f2"));
        p.drawText(QRect(16, frameSize.height(), frameSize.width() - 32, kCaption), Qt::AlignVCenter, caption);
        p.end();
        for (int i = 0; i < hold; ++i)
            framed.save(QStringLiteral("%1/frame-%2.png").arg(out).arg(count++, 5, 10, QLatin1Char('0')));
    };
    const auto moveTo = [&](const QPoint& to, int steps = 8) {
        const QPoint from = cursor;
        for (int i = 1; i <= steps; ++i) {
            cursor = from + (to - from) * i / steps;
            frame();
        }
    };
    const auto click = [&] {
        pressed = true;
        frame(2);
        pressed = false;
    };
    const auto centerOfButton = [&](const char* name) {
        QVariant rect;
        QMetaObject::invokeMethod(static_cast<QQuickWindow*>(pin.m_hoverBar->barWindow())->contentItem()->childItems().first(),
                                  "buttonRect", Q_RETURN_ARG(QVariant, rect), Q_ARG(QVariant, QString::fromLatin1(name)));
        return pin.m_hoverBar->barGeometry().topLeft() + rect.toRectF().center().toPoint();
    };

    caption = QStringLiteral("#225 · A pin is just the image");
    frame(12);
    caption = QStringLiteral("Rest the pointer on it: the controls appear");
    moveTo(pin.frameGeometry().center());
    hover(pin);
    frame(14);
    caption = QStringLiteral("Click the zoom value: slider, presets, reset");
    moveTo(centerOfButton("pinHoverZoom"));
    click();
    pin.m_hoverBarViewModel->openPanel(PinHoverBarViewModel::ZoomPanel);
    frame(10);
    for (int percent : {110, 125, 140, 150}) {
        pin.m_hoverBarViewModel->requestZoomPercent(percent);
        frame(2);
    }
    frame(8);
    caption = QStringLiteral("Reset to 100%");
    pin.m_hoverBarViewModel->requestZoomPercent(100);
    frame(12);
    pin.m_hoverBarViewModel->closePanel();
    caption = QStringLiteral("Opacity the same way");
    moveTo(centerOfButton("pinHoverOpacity"));
    click();
    pin.m_hoverBarViewModel->openPanel(PinHoverBarViewModel::OpacityPanel);
    frame(6);
    for (int percent : {90, 80, 70}) {
        pin.m_hoverBarViewModel->requestOpacityPercent(percent);
        frame(3);
    }
    frame(8);
    pin.m_hoverBarViewModel->closePanel();
    caption = QStringLiteral("Move away: the controls fade, the image stays");
    moveTo(pin.frameGeometry().topLeft() + QPoint(-40, 120));
    pin.m_hoverBar->hideNow();
    frame(16);
    QVERIFY(count > 60);
}

void TestPinWindowEvidence::nativeControlsAndZOrder()
{
#ifndef Q_OS_WIN
    QSKIP("Windows native window policy check");
#else
    const QString out = qEnvironmentVariable("SNAPTRAY_PIN_NATIVE_DIR");
    const QString helper = qEnvironmentVariable("SNAPTRAY_FOCUS_HELPER");
    if (out.isEmpty() || helper.isEmpty()) QSKIP("Set pin native evidence directory and focus helper");
    QVERIFY(QFileInfo::exists(helper));
    QVERIFY(QDir().mkpath(out));
    PinWindow pin(demoImage(QSize(420, 260)), QPoint(240, 200), nullptr, false, false);
    pin.show();
    QVERIFY(QTest::qWaitForWindowExposed(&pin));
    pin.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&pin));
    hover(pin);
    auto* bar = qobject_cast<QQuickWindow*>(pin.m_hoverBar->barWindow());
    QVERIFY(bar);
    const auto openPanel = [&](const QString& button, int expectedPanel) {
        hover(pin);
        QVariant rect;
        auto* root = bar->contentItem()->childItems().first();
        QVERIFY(QMetaObject::invokeMethod(root, "buttonRect", Q_RETURN_ARG(QVariant, rect),
                                         Q_ARG(QVariant, button)));
        QTest::mouseClick(bar, Qt::LeftButton, Qt::NoModifier, rect.toRectF().center().toPoint());
        QTRY_VERIFY(pin.m_hoverBar->isPanelVisible());
        QCOMPARE(pin.m_hoverBarViewModel->panel(), expectedPanel);
    };
    const auto openZoom = [&] { openPanel(QStringLiteral("pinHoverZoom"), PinHoverBarViewModel::ZoomPanel); };
    openZoom();
    auto* panel = qobject_cast<QQuickWindow*>(pin.m_hoverBar->panelWindow());
    QVERIFY(panel);
    QVERIFY(QTest::qWaitForWindowExposed(panel));
    auto* slider = panel->contentItem()->childItems().first()->findChild<QQuickItem*>("pinValueSlider");
    QVERIFY(slider);
    auto* handle = slider->property("handle").value<QQuickItem*>();
    QVERIFY(handle);
    const QPoint start = handle->mapToScene(QPointF(handle->width()/2, handle->height()/2)).toPoint();
    const QPoint panelPosition = panel->position();
    const qreal zoom = pin.zoomLevel();
    QTest::mousePress(panel, Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(panel, start + QPoint(12,0));
    QTRY_VERIFY(!qFuzzyCompare(pin.zoomLevel(), zoom));
    QCOMPARE(panel->position(), panelPosition);
    QTest::mouseRelease(panel, Qt::LeftButton, Qt::NoModifier, start + QPoint(12,0));
    settle();
    QVERIFY(pin.m_hoverBar->isPanelVisible());
    std::function<QQuickItem*(QQuickItem*, const QString&)> findVisual = [&](QQuickItem* root, const QString& name) -> QQuickItem* {
        if (root->objectName() == name) return root;
        for (auto* child : root->childItems()) if (auto* found = findVisual(child, name)) return found;
        return nullptr;
    };
    const auto clickPanelButton = [&](const QString& name) {
        QQuickItem* button = nullptr;
        QTRY_VERIFY((button = findVisual(panel->contentItem(), name)) && button->isVisible() && button->width() > 0);
        QTest::mouseClick(panel, Qt::LeftButton, Qt::NoModifier,
                          button->mapToScene(QPointF(button->width()/2, button->height()/2)).toPoint());
    };
    for (const int percent : {33, 50, 100, 200}) {
        QTest::keyClick(&pin, Qt::Key_0, Qt::ControlModifier);
        openZoom();
        clickPanelButton(QStringLiteral("pinValuePreset_%1").arg(percent));
        QTRY_COMPARE(qRound(pin.zoomLevel()*100), percent);
    }
    QTest::keyClick(&pin, Qt::Key_0, Qt::ControlModifier);
    openPanel(QStringLiteral("pinHoverOpacity"), PinHoverBarViewModel::OpacityPanel);
    for (const int percent : {50, 75, 100}) {
        clickPanelButton(QStringLiteral("pinValuePreset_%1").arg(percent));
        QTRY_COMPARE(qRound(pin.opacity()*100), percent);
    }
    slider = findVisual(panel->contentItem(), QStringLiteral("pinValueSlider"));
    QVERIFY(slider);
    handle = slider->property("handle").value<QQuickItem*>();
    QVERIFY(handle);
    const QPoint opacityStart = handle->mapToScene(QPointF(handle->width()/2, handle->height()/2)).toPoint();
    const QPoint opacityMinimum = slider->mapToScene(QPointF(slider->property("leftPadding").toReal() + handle->width()/2,
                                                             slider->height()/2)).toPoint();
    QTest::mousePress(panel, Qt::LeftButton, Qt::NoModifier, opacityStart);
    QTest::mouseMove(panel, opacityMinimum);
    QTRY_COMPARE(qRound(pin.opacity()*100), 10);
    QTest::mouseRelease(panel, Qt::LeftButton, Qt::NoModifier, opacityMinimum);
    clickPanelButton(QStringLiteral("pinValueReset"));
    QTRY_COMPARE(pin.opacity(), 1.0);
    QTest::keyClick(&pin, Qt::Key_Escape);
    openZoom();
    const int rotation = pin.m_rotationAngle;
    bool sawResize = false;
    bool panelVisibleAtResize = false;
    class ResizeObserver final : public QObject {
    public:
        std::function<void()> resized;
        bool eventFilter(QObject*, QEvent* event) override {
            if (event->type() == QEvent::Resize) resized();
            return false;
        }
    } observer;
    observer.resized = [&] { sawResize = true; panelVisibleAtResize |= pin.m_hoverBar->isPanelVisible(); };
    pin.installEventFilter(&observer);
    QTest::keyClick(&pin, Qt::Key_1);
    QTRY_VERIFY(!pin.m_hoverBar->isPanelVisible());
    QTRY_VERIFY(sawResize);
    QVERIFY(!panelVisibleAtResize);
    QCOMPARE(pin.m_rotationAngle, (rotation + 90) % 360);
    pin.removeEventFilter(&observer);
    QTest::keyClick(&pin, Qt::Key_0, Qt::ControlModifier);
    QCOMPARE(pin.zoomLevel(), 1.0);
    QTest::keyClick(&pin, Qt::Key_Equal, Qt::ControlModifier);
    QVERIFY(pin.zoomLevel() > 1.0);
    QTest::keyClick(&pin, Qt::Key_0, Qt::ControlModifier);
    QCOMPARE(pin.zoomLevel(), 1.0);
    QTest::keyClick(&pin, Qt::Key_BracketLeft);
    QVERIFY(pin.opacity() < 1.0);
    QTest::keyClick(&pin, Qt::Key_BracketRight, Qt::ControlModifier);
    QCOMPARE(pin.opacity(), 1.0);
    openZoom();
    settle();
    const auto above = [](HWND upper, HWND lower) {
        for (HWND window=GetTopWindow(nullptr); window; window=GetWindow(window,GW_HWNDNEXT)) {
            if (window==upper) return true;
            if (window==lower) return false;
        }
        return false;
    };
    const auto visibleAndAbove = [&] {
        const HWND pinHandle = reinterpret_cast<HWND>(pin.winId());
        const HWND barHandle = reinterpret_cast<HWND>(bar->winId());
        const HWND panelHandle = reinterpret_cast<HWND>(panel->winId());
        return IsWindowVisible(pinHandle) && IsWindowVisible(barHandle) && IsWindowVisible(panelHandle)
            && bar->contentItem()->childItems().first()->isVisible()
            && panel->contentItem()->childItems().first()->isVisible()
            && bar->opacity() > 0.5 && panel->opacity() > 0.5
            && above(barHandle,pinHandle) && above(panelHandle,pinHandle);
    };
    QVERIFY(visibleAndAbove());
    QProcess other;
    other.start(helper);
    QVERIFY(other.waitForStarted());
    const auto foregroundPid = [] { DWORD pid=0; GetWindowThreadProcessId(GetForegroundWindow(),&pid); return pid; };
    QTRY_COMPARE(foregroundPid(), DWORD(other.processId()));
    QTest::qWait(250);
    QVERIFY(!pin.isActiveWindow());
    QVERIFY(visibleAndAbove());
    QVERIFY(compose(pin).save(out + "/foreign-app-active.png"));
    qInfo() << "Native pin check: slider stayed fixed; shortcuts passed; bar and panel visible above pin with another process active";
    QVERIFY(other.waitForFinished(5000));
    pin.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&pin));
    QTest::keyClick(&pin, Qt::Key_Escape);
    QTRY_VERIFY(!pin.m_hoverBar->isPanelVisible());
#endif
}

QTEST_MAIN(TestPinWindowEvidence)
#include "tst_PinWindowEvidence.moc"
