#ifndef ANNOTATIONCONTEXT_H
#define ANNOTATIONCONTEXT_H

#include <QColor>
#include <QPoint>
#include <functional>

class QWidget;
class TextAnnotationEditor;
class ToolOptionsPanel;
class AnnotationHostAdapter;

namespace snaptray {
namespace colorwidgets {
class ColorPickerDialogCompat;
}
}

class AnnotationContext
{
public:
    explicit AnnotationContext(AnnotationHostAdapter& host);

    void setupTextAnnotationEditor(bool attachOptionsPanel = true,
                                   bool connectUpdateSignal = true) const;
    void connectTextEditorSignals() const;
    void connectToolOptionsSignals() const;
    void connectTextFormattingSignals() const;
    void syncTextFormattingControls() const;

    static void showColorPickerDialog(
        QWidget* hostWidget,
        snaptray::colorwidgets::ColorPickerDialogCompat*& dialog,
        const QColor& currentColor,
        const QPoint& centerPoint,
        const std::function<void(const QColor&)>& onColorSelected);

private:
    AnnotationHostAdapter& m_host;
};

#endif // ANNOTATIONCONTEXT_H
