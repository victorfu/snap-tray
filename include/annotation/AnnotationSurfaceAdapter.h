#ifndef ANNOTATIONSURFACEADAPTER_H
#define ANNOTATIONSURFACEADAPTER_H

#include <QRect>

class AnnotationSurfaceAdapter
{
public:
    virtual ~AnnotationSurfaceAdapter() = default;

    virtual void requestFullAnnotationUpdate() = 0;
    virtual void requestAnnotationUpdate(const QRect& annotationRect) = 0;
    virtual bool supportsAnnotationRectUpdate() const = 0;
    virtual QRect mapAnnotationRectToHostUpdate(const QRect& annotationRect) const = 0;
    virtual QRect annotationViewport() const = 0;
};

#endif // ANNOTATIONSURFACEADAPTER_H
