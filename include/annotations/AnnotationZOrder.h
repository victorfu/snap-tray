#pragma once

// Drawing-order moves for the selected annotation (#268). Kept apart from
// AnnotationLayer so the shortcut and label tables in snaptray_core and the
// style strip view model can use it.
enum class AnnotationZOrderMove { Forward, Backward, ToFront, ToBack };
