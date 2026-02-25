#include "InlineTextEditor.h"
#include "cursor/CursorManager.h"

#include <QTextEdit>
#include <QTextDocument>
#include <QTextCursor>
#include <QFontMetrics>
#include <QWidget>
#include <QKeyEvent>

const int InlineTextEditor::MIN_WIDTH;
const int InlineTextEditor::MIN_HEIGHT;
const int InlineTextEditor::PADDING;

namespace {
    constexpr int kEditorBorderPx = 1;
    constexpr qreal kLightTextLumaThreshold = 0.72;

    qreal relativeLuma(const QColor& color)
    {
        const QColor rgb = color.toRgb();
        return 0.2126 * rgb.redF() + 0.7152 * rgb.greenF() + 0.0722 * rgb.blueF();
    }

    bool needsDarkEditorBackdrop(const QColor& textColor)
    {
        return relativeLuma(textColor) >= kLightTextLumaThreshold;
    }
}

InlineTextEditor::InlineTextEditor(QWidget* parent)
    : QObject(parent)
    , m_parentWidget(parent)
    , m_textEdit(nullptr)
    , m_isEditing(false)
    , m_isConfirmMode(false)
    , m_isDragging(false)
    , m_color(Qt::red)
{
    m_font.setPointSize(16);
    m_font.setBold(true);
}

InlineTextEditor::~InlineTextEditor()
{
    // QTextEdit is a child of m_parentWidget, so it will be deleted automatically
}

void InlineTextEditor::startEditing(const QPoint& pos, const QRect& bounds)
{
    startEditingInternal(pos, bounds, QString(), false);
}

void InlineTextEditor::startEditingExisting(const QPoint& pos, const QRect& bounds, const QString& existingText)
{
    // Preserve the original annotation baseline when re-editing existing text.
    startEditingInternal(pos, bounds, existingText, true);
}

void InlineTextEditor::startEditingInternal(const QPoint& pos,
                                            const QRect& bounds,
                                            const QString& existingText,
                                            bool preserveBaselineOnClamp)
{
    // Create QTextEdit lazily
    if (!m_textEdit) {
        m_textEdit = new QTextEdit(m_parentWidget);
        m_textEdit->setFrameStyle(QFrame::NoFrame);
        m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEdit->setAcceptRichText(false);
        m_textEdit->setContentsMargins(0, 0, 0, 0);
        m_textEdit->document()->setDocumentMargin(0.0);

        // Connect for auto-resize
        connect(m_textEdit->document(), &QTextDocument::contentsChanged,
                this, &InlineTextEditor::onContentsChanged);
    }

    m_bounds = bounds;

    // Reset states from previous editing session
    m_isConfirmMode = false;
    m_isDragging = false;
    m_textEdit->setReadOnly(false);
    CursorManager::instance().pushCursorForWidget(m_textEdit, CursorContext::Tool, Qt::IBeamCursor);
    m_textEdit->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    // Update style and font
    updateStyle();
    m_textEdit->setFont(m_font);
    QFontMetrics fm(m_font);

    // pos is treated as the text baseline position (where text starts).
    // Convert baseline to editor top-left using the actual QTextEdit cursor inset.
    QPoint boxTopLeft = boxTopLeftFromBaseline(pos);
    int boxX = boxTopLeft.x();
    int boxY = boxTopLeft.y();
    int width = MIN_WIDTH;
    int height = qMax(MIN_HEIGHT, fm.lineSpacing() + 2 * PADDING + 2 * kEditorBorderPx);

    // Clamp to bounds
    if (boxX + width > bounds.right()) {
        boxX = bounds.right() - width;
    }
    if (boxY + height > bounds.bottom()) {
        boxY = bounds.bottom() - height;
    }
    boxX = qMax(bounds.left(), boxX);
    boxY = qMax(bounds.top(), boxY);

    // Store baseline position used when committing text.
    // For new text, keep it aligned with the clamped editor geometry so the
    // committed annotation appears where the user actually typed.
    if (preserveBaselineOnClamp) {
        m_textPosition = pos;
    } else {
        m_textPosition = baselineFromBoxTopLeft(QPoint(boxX, boxY));
    }

    // Set geometry and show
    m_textEdit->setGeometry(boxX, boxY, width, height);

    // Set existing text if re-editing
    if (!existingText.isEmpty()) {
        m_textEdit->setPlainText(existingText);
        // Move cursor to end
        QTextCursor cursor = m_textEdit->textCursor();
        cursor.movePosition(QTextCursor::End);
        m_textEdit->setTextCursor(cursor);
        // Adjust size for existing text
        adjustSize();
    } else {
        m_textEdit->clear();
    }

    m_textEdit->show();
    m_textEdit->setFocus();
    m_textEdit->raise();

    m_isEditing = true;
}

QString InlineTextEditor::finishEditing()
{
    if (!m_isEditing || !m_textEdit) {
        return QString();
    }

    // Remove event filter installed during confirm mode
    m_textEdit->removeEventFilter(this);

    QString text = m_textEdit->toPlainText().trimmed();

    m_textEdit->hide();
    m_textEdit->clear();
    m_isEditing = false;
    m_isConfirmMode = false;
    m_isDragging = false;

    // Return focus to parent widget so it can receive keyboard events
    if (m_parentWidget) {
        m_parentWidget->setFocus();
    }

    if (!text.isEmpty()) {
        // m_textPosition is already the baseline position
        emit editingFinished(text, m_textPosition);
    }

    return text;
}

void InlineTextEditor::enterConfirmMode()
{
    if (!m_isEditing || !m_textEdit) return;

    // Switch from editing mode to confirm mode
    m_isConfirmMode = true;
    m_textEdit->setReadOnly(true);

    // Install event filter to intercept Enter/Escape keys in confirm mode
    // IMPORTANT: Keep focus on QTextEdit so the event filter receives key events
    // The event filter will intercept Enter/Escape before QTextEdit processes them
    m_textEdit->installEventFilter(this);

    // DON'T clear focus - keep focus on QTextEdit so event filter works
    // For floating frameless windows like PinWindow, transferring focus doesn't
    // reliably deliver keyboard events to the parent widget

    // Disable mouse events on QTextEdit so parent can handle dragging
    m_textEdit->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // Change cursor to indicate draggable
    CursorManager::instance().pushCursorForWidget(m_textEdit, CursorContext::Drag, Qt::SizeAllCursor);
}

void InlineTextEditor::cancelEditing()
{
    if (!m_isEditing || !m_textEdit) {
        return;
    }

    // Remove event filter installed during confirm mode
    m_textEdit->removeEventFilter(this);

    m_textEdit->hide();
    m_textEdit->clear();
    m_isEditing = false;
    m_isConfirmMode = false;
    m_isDragging = false;

    // Return focus to parent widget so it can receive keyboard events
    if (m_parentWidget) {
        m_parentWidget->setFocus();
    }

    emit editingCancelled();
}

void InlineTextEditor::setColor(const QColor& color)
{
    m_color = color;
    if (m_isEditing && m_textEdit) {
        updateStyle();
    }
}

void InlineTextEditor::setFont(const QFont& font)
{
    m_font = font;
    if (m_isEditing && m_textEdit) {
        m_textEdit->setFont(font);
        updateStyle();
        adjustSize();
    }
}

void InlineTextEditor::setBold(bool enabled)
{
    m_font.setBold(enabled);
    if (m_isEditing && m_textEdit) {
        m_textEdit->setFont(m_font);
        updateStyle();
        adjustSize();
    }
}

void InlineTextEditor::setItalic(bool enabled)
{
    m_font.setItalic(enabled);
    if (m_isEditing && m_textEdit) {
        m_textEdit->setFont(m_font);
        updateStyle();
        adjustSize();
    }
}

void InlineTextEditor::setUnderline(bool enabled)
{
    m_font.setUnderline(enabled);
    if (m_isEditing && m_textEdit) {
        m_textEdit->setFont(m_font);
        updateStyle();
        adjustSize();
    }
}

void InlineTextEditor::setFontSize(int size)
{
    m_font.setPointSize(size);
    if (m_isEditing && m_textEdit) {
        m_textEdit->setFont(m_font);
        updateStyle();
        adjustSize();
    }
}

void InlineTextEditor::setFontFamily(const QString& family)
{
    if (family.isEmpty()) {
        // Reset to default system font
        QFont defaultFont;
        m_font.setFamily(defaultFont.family());
    } else {
        m_font.setFamily(family);
    }
    if (m_isEditing && m_textEdit) {
        m_textEdit->setFont(m_font);
        updateStyle();
        adjustSize();
    }
}

bool InlineTextEditor::contains(const QPoint& pos) const
{
    if (!m_isEditing || !m_textEdit) {
        return false;
    }
    return m_textEdit->geometry().contains(pos);
}

QPoint InlineTextEditor::baselineOffsetInEditor() const
{
    QFontMetrics fm(m_font);
    // Keep baseline conversion invariant to editor scroll position.
    return QPoint(kEditorBorderPx + PADDING,
                  kEditorBorderPx + PADDING + fm.ascent());
}

QPoint InlineTextEditor::boxTopLeftFromBaseline(const QPoint& baseline) const
{
    return baseline - baselineOffsetInEditor();
}

QPoint InlineTextEditor::baselineFromBoxTopLeft(const QPoint& boxTopLeft) const
{
    return boxTopLeft + baselineOffsetInEditor();
}

void InlineTextEditor::handleMousePress(const QPoint& pos)
{
    if (!m_isConfirmMode || !m_textEdit) return;

    if (m_textEdit->geometry().contains(pos)) {
        // Start dragging
        m_isDragging = true;
        m_dragStartPos = pos;
        m_dragStartTextPos = m_textPosition;
    }
}

void InlineTextEditor::handleMouseMove(const QPoint& pos)
{
    if (!m_isDragging || !m_textEdit) return;

    // Calculate delta and update position
    QPoint delta = pos - m_dragStartPos;
    m_textPosition = m_dragStartTextPos + delta;

    // Update input box position
    QPoint boxTopLeft = boxTopLeftFromBaseline(m_textPosition);
    int boxX = boxTopLeft.x();
    int boxY = boxTopLeft.y();

    // Clamp to bounds
    boxX = qMax(m_bounds.left(), boxX);
    boxY = qMax(m_bounds.top(), boxY);
    if (boxX + m_textEdit->width() > m_bounds.right()) {
        boxX = m_bounds.right() - m_textEdit->width();
    }
    if (boxY + m_textEdit->height() > m_bounds.bottom()) {
        boxY = m_bounds.bottom() - m_textEdit->height();
    }

    m_textEdit->move(boxX, boxY);

    // Keep baseline position consistent with the actual on-screen editor
    // geometry after clamping to bounds.
    m_textPosition = baselineFromBoxTopLeft(QPoint(boxX, boxY));
}

void InlineTextEditor::handleMouseRelease(const QPoint& pos)
{
    Q_UNUSED(pos);
    m_isDragging = false;
}

void InlineTextEditor::onContentsChanged()
{
    if (m_isEditing && m_textEdit) {
        adjustSize();
    }
}

void InlineTextEditor::updateStyle()
{
    if (!m_textEdit) return;

    // Build font style properties
    QString fontWeight = m_font.bold() ? "bold" : "normal";
    QString fontStyle = m_font.italic() ? "italic" : "normal";
    QString textDecoration = m_font.underline() ? "underline" : "none";
    QString fontFamily = m_font.family().isEmpty() ? "" : QString("font-family: \"%1\";").arg(m_font.family());
    const bool useDarkBackdrop = needsDarkEditorBackdrop(m_color);
    const QString normalBackground = useDarkBackdrop
        ? QStringLiteral("rgba(0, 0, 0, 110)")
        : QStringLiteral("rgba(220, 242, 255, 42)");
    const QString focusedBackground = useDarkBackdrop
        ? QStringLiteral("rgba(0, 0, 0, 150)")
        : QStringLiteral("rgba(220, 242, 255, 70)");

    // Keep blue focus chrome while preserving contrast for light text colors.
    QString styleSheet = QString(
        "QTextEdit {"
        "  background: %7;"
        "  color: %1;"
        "  border: 1px solid rgba(0, 174, 255, 125);"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "  font-size: %2pt;"
        "  font-weight: %3;"
        "  font-style: %4;"
        "  text-decoration: %5;"
        "  %6"
        "}"
        "QTextEdit:focus {"
        "  background: %8;"
        "  border: 1px solid rgba(0, 174, 255, 220);"
        "}"
    ).arg(m_color.name())
     .arg(m_font.pointSize())
     .arg(fontWeight)
     .arg(fontStyle)
     .arg(textDecoration)
     .arg(fontFamily)
     .arg(normalBackground)
     .arg(focusedBackground);
    m_textEdit->setStyleSheet(styleSheet);
}

void InlineTextEditor::adjustSize()
{
    if (!m_textEdit) return;

    // Calculate actual text width using QFontMetrics
    QString text = m_textEdit->toPlainText();
    QFontMetrics fm(m_font);

    // Find the widest line
    int maxLineWidth = 0;
    QStringList lines = text.split('\n');
    for (const QString& line : lines) {
        int lineWidth = fm.horizontalAdvance(line);
        maxLineWidth = qMax(maxLineWidth, lineWidth);
    }

    // Add padding for border and internal padding
    const int horizontalChrome = 2 * PADDING + 2 * kEditorBorderPx;
    const int verticalChrome = 2 * PADDING + 2 * kEditorBorderPx;
    int newWidth = qMax(MIN_WIDTH, maxLineWidth + horizontalChrome + 4);
    int newHeight = qMax(MIN_HEIGHT, lines.count() * fm.lineSpacing() + verticalChrome);

    // Calculate box position from baseline position (m_textPosition)
    QPoint boxTopLeft = boxTopLeftFromBaseline(m_textPosition);
    int boxX = boxTopLeft.x();
    int boxY = boxTopLeft.y();

    // Clamp to bounds
    if (boxX + newWidth > m_bounds.right()) {
        newWidth = m_bounds.right() - boxX;
    }
    if (boxY + newHeight > m_bounds.bottom()) {
        newHeight = m_bounds.bottom() - boxY;
    }
    newWidth = qMax(100, newWidth);
    newHeight = qMax(30, newHeight);

    m_textEdit->setFixedSize(newWidth, newHeight);
}

bool InlineTextEditor::eventFilter(QObject* obj, QEvent* event)
{
    if (obj == m_textEdit && m_isConfirmMode && event->type() == QEvent::KeyPress) {
        QKeyEvent* keyEvent = static_cast<QKeyEvent*>(event);

        if (keyEvent->key() == Qt::Key_Return || keyEvent->key() == Qt::Key_Enter) {
            // Finish editing on Enter
            finishEditing();
            return true;  // Event handled
        }
        else if (keyEvent->key() == Qt::Key_Escape) {
            // Cancel editing on Escape
            cancelEditing();
            return true;  // Event handled
        }
    }

    return QObject::eventFilter(obj, event);
}
