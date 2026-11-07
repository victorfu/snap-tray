#pragma once

// Small helpers around AXUIElement attributes shared by window detection and
// scroll area detection. macOS only.

#include <ApplicationServices/ApplicationServices.h>
#include <QString>

#include <optional>

namespace snaptray::platform::mac {

// Position and size of an element in global top-left screen coordinates.
bool getAXElementFrame(AXUIElementRef element, CGRect& outFrame);
// The element's AXRole, or an empty string.
QString getAXElementRole(AXUIElementRef element);
// A boolean attribute that may be stored as CFBoolean or CFNumber.
std::optional<bool> copyAXBooleanAttribute(AXUIElementRef element, CFStringRef attribute);
// A numeric attribute (CFNumber), such as a scroll bar's AXValue.
std::optional<double> copyAXNumberAttribute(AXUIElementRef element, CFStringRef attribute);
// An element-valued attribute, retained; the caller releases it.
AXUIElementRef copyAXElementAttribute(AXUIElementRef element, CFStringRef attribute);

} // namespace snaptray::platform::mac
