#include <QtTest/QtTest>

#include <QCursor>
#include <QGuiApplication>
#include <QScreen>

#include "WindowDetector.h"

namespace {

QRect clampRectToScreen(const QRect& screenRect, const QPoint& center, const QSize& size)
{
    if (!screenRect.isValid() || size.isEmpty()) {
        return {};
    }

    const int maxX = screenRect.right() - size.width() + 1;
    const int maxY = screenRect.bottom() - size.height() + 1;
    const int x = qBound(screenRect.left(), center.x() - size.width() / 2, maxX);
    const int y = qBound(screenRect.top(), center.y() - size.height() / 2, maxY);
    return QRect(QPoint(x, y), size);
}

DetectedElement makeElement(const QRect& bounds, int layer = 0)
{
    DetectedElement element;
    element.bounds = bounds;
    element.windowTitle = QStringLiteral("test");
    element.ownerApp = QStringLiteral("SnapTrayTests");
    element.windowLayer = layer;
    element.windowId = static_cast<uint32_t>(layer + 1);
    element.elementType = ElementType::Window;
    element.ownerPid = 4242;
    return element;
}

class TestWindowDetector final : public WindowDetector
{
public:
    using WindowDetector::WindowDetector;

    mutable int childQueryCount = 0;
    std::optional<DetectedElement> childResult;

protected:
    std::optional<DetectedElement> detectChildElementAt(
        const QPoint& screenPos,
        const DetectedElement& topWindow,
        size_t topLevelIndex) const override
    {
        Q_UNUSED(screenPos);
        Q_UNUSED(topWindow);
        Q_UNUSED(topLevelIndex);
        ++childQueryCount;
        return childResult;
    }
};

} // namespace

class tst_WindowDetectorQueryMode : public QObject
{
    Q_OBJECT

private slots:
    void testTopLevelOnlySkipsChildQuery();
    void testIncludeChildControlsUsesChildQuery();
};

void tst_WindowDetectorQueryMode::testTopLevelOnlySkipsChildQuery()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        QSKIP("No screen available for WindowDetector query mode test.");
    }

    const QRect topBounds = clampRectToScreen(screen->geometry(), screen->geometry().center(), QSize(220, 160));
    const QPoint hitPoint = topBounds.center();

    TestWindowDetector detector;
    detector.m_enabled = true;
    detector.setScreen(screen);
    detector.m_refreshComplete = true;
    detector.m_windowCache = {makeElement(topBounds)};
    detector.childResult = makeElement(QRect(hitPoint.x() - 20, hitPoint.y() - 15, 40, 30), 1);

    const auto result = detector.detectWindowAt(hitPoint, WindowDetector::QueryMode::TopLevelOnly);

    QVERIFY(result.has_value());
    QCOMPARE(result->bounds, topBounds);
    QCOMPARE(detector.childQueryCount, 0);
}

void tst_WindowDetectorQueryMode::testIncludeChildControlsUsesChildQuery()
{
    QScreen* screen = QGuiApplication::screenAt(QCursor::pos());
    if (!screen) {
        screen = QGuiApplication::primaryScreen();
    }
    if (!screen) {
        QSKIP("No screen available for WindowDetector query mode test.");
    }

    const QRect topBounds = clampRectToScreen(screen->geometry(), screen->geometry().center(), QSize(220, 160));
    const QPoint hitPoint = topBounds.center();
    const QRect childBounds = QRect(hitPoint.x() - 24, hitPoint.y() - 18, 48, 36);

    TestWindowDetector detector;
    detector.m_enabled = true;
    detector.setScreen(screen);
    detector.m_refreshComplete = true;
    detector.m_windowCache = {makeElement(topBounds)};
    detector.childResult = makeElement(childBounds, 1);

    const auto result = detector.detectWindowAt(hitPoint, WindowDetector::QueryMode::IncludeChildControls);

    QVERIFY(result.has_value());
    QCOMPARE(result->bounds, childBounds);
    QCOMPARE(detector.childQueryCount, 1);
}

QTEST_MAIN(tst_WindowDetectorQueryMode)
#include "tst_WindowDetectorQueryMode.moc"
