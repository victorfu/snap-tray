#pragma once

#include "beautify/BeautifySettings.h"

#include <QJsonObject>
#include <optional>

namespace SnapTray {

// The exact pixel settings one picture was beautified with (#344), as kept in
// its history entry. Unlike FrameStyle, nothing is a fraction or a style name:
// the values are the ones BeautifyRenderer used, so they do not change when the
// global defaults or the saved styles do.
QJsonObject beautifySettingsToJson(const BeautifySettings& settings);

// Every key written by beautifySettingsToJson() must be present, of the right
// type and within a sane range; anything else is damaged data and gives
// nullopt rather than a guessed look.
std::optional<BeautifySettings> beautifySettingsFromJson(const QJsonObject& json);

} // namespace SnapTray
