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
    QLinearGradient backdrop(0, 0, area.width(), area.height());
    const bool dark = ToolbarStyleConfig::loadStyle() == ToolbarStyleType::Dark;
    backdrop.setColorAt(0, dark ? QColor("#1f2433") : QColor("#9fb7d6"));
    backdrop.setColorAt(1, dark ? QColor("#38283f") : QColor("#d8c3d9"));
    p.fillRect(out.rect(), backdrop);
    const auto draw = [&](const QImage& image, const QPoint& globalTopLeft) {
        QImage copy = image;
        copy.setDevicePixelRatio(1.0);
        const QSize logical = image.size() / image.devicePixelRatio();
        p.drawImage(QRect(globalTopLeft - area.topLeft(), logical), copy);
    };
    p.setOpacity(pin.opacity());
    draw(pin.grab().toImage(), pinRect.topLeft());
    p.setOpacity(1.0);
    if (auto* pill = pin.findChild<ClickThroughExitButton*>(); pill && pill->isVisible())
        draw(pill->grab().toImage(), pill->geometry().topLeft());
    if (pin.m_hoverBar && pin.m_hoverBar->isBarVisible())
        draw(static_cast<QQuickWindow*>(pin.m_hoverBar->barWindow())->grabWindow(),
             pin.m_hoverBar->barGeometry().topLeft());
    if (pin.m_hoverBar && pin.m_hoverBar->isPanelVisible())
        draw(static_cast<QQuickWindow*>(pin.m_hoverBar->panelWindow())->grabWindow(),
             pin.m_hoverBar->panelWindow()->geometry().topLeft());
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

QTEST_MAIN(TestPinWindowEvidence)
#include "tst_PinWindowEvidence.moc"
