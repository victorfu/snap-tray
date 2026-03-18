#pragma once

#include <QByteArray>
#include <QString>
#include <QVector>
#include <memory>

class AnnotationLayer;
class QPixmap;

using SharedPixmap = std::shared_ptr<const QPixmap>;

namespace SnapTray {

QByteArray serializeAnnotationLayer(const AnnotationLayer& layer);
bool deserializeAnnotationLayer(const QByteArray& data,
                                AnnotationLayer* layer,
                                SharedPixmap sourcePixmap,
                                QString* errorMessage = nullptr);

} // namespace SnapTray
