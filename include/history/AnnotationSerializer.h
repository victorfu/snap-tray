#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <memory>
#include <optional>
#include <vector>

class AnnotationItem;
class AnnotationLayer;
class QPixmap;

using SharedPixmap = std::shared_ptr<const QPixmap>;

namespace SnapTray {

QByteArray serializeAnnotationLayer(const AnnotationLayer& layer);

// Decodes a stored annotations document without touching any layer, so a
// caller can check it before giving up what is on screen. Fails for anything
// but a complete document: no bytes, invalid JSON, a root that is not an
// object, "items" missing or not an array, or an item that is not a known
// annotation. A document with no annotations is {"items":[]}.
std::optional<std::vector<std::unique_ptr<AnnotationItem>>> decodeAnnotationItems(
    const QByteArray& data,
    SharedPixmap sourcePixmap,
    QString* errorMessage = nullptr);

// Replaces the layer's items with the decoded document. On failure the layer
// is left empty; use decodeAnnotationItems() when it has to survive.
bool deserializeAnnotationLayer(const QByteArray& data,
                                AnnotationLayer* layer,
                                SharedPixmap sourcePixmap,
                                QString* errorMessage = nullptr);

} // namespace SnapTray
