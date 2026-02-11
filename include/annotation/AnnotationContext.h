#ifndef ANNOTATIONCONTEXT_H
#define ANNOTATIONCONTEXT_H

#include <QColor>
#include <QPoint>
#include <QString>
#include <functional>

class QWidget;
class TextAnnotationEditor;
class ToolOptionsPanel;
class AnnotationHostAdapter;
class RegionSettingsHelper;

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
    void showTextFontSizeDropdown(RegionSettingsHelper& settingsHelper, const QPoint& pos) const;
    void showTextFontFamilyDropdown(RegionSettingsHelper& settingsHelper, const QPoint& pos) const;
    void applyTextFontSize(int size) const;
    void applyTextFontFamily(const QString& family) const;

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
