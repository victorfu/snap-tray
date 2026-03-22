#ifndef FUNCTIONALANNOTATIONSURFACEADAPTER_H
#define FUNCTIONALANNOTATIONSURFACEADAPTER_H

#include "annotation/AnnotationSurfaceAdapter.h"

#include <functional>

class FunctionalAnnotationSurfaceAdapter : public AnnotationSurfaceAdapter
{
public:
    std::function<void()> requestFullUpdate;
    std::function<void(const QRect&)> requestRectUpdate;
    std::function<bool()> supportsRectUpdate;
    std::function<QRect(const QRect&)> mapRectToHost;
    std::function<QRect()> viewportProvider;

    void requestFullAnnotationUpdate() override
    {
        if (requestFullUpdate) {
            requestFullUpdate();
        }
    }

    void requestAnnotationUpdate(const QRect& annotationRect) override
    {
        if (requestRectUpdate) {
            requestRectUpdate(annotationRect);
            return;
        }

        requestFullAnnotationUpdate();
    }

    bool supportsAnnotationRectUpdate() const override
    {
        return supportsRectUpdate ? supportsRectUpdate() : false;
    }

    QRect mapAnnotationRectToHostUpdate(const QRect& annotationRect) const override
    {
        return mapRectToHost ? mapRectToHost(annotationRect) : QRect();
    }

    QRect annotationViewport() const override
    {
        return viewportProvider ? viewportProvider() : QRect();
    }
};

#endif // FUNCTIONALANNOTATIONSURFACEADAPTER_H
