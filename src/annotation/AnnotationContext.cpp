#include "annotation/AnnotationContext.h"

#include "annotation/AnnotationHostAdapter.h"
#include "annotations/AnnotationLayer.h"
#include "InlineTextEditor.h"
#include "region/TextAnnotationEditor.h"
#include "region/RegionSettingsHelper.h"
#include "colorwidgets/ColorPickerDialogCompat.h"

#include <QWidget>

using snaptray::colorwidgets::ColorPickerDialogCompat;

AnnotationContext::AnnotationContext(AnnotationHostAdapter& host)
    : m_host(host)
{
}

void AnnotationContext::setupTextAnnotationEditor(bool connectUpdateSignal) const
{
    auto* textAnnotationEditor = m_host.textAnnotationEditorForContext();
    if (!textAnnotationEditor) {
        return;
    }

    textAnnotationEditor->setAnnotationLayer(m_host.annotationLayerForContext());
    textAnnotationEditor->setTextEditor(m_host.inlineTextEditorForContext());
    textAnnotationEditor->setParentWidget(m_host.annotationHostWidget());

    if (connectUpdateSignal && m_host.annotationHostWidget()) {
        QObject::connect(textAnnotationEditor,
                         &TextAnnotationEditor::updateRequested,
                         m_host.annotationHostWidget(),
                         QOverload<>::of(&QWidget::update));
    }
}

void AnnotationContext::connectTextEditorSignals() const
{
    auto* textEditor = m_host.inlineTextEditorForContext();
    QWidget* hostWidget = m_host.annotationHostWidget();
    if (!textEditor || !hostWidget) {
        return;
    }

    QObject::connect(textEditor,
                     &InlineTextEditor::editingFinished,
                     hostWidget,
                     [this](const QString& text, const QPoint& position) {
                         m_host.onContextTextEditingFinished(text, position);
                     });

    QObject::connect(textEditor,
                     &InlineTextEditor::editingCancelled,
                     hostWidget,
                     [this]() {
                         m_host.onContextTextEditingCancelled();
                     });
}

void AnnotationContext::showTextFontSizeDropdown(RegionSettingsHelper& settingsHelper,
                                                 const QPoint& pos) const
{
    auto* textAnnotationEditor = m_host.textAnnotationEditorForContext();
    if (!textAnnotationEditor) {
        return;
    }
    TextFormattingState textFormatting = textAnnotationEditor->formatting();
    settingsHelper.showFontSizeDropdown(pos, textFormatting.fontSize);
}

void AnnotationContext::showTextFontFamilyDropdown(RegionSettingsHelper& settingsHelper,
                                                   const QPoint& pos) const
{
    auto* textAnnotationEditor = m_host.textAnnotationEditorForContext();
    if (!textAnnotationEditor) {
        return;
    }
    TextFormattingState textFormatting = textAnnotationEditor->formatting();
    settingsHelper.showFontFamilyDropdown(pos, textFormatting.fontFamily);
}

void AnnotationContext::applyTextFontSize(int size) const
{
    auto* textAnnotationEditor = m_host.textAnnotationEditorForContext();
    if (!textAnnotationEditor) {
        return;
    }
    textAnnotationEditor->setFontSize(size);
}

void AnnotationContext::applyTextFontFamily(const QString& family) const
{
    auto* textAnnotationEditor = m_host.textAnnotationEditorForContext();
    if (!textAnnotationEditor) {
        return;
    }
    textAnnotationEditor->setFontFamily(family);
}

void AnnotationContext::showColorPickerDialog(
    QWidget* hostWidget,
    std::unique_ptr<ColorPickerDialogCompat>& dialog,
    const QColor& currentColor,
    const QPoint& centerPoint,
    const std::function<void(const QColor&)>& onColorSelected,
    const std::function<std::unique_ptr<ColorPickerDialogCompat>()>& dialogFactory)
{
    if (!hostWidget) {
        return;
    }

    if (!dialog) {
        dialog = dialogFactory ? dialogFactory() : std::make_unique<ColorPickerDialogCompat>();

        QObject::connect(dialog.get(),
                         &ColorPickerDialogCompat::colorSelected,
                         hostWidget,
                         [onColorSelected](const QColor& color) {
                             if (onColorSelected) {
                                 onColorSelected(color);
                             }
                         });
    }

    dialog->setCurrentColor(currentColor);
    dialog->setPlacementAnchor(centerPoint);
    dialog->move(centerPoint.x() - 170, centerPoint.y() - 210);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
