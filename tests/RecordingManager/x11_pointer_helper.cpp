// Test-only: moves, clicks and reshapes the pointer on the (isolated) X display.
#include <X11/Xlib.h>
#include <X11/cursorfont.h>
#include <X11/extensions/XTest.h>

#include <cstring>

void snaptrayTestX11Pointer(const char* action, int a, int b)
{
    static Display* display = XOpenDisplay(nullptr);
    if (!display) return;
    const Window root = DefaultRootWindow(display);
    if (std::strcmp(action, "move") == 0) {
        XTestFakeMotionEvent(display, -1, a, b, CurrentTime);
    } else if (std::strcmp(action, "button") == 0) {
        XTestFakeButtonEvent(display, static_cast<unsigned int>(a), b ? True : False, CurrentTime);
    } else if (std::strcmp(action, "shape") == 0) {
        XDefineCursor(display, root, XCreateFontCursor(display, static_cast<unsigned int>(a)));
    } else if (std::strcmp(action, "swap") == 0) {
        // The map must keep the server's length; swap only buttons 1 and 3.
        unsigned char map[256];
        const int count = XGetPointerMapping(display, map, 256);
        for (int i = 0; i < count; ++i) map[i] = static_cast<unsigned char>(i + 1);
        if (a && count >= 3) { map[0] = 3; map[2] = 1; }
        while (XSetPointerMapping(display, map, count) == MappingBusy) {}
    }
    XSync(display, False);
}
