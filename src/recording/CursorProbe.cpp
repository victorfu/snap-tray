#include "recording/CursorProbe.h"

#include <QCursor>
#include <QGuiApplication>

namespace SnapTray {

#ifdef SNAPTRAY_HAS_X11_CURSOR_PROBE
std::unique_ptr<CursorProbe> createX11CursorProbe(); // X11CursorProbe_linux.cpp
#endif

namespace {

// Position only: QCursor::pos() works everywhere Qt does, but says nothing
// about the shape or about clicks in other applications.
class PositionOnlyCursorProbe : public CursorProbe
{
public:
    Capabilities capabilities() const override { return {}; }
    std::optional<State> poll() override { return State{QCursor::pos(), false, 0}; }
    QString name() const override { return QStringLiteral("position-only"); }
};

} // namespace

std::unique_ptr<CursorProbe> createCursorProbe()
{
#ifdef SNAPTRAY_HAS_X11_CURSOR_PROBE
    if (QGuiApplication::platformName() == QLatin1String("xcb")) {
        if (auto probe = createX11CursorProbe()) return probe;
    }
#endif
#ifdef Q_OS_WIN
    return createWindowsCursorProbe();
#else
    return std::make_unique<PositionOnlyCursorProbe>();
#endif
}

} // namespace SnapTray
