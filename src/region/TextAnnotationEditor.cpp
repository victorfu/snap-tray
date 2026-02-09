#include "region/TextAnnotationEditor.h"
#include "annotations/AnnotationLayer.h"
#include "annotations/TextBoxAnnotation.h"
#include "InlineTextEditor.h"
#include "toolbar/ToolOptionsPanel.h"
#include "settings/Settings.h"
#include <QWidget>
#include <QSettings>
#include <QtMath>
#include <QFontMetrics>

namespace {
    const char* SETTINGS_KEY_TEXT_BOLD = "annotation/text_bold";
    const char* SETTINGS_KEY_TEXT_ITALIC = "annotation/text_italic";
    const char* SETTINGS_KEY_TEXT_UNDERLINE = "annotation/text_underline";
    const char* SETTINGS_KEY_TEXT_SIZE = "annotation/text_size";
    const char* SETTINGS_KEY_TEXT_FAMILY = "annotation/text_family";

    QPoint toRoundedPoint(const QPointF& point)
    {
        return QPoint(qRound(point.x()), qRound(point.y()));
    }

    QPointF baselineLocalOffset(const QFont& font)
    {
        QFontMetrics fm(font);
        return QPointF(TextBoxAnnotation::kPadding, fm.ascent() + TextBoxAnnotation::kPadding);
    }
}

TextAnnotationEditor::TextAnnotationEditor(QObject* parent)
    : QObject(parent)
    , m_displayToAnnotationMapper([](const QPointF& p) { return p; })
    , m_annotationToDisplayMapper([](const QPointF& p) { return p; })
{
    loadSettings();
}

void TextAnnotationEditor::setAnnotationLayer(AnnotationLayer* layer)
{
    m_annotationLayer = layer;
}

void TextAnnotationEditor::setTextEditor(InlineTextEditor* editor)
{
    m_textEditor = editor;
}

void TextAnnotationEditor::setColorAndWidthWidget(ToolOptionsPanel* widget)
{
    m_colorAndWidthWidget = widget;
}

void TextAnnotationEditor::setParentWidget(QWidget* widget)
{
    m_parentWidget = widget;
}

void TextAnnotationEditor::setCoordinateMappers(
    const std::function<QPointF(const QPointF&)>& displayToAnnotation,
    const std::function<QPointF(const QPointF&)>& annotationToDisplay)
{
    m_displayToAnnotationMapper = displayToAnnotation
        ? displayToAnnotation
        : [](const QPointF& p) { return p; };
    m_annotationToDisplayMapper = annotationToDisplay
        ? annotationToDisplay
        : [](const QPointF& p) { return p; };
}

void TextAnnotationEditor::startEditing(const QPoint& pos, const QRect& selectionRect, const QColor& color)
{
    if (!m_textEditor) return;

    m_editSessionActive = true;
    m_editingIndex = -1;  // Creating new annotation
    m_textEditor->setColor(color);
    m_textEditor->setFont(m_formatting.toQFont());
    m_textEditor->startEditing(pos, selectionRect);
}

void TextAnnotationEditor::startReEditing(int annotationIndex, const QColor& color)
{
    if (!m_annotationLayer || !m_textEditor) {
        m_editSessionActive = false;
        return;
    }

    auto* textItem = dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->itemAt(annotationIndex));
    if (!textItem) {
        m_editSessionActive = false;
        return;
    }

    m_editSessionActive = true;
    m_editingIndex = annotationIndex;

    // Extract formatting from existing annotation
    m_formatting = TextFormattingState::fromQFont(textItem->font());

    // Update UI to reflect current formatting
    if (m_colorAndWidthWidget) {
        m_colorAndWidthWidget->setCurrentColor(textItem->color());
        m_colorAndWidthWidget->setBold(m_formatting.bold);
        m_colorAndWidthWidget->setItalic(m_formatting.italic);
        m_colorAndWidthWidget->setUnderline(m_formatting.underline);
        m_colorAndWidthWidget->setFontSize(m_formatting.fontSize);
        m_colorAndWidthWidget->setFontFamily(m_formatting.fontFamily);
    }

    // Start editor with existing text
    m_textEditor->setColor(textItem->color());
    m_textEditor->setFont(m_formatting.toQFont());

    // Get selection rect from parent if available
    QRect selectionRect;
    if (m_parentWidget) {
        selectionRect = m_parentWidget->rect();
    }

    QPointF baselineAnnotation = textItem->mapLocalPointToTransformed(baselineLocalOffset(textItem->font()));
    QPoint baselineDisplay = toRoundedPoint(m_annotationToDisplayMapper(baselineAnnotation));
    m_textEditor->startEditingExisting(baselineDisplay, selectionRect, textItem->text());

    // Hide the original annotation while editing (prevent duplicate display)
    textItem->setVisible(false);

    // Clear selection to hide gizmo while editing
    m_annotationLayer->clearSelection();
    emit updateRequested();
}

bool TextAnnotationEditor::finishEditing(const QString& text, const QPoint& position, const QColor& color)
{
    if (!m_annotationLayer) return false;
    if (!m_editSessionActive) return false;

    // Ignore duplicate finish callbacks for the same edit session.
    m_editSessionActive = false;

    QFont font = m_formatting.toQFont();
    QPointF baselineAnnotation = m_displayToAnnotationMapper(QPointF(position));
    QPointF localBaseline = baselineLocalOffset(font);
    bool createdNew = false;

    if (m_editingIndex >= 0) {
        // Re-editing: restore visibility first
        auto* textItem = dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->itemAt(m_editingIndex));
        if (textItem) {
            textItem->setVisible(true);  // Restore visibility
            if (!text.isEmpty()) {
                const qreal originalRotation = textItem->rotation();
                const qreal originalScale = textItem->scale();
                const bool originalMirrorX = textItem->mirrorX();
                const bool originalMirrorY = textItem->mirrorY();

                textItem->setText(text);
                textItem->setFont(font);
                textItem->setColor(color);
                textItem->setRotation(originalRotation);
                textItem->setScale(originalScale);
                textItem->setMirror(originalMirrorX, originalMirrorY);

                QPointF topLeft = textItem->topLeftFromTransformedLocalPoint(baselineAnnotation, localBaseline);
                textItem->setPosition(topLeft);
            }
        }
        m_annotationLayer->setSelectedIndex(m_editingIndex);
        m_editingIndex = -1;
    }
    else if (!text.isEmpty()) {
        // Create new TextBoxAnnotation
        auto textAnnotation = std::make_unique<TextBoxAnnotation>(QPointF(0, 0), text, font, color);
        QPointF topLeft = textAnnotation->topLeftFromTransformedLocalPoint(baselineAnnotation, localBaseline);
        textAnnotation->setPosition(topLeft);
        m_annotationLayer->addItem(std::move(textAnnotation));

        // Auto-select the newly created text annotation to show the gizmo
        int newIndex = static_cast<int>(m_annotationLayer->itemCount()) - 1;
        m_annotationLayer->setSelectedIndex(newIndex);
        createdNew = true;
    }

    emit editingFinished();
    emit updateRequested();
    return createdNew;
}

void TextAnnotationEditor::cancelEditing()
{
    m_editSessionActive = false;

    if (m_editingIndex >= 0 && m_annotationLayer) {
        // Restore visibility of the original annotation
        auto* textItem = dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->itemAt(m_editingIndex));
        if (textItem) {
            textItem->setVisible(true);
        }
        m_editingIndex = -1;
    }
    emit editingCancelled();
    emit updateRequested();
}

bool TextAnnotationEditor::isEditing() const
{
    return m_textEditor && m_textEditor->isEditing();
}

void TextAnnotationEditor::startTransformation(const QPoint& pos, GizmoHandle handle)
{
    if (!m_annotationLayer) return;

    auto* textItem = dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->selectedItem());
    if (!textItem) return;

    m_isTransforming = true;
    m_activeGizmoHandle = handle;
    m_transformStartCenter = textItem->center();
    m_transformStartRotation = textItem->rotation();
    m_transformStartScale = textItem->scale();

    // Calculate initial angle and distance from center to mouse
    QPointF delta = QPointF(pos) - m_transformStartCenter;
    m_transformStartAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
    m_transformStartDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());

    m_dragStart = pos;
}

void TextAnnotationEditor::updateTransformation(const QPoint& pos)
{
    if (!m_annotationLayer) return;

    auto* textItem = dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->selectedItem());
    if (!textItem) return;

    QPointF center = m_transformStartCenter;
    QPointF delta = QPointF(pos) - center;

    switch (m_activeGizmoHandle) {
    case GizmoHandle::Rotation: {
        // Calculate new angle from center to mouse
        qreal currentAngle = qRadiansToDegrees(qAtan2(delta.y(), delta.x()));
        qreal angleDelta = currentAngle - m_transformStartAngle;

        // Apply rotation
        textItem->setRotation(m_transformStartRotation + angleDelta);
        break;
    }

    case GizmoHandle::TopLeft:
    case GizmoHandle::TopRight:
    case GizmoHandle::BottomLeft:
    case GizmoHandle::BottomRight: {
        // Calculate scale based on distance from center
        qreal currentDistance = qSqrt(delta.x() * delta.x() + delta.y() * delta.y());

        if (m_transformStartDistance > 0) {
            qreal scaleFactor = currentDistance / m_transformStartDistance;
            // Clamp scale to reasonable range (10% to 1000%)
            scaleFactor = qBound(0.1, m_transformStartScale * scaleFactor, 10.0);
            textItem->setScale(scaleFactor);
        }
        break;
    }

    case GizmoHandle::Body: {
        // Move the annotation
        QPoint moveD = pos - m_dragStart;
        textItem->moveBy(moveD);
        m_dragStart = pos;
        break;
    }

    default:
        break;
    }

    emit transformationUpdated();
    emit updateRequested();
}

void TextAnnotationEditor::finishTransformation()
{
    m_isTransforming = false;
    m_activeGizmoHandle = GizmoHandle::None;
}

void TextAnnotationEditor::startDragging(const QPoint& pos)
{
    m_isDragging = true;
    m_dragStart = pos;
}

void TextAnnotationEditor::updateDragging(const QPoint& pos)
{
    if (!m_annotationLayer || !m_isDragging) return;

    auto* textItem = dynamic_cast<TextBoxAnnotation*>(m_annotationLayer->selectedItem());
    if (!textItem) return;

    QPoint delta = pos - m_dragStart;
    textItem->moveBy(delta);
    m_dragStart = pos;
    emit updateRequested();
}

void TextAnnotationEditor::finishDragging()
{
    m_isDragging = false;
}

void TextAnnotationEditor::setBold(bool enabled)
{
    m_formatting.bold = enabled;
    saveSettings();
    if (m_textEditor && m_textEditor->isEditing()) {
        m_textEditor->setBold(enabled);
    }
    emit formattingChanged();
    emit updateRequested();
}

void TextAnnotationEditor::setItalic(bool enabled)
{
    m_formatting.italic = enabled;
    saveSettings();
    if (m_textEditor && m_textEditor->isEditing()) {
        m_textEditor->setItalic(enabled);
    }
    emit formattingChanged();
    emit updateRequested();
}

void TextAnnotationEditor::setUnderline(bool enabled)
{
    m_formatting.underline = enabled;
    saveSettings();
    if (m_textEditor && m_textEditor->isEditing()) {
        m_textEditor->setUnderline(enabled);
    }
    emit formattingChanged();
    emit updateRequested();
}

void TextAnnotationEditor::setFontSize(int size)
{
    m_formatting.fontSize = size;
    saveSettings();
    if (m_colorAndWidthWidget) {
        m_colorAndWidthWidget->setFontSize(size);
    }
    if (m_textEditor && m_textEditor->isEditing()) {
        m_textEditor->setFontSize(size);
    }
    emit formattingChanged();
    emit updateRequested();
}

void TextAnnotationEditor::setFontFamily(const QString& family)
{
    m_formatting.fontFamily = family;
    saveSettings();
    if (m_colorAndWidthWidget) {
        m_colorAndWidthWidget->setFontFamily(family);
    }
    if (m_textEditor && m_textEditor->isEditing()) {
        m_textEditor->setFontFamily(family);
    }
    emit formattingChanged();
    emit updateRequested();
}

void TextAnnotationEditor::loadSettings()
{
    auto settings = SnapTray::getSettings();
    m_formatting.bold = settings.value(SETTINGS_KEY_TEXT_BOLD, true).toBool();
    m_formatting.italic = settings.value(SETTINGS_KEY_TEXT_ITALIC, false).toBool();
    m_formatting.underline = settings.value(SETTINGS_KEY_TEXT_UNDERLINE, false).toBool();
    m_formatting.fontSize = settings.value(SETTINGS_KEY_TEXT_SIZE, 16).toInt();
    m_formatting.fontFamily = settings.value(SETTINGS_KEY_TEXT_FAMILY, QString()).toString();
}

void TextAnnotationEditor::saveSettings()
{
    auto settings = SnapTray::getSettings();
    settings.setValue(SETTINGS_KEY_TEXT_BOLD, m_formatting.bold);
    settings.setValue(SETTINGS_KEY_TEXT_ITALIC, m_formatting.italic);
    settings.setValue(SETTINGS_KEY_TEXT_UNDERLINE, m_formatting.underline);
    settings.setValue(SETTINGS_KEY_TEXT_SIZE, m_formatting.fontSize);
    settings.setValue(SETTINGS_KEY_TEXT_FAMILY, m_formatting.fontFamily);
}

bool TextAnnotationEditor::isDoubleClick(const QPoint& pos, qint64 currentTime) const
{
    if (m_lastClickTime == 0) {
        return false;
    }

    qint64 timeDiff = currentTime - m_lastClickTime;
    if (timeDiff > kDoubleClickInterval) {
        return false;
    }

    int dx = pos.x() - m_lastClickPos.x();
    int dy = pos.y() - m_lastClickPos.y();
    int distanceSq = dx * dx + dy * dy;
    if (distanceSq > kDoubleClickDistance * kDoubleClickDistance) {
        return false;
    }

    return true;
}

void TextAnnotationEditor::recordClick(const QPoint& pos, qint64 time)
{
    m_lastClickPos = pos;
    m_lastClickTime = time;
}
