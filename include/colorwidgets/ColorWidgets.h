#ifndef SNAPTRAY_COLOR_WIDGETS_H
#define SNAPTRAY_COLOR_WIDGETS_H

/**
 * @file ColorWidgets.h
 * @brief Main header file for SnapTray Color Widgets library
 *
 * This header includes all color widget components for convenient access.
 * You can either include this file to get all components, or include
 * individual headers for specific components.
 *
 * Components included:
 * - ColorUtils: Color utility functions (conversion, parsing, calculations)
 * - ColorWheel: HSV/HSL/LCH color wheel with triangle or square selector
 * - GradientSlider: Gradient-background slider for any color channel
 * - HueSlider: Specialized hue selection slider
 * - ColorPreview: Color preview widget with comparison mode
 * - ColorLineEdit: Hex color input with preview
 * - ColorDialog: Complete color picker dialog
 * - StyledColorDialog: SnapTray-themed color dialog
 * - ColorPickerDialogCompat: Drop-in replacement for legacy ColorPickerDialog
 *
 * Example usage:
 * @code
 * #include "colorwidgets/ColorWidgets.h"
 *
 * using namespace snaptray::colorwidgets;
 *
 * // Use the styled dialog
 * QColor color = StyledColorDialog::getColor(Qt::red, this, "Pick a color");
 *
 * // Or create individual components
 * ColorWheel* wheel = new ColorWheel(this);
 * wheel->setWheelShape(ColorWheel::ShapeSquare);
 * wheel->setColor(Qt::blue);
 * @endcode
 */

#include "colorwidgets/ColorDialog.h"
#include "colorwidgets/ColorLineEdit.h"
#include "colorwidgets/ColorPickerDialogCompat.h"
#include "colorwidgets/ColorPreview.h"
#include "colorwidgets/ColorUtils.h"
#include "colorwidgets/ColorWheel.h"
#include "colorwidgets/GradientSlider.h"
#include "colorwidgets/HueSlider.h"
#include "colorwidgets/StyledColorDialog.h"

#endif  // SNAPTRAY_COLOR_WIDGETS_H
