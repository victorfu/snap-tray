#ifndef REMOVEDANNOTATIONITEM_H
#define REMOVEDANNOTATIONITEM_H

#include "AnnotationItem.h"

#include <cstddef>
#include <cstdint>
#include <memory>

// Ownership transport used while an eraser gesture or Remove history command
// holds an annotation outside the visible scene.
struct RemovedAnnotationItem
{
    std::size_t originalIndex = 0;
    std::unique_ptr<AnnotationItem> item;
    std::uint64_t id = 0;
};

#endif // REMOVEDANNOTATIONITEM_H
