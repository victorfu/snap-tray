#pragma once

#include <QRect>
#include <QString>
#include <xcb/xcb.h>
#include <xcb/randr.h>
#include <cstdlib>

namespace SnapTray {

// Use the same native RandR output bounds for capture and window timelines.
inline QRect x11ScreenGeometry(xcb_connection_t* connection, const xcb_screen_t* screen,
                               const QString& name, const QSize& expectedPixelSize)
{
    QRect native;
    auto* resources = xcb_randr_get_screen_resources_current_reply(connection,
        xcb_randr_get_screen_resources_current(connection, screen->root), nullptr);
    if (resources) {
        const auto* outputs = xcb_randr_get_screen_resources_current_outputs(resources);
        for (int i = 0; i < xcb_randr_get_screen_resources_current_outputs_length(resources); ++i) {
            auto* output = xcb_randr_get_output_info_reply(connection,
                xcb_randr_get_output_info(connection, outputs[i], resources->config_timestamp), nullptr);
            if (output && output->crtc && QString::fromUtf8(
                    reinterpret_cast<char*>(xcb_randr_get_output_info_name(output)),
                    xcb_randr_get_output_info_name_length(output)) == name) {
                auto* crtc = xcb_randr_get_crtc_info_reply(connection,
                    xcb_randr_get_crtc_info(connection, output->crtc, resources->config_timestamp), nullptr);
                if (crtc) { native = QRect(crtc->x, crtc->y, crtc->width, crtc->height); free(crtc); }
            }
            free(output);
        }
        free(resources);
    }
    // Only fall back when the selected output covers the entire root (e.g. Xvfb).
    if (native.isEmpty() && expectedPixelSize == QSize(screen->width_in_pixels, screen->height_in_pixels)) {
        native = QRect(QPoint(0, 0), expectedPixelSize);
    }
    return native;
}

} // namespace SnapTray
