#pragma once

class AnnotationItem;

// Geometry and content of interactively editable annotations (#237): what a
// move, rotate, scale, endpoint/vertex drag, or text re-edit changes. Style
// (AnnotationStyle) and visibility are never included.
namespace AnnotationGeometry {
bool supports(const AnnotationItem& item);
// Copies `from`'s geometry onto `to`. False, with `to` unchanged, when the
// kinds differ or the kind has no editable geometry.
bool copy(const AnnotationItem& from, AnnotationItem& to);
bool equal(const AnnotationItem& a, const AnnotationItem& b);
} // namespace AnnotationGeometry
