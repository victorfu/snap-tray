#pragma once

#include "recording/WindowTimeline.h"

#include <QString>

#include <optional>

// The window timeline travels next to the temporary recording as
// "<video>.windows.json" and lives exactly as long as that file: written when
// encoding finishes, removed when the recording is saved, discarded or
// replaced by an export. It never reaches a final output or History.
namespace SnapTray::WindowTimelineSidecar {

constexpr auto kSuffix = ".windows.json";

QString pathFor(const QString& videoPath);
// Atomic: a crash mid-write leaves no half file. False (with qWarning) on failure.
bool write(const QString& videoPath, const WindowTimeline& timeline);
// nullopt when there is no sidecar or it cannot be parsed.
std::optional<WindowTimeline> read(const QString& videoPath);
// No-op when there is no sidecar.
void remove(const QString& videoPath);

} // namespace SnapTray::WindowTimelineSidecar
