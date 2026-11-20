#pragma once

#include "beautify/BeautifySettings.h"
#include "pinwindow/RegionLayoutManager.h"

#include <optional>

// Metadata retained with the exported pixels, even after the selector closes
// or a history entry is pruned.
struct ScreenshotOrigin
{
    QString id;
    QString windowTitle;
    QString ownerApp;
    int regionIndex = -1;
    QRect globalRect;
    std::optional<BeautifySettings> beautify;
    QVector<LayoutRegion> layout;
};
