#include "annotation/AnnotationContext.h"

#include "annotation/AnnotationHostAdapter.h"
#include "annotations/AnnotationLayer.h"
#include "toolbar/ToolOptionsPanel.h"
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

void AnnotationContext::setupTextAnnotationEditor(bool attachOptionsPanel,
                                                  bool connectUpdateSignal) const
{
    auto* textAnnotationEditor = m_host.textAnnotationEditorForContext();
    if (!textAnnotationEditor) {
        return;
    }

    textAnnotationEditor->setAnnotationLayer(m_host.annotationLayerForContext());
    textAnnotationEditor->setTextEditor(m_host.inlineTextEditorForContext());
    textAnnotationEditor->setParentWidget(m_host.annotationHostWidget());

    if (attachOptionsPanel) {
        textAnnotationEditor->setColorAndWidthWidget(m_host.toolOptionsPanelForContext());
    }

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

void AnnotationContext::connectToolOptionsSignals() const
{
    auto* optionsPanel = m_host.toolOptionsPanelForContext();
    QWidget* hostWidget = m_host.annotationHostWidget();
    if (!optionsPanel || !hostWidget) {
        return;
    }

    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::colorSelected,
                     hostWidget,
                     [this](const QColor& color) {
                         m_host.onContextColorSelected(color);
                     });

    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::customColorPickerRequested,
                     hostWidget,
                     [this]() {
                         m_host.onContextMoreColorsRequested();
                     });

    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::widthChanged,
                     hostWidget,
                     [this](int width) {
                         m_host.onContextLineWidthChanged(width);
                     });

    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::arrowStyleChanged,
                     hostWidget,
                     [this](LineEndStyle style) {
                         m_host.onContextArrowStyleChanged(style);
                     });

    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::lineStyleChanged,
                     hostWidget,
                     [this](LineStyle style) {
                         m_host.onContextLineStyleChanged(style);
                     });

    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::fontSizeDropdownRequested,
                     hostWidget,
                     [this](const QPoint& pos) {
                         m_host.onContextFontSizeDropdownRequested(pos);
                     });

    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::fontFamilyDropdownRequested,
                     hostWidget,
                     [this](const QPoint& pos) {
                         m_host.onContextFontFamilyDropdownRequested(pos);
                     });
}

void AnnotationContext::connectTextFormattingSignals() const
{
    auto* optionsPanel = m_host.toolOptionsPanelForContext();
    auto* textAnnotationEditor = m_host.textAnnotationEditorForContext();
    if (!optionsPanel || !textAnnotationEditor) {
        return;
    }

    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::boldToggled,
                     textAnnotationEditor,
                     &TextAnnotationEditor::setBold);
    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::italicToggled,
                     textAnnotationEditor,
                     &TextAnnotationEditor::setItalic);
    QObject::connect(optionsPanel,
                     &ToolOptionsPanel::underlineToggled,
                     textAnnotationEditor,
                     &TextAnnotationEditor::setUnderline);
}

void AnnotationContext::syncTextFormattingControls() const
{
    auto* optionsPanel = m_host.toolOptionsPanelForContext();
    auto* textAnnotationEditor = m_host.textAnnotationEditorForContext();
    if (!optionsPanel || !textAnnotationEditor) {
        return;
    }

    TextFormattingState textFormatting = textAnnotationEditor->formatting();
    optionsPanel->setBold(textFormatting.bold);
    optionsPanel->setItalic(textFormatting.italic);
    optionsPanel->setUnderline(textFormatting.underline);
    optionsPanel->setFontSize(textFormatting.fontSize);
    optionsPanel->setFontFamily(textFormatting.fontFamily);
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
    const std::function<void(const QColor&)>& onColorSelected)
{
    if (!hostWidget) {
        return;
    }

    if (!dialog) {
        dialog = std::make_unique<ColorPickerDialogCompat>();

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
    dialog->move(centerPoint.x() - 170, centerPoint.y() - 210);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}
