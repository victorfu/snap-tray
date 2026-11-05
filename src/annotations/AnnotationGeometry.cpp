#include "annotations/AnnotationGeometry.h"

#include <array>

#include "annotations/ArrowAnnotation.h"
#include "annotations/EmojiStickerAnnotation.h"
#include "annotations/PolylineAnnotation.h"
#include "annotations/ShapeAnnotation.h"
#include "annotations/TextBoxAnnotation.h"

namespace {

struct Ops {
    bool (*matches)(const AnnotationItem&);
    void (*copy)(const AnnotationItem&, AnnotationItem&);
    bool (*equal)(const AnnotationItem&, const AnnotationItem&);
};

template <typename T> bool is(const AnnotationItem& item) { return dynamic_cast<const T*>(&item) != nullptr; }
template <typename T> const T& as(const AnnotationItem& item) { return static_cast<const T&>(item); }
template <typename T> T& as(AnnotationItem& item) { return static_cast<T&>(item); }

const std::array<Ops, 5> kOps = {{
    {&is<ShapeAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         const auto& a = as<ShapeAnnotation>(f);
         auto& b = as<ShapeAnnotation>(t);
         b.setRectF(a.rectF());
         b.setRotation(a.rotation());
         b.setScale(a.scaleX(), a.scaleY());
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         const auto& a = as<ShapeAnnotation>(x);
         const auto& b = as<ShapeAnnotation>(y);
         return a.rectF() == b.rectF() && a.rotation() == b.rotation()
             && a.scaleX() == b.scaleX() && a.scaleY() == b.scaleY();
     }},
    {&is<ArrowAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         const auto& a = as<ArrowAnnotation>(f);
         auto& b = as<ArrowAnnotation>(t);
         b.setStart(a.start());
         b.setEnd(a.end());
         b.setControlPoint(a.controlPoint());   // last: setStart/setEnd shift it
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         const auto& a = as<ArrowAnnotation>(x);
         const auto& b = as<ArrowAnnotation>(y);
         return a.start() == b.start() && a.end() == b.end() && a.controlPoint() == b.controlPoint();
     }},
    {&is<PolylineAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         as<PolylineAnnotation>(t).setPoints(as<PolylineAnnotation>(f).points());
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         return as<PolylineAnnotation>(x).points() == as<PolylineAnnotation>(y).points();
     }},
    {&is<TextBoxAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         const auto& a = as<TextBoxAnnotation>(f);
         auto& b = as<TextBoxAnnotation>(t);
         b.setRotation(a.rotation());
         b.setScale(a.scale());
         b.setMirror(a.mirrorX(), a.mirrorY());
         b.setPosition(a.position());
         b.restoreLayout(a.text(), a.box(), a.wrapWidth());   // last: no recalculation after it
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         const auto& a = as<TextBoxAnnotation>(x);
         const auto& b = as<TextBoxAnnotation>(y);
         return a.text() == b.text() && a.position() == b.position() && a.box() == b.box()
             && a.wrapWidth() == b.wrapWidth() && a.rotation() == b.rotation()
             && a.scale() == b.scale() && a.mirrorX() == b.mirrorX() && a.mirrorY() == b.mirrorY();
     }},
    {&is<EmojiStickerAnnotation>,
     [](const AnnotationItem& f, AnnotationItem& t) {
         const auto& a = as<EmojiStickerAnnotation>(f);
         auto& b = as<EmojiStickerAnnotation>(t);
         b.setPosition(a.position());
         b.setScale(a.scale());
         b.setRotation(a.rotation());
         b.setMirror(a.mirrorX(), a.mirrorY());
     },
     [](const AnnotationItem& x, const AnnotationItem& y) {
         const auto& a = as<EmojiStickerAnnotation>(x);
         const auto& b = as<EmojiStickerAnnotation>(y);
         return a.position() == b.position() && a.scale() == b.scale() && a.rotation() == b.rotation()
             && a.mirrorX() == b.mirrorX() && a.mirrorY() == b.mirrorY();
     }},
}};

const Ops* opsFor(const AnnotationItem& item)
{
    for (const Ops& ops : kOps) {
        if (ops.matches(item)) return &ops;
    }
    return nullptr;
}

} // namespace

namespace AnnotationGeometry {

bool supports(const AnnotationItem& item) { return opsFor(item) != nullptr; }

bool copy(const AnnotationItem& from, AnnotationItem& to)
{
    const Ops* ops = opsFor(from);
    if (!ops || opsFor(to) != ops) return false;
    ops->copy(from, to);
    return true;
}

bool equal(const AnnotationItem& a, const AnnotationItem& b)
{
    const Ops* ops = opsFor(a);
    return ops && opsFor(b) == ops && ops->equal(a, b);
}

} // namespace AnnotationGeometry
