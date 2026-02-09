#ifndef ANNOTATIONHOSTADAPTER_H
#define ANNOTATIONHOSTADAPTER_H

#include <QColor>
#include <QPoint>
#include <QString>

#include "annotations/ArrowAnnotation.h"
#include "annotations/LineStyle.h"

class QWidget;
class AnnotationLayer;
class ToolOptionsPanel;
class InlineTextEditor;
class TextAnnotationEditor;

class AnnotationHostAdapter
{
public:
    virtual ~AnnotationHostAdapter() = default;

    virtual QWidget* annotationHostWidget() const = 0;
    virtual AnnotationLayer* annotationLayerForContext() const = 0;
    virtual ToolOptionsPanel* toolOptionsPanelForContext() const = 0;
    virtual InlineTextEditor* inlineTextEditorForContext() const = 0;
    virtual TextAnnotationEditor* textAnnotationEditorForContext() const = 0;

    virtual void onContextColorSelected(const QColor& color) = 0;
    virtual void onContextMoreColorsRequested() = 0;
    virtual void onContextLineWidthChanged(int width) = 0;
    virtual void onContextArrowStyleChanged(LineEndStyle style) = 0;
    virtual void onContextLineStyleChanged(LineStyle style) = 0;
    virtual void onContextFontSizeDropdownRequested(const QPoint& pos) = 0;
    virtual void onContextFontFamilyDropdownRequested(const QPoint& pos) = 0;
    virtual void onContextTextEditingFinished(const QString& text, const QPoint& position) = 0;
    virtual void onContextTextEditingCancelled() = 0;
};

#endif // ANNOTATIONHOSTADAPTER_H
