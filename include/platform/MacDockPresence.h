#pragma once

namespace SnapTray {

// Keeps the LSUIElement app in the Dock (regular activation policy) while alive.
// The app returns to accessory mode once the last claim is destroyed, so Settings
// and an update session can overlap. Use on the GUI thread.
class DockPresenceClaim
{
public:
    DockPresenceClaim();
    ~DockPresenceClaim();

    DockPresenceClaim(const DockPresenceClaim&) = delete;
    DockPresenceClaim& operator=(const DockPresenceClaim&) = delete;
};

} // namespace SnapTray
