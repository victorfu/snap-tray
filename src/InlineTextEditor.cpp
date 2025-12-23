#include "InlineTextEditor.h"

#include <QTextEdit>
#include <QTextDocument>
#include <QFontMetrics>
#include <QWidget>

const int InlineTextEditor::MIN_WIDTH;
const int InlineTextEditor::MIN_HEIGHT;
const int InlineTextEditor::PADDING;

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
    // Create QTextEdit lazily
    if (!m_textEdit) {
        m_textEdit = new QTextEdit(m_parentWidget);
        m_textEdit->setFrameStyle(QFrame::NoFrame);
        m_textEdit->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEdit->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        m_textEdit->setAcceptRichText(false);

        // Connect for auto-resize
        connect(m_textEdit->document(), &QTextDocument::contentsChanged,
                this, &InlineTextEditor::onContentsChanged);
    }

    m_bounds = bounds;

    // pos is treated as the text baseline position (where text starts)
    // Calculate input box top-left from baseline position
    QFontMetrics fm(m_font);
    int boxX = pos.x() - PADDING;
    int boxY = pos.y() - fm.ascent() - PADDING;
    int width = MIN_WIDTH;
    int height = MIN_HEIGHT;

    // Clamp to bounds
    if (boxX + width > bounds.right()) {
        boxX = bounds.right() - width;
    }
    if (boxY + height > bounds.bottom()) {
        boxY = bounds.bottom() - height;
    }
    boxX = qMax(bounds.left(), boxX);
    boxY = qMax(bounds.top(), boxY);

    // Store the baseline position (text position)
    m_textPosition = pos;

    // Reset states from previous editing session
    m_isConfirmMode = false;
    m_isDragging = false;
    m_textEdit->setReadOnly(false);
    m_textEdit->setCursor(Qt::IBeamCursor);
    m_textEdit->setAttribute(Qt::WA_TransparentForMouseEvents, false);

    // Update style and font
    updateStyle();
    m_textEdit->setFont(m_font);

    // Set geometry and show
    m_textEdit->setGeometry(boxX, boxY, width, height);
    m_textEdit->clear();
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

    QString text = m_textEdit->toPlainText().trimmed();

    m_textEdit->hide();
    m_textEdit->clear();
    m_isEditing = false;
    m_isConfirmMode = false;
    m_isDragging = false;

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
    m_textEdit->clearFocus();

    // Disable mouse events on QTextEdit so parent can handle dragging
    m_textEdit->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    // Change cursor to indicate draggable
    m_textEdit->setCursor(Qt::SizeAllCursor);
}

void InlineTextEditor::cancelEditing()
{
    if (!m_isEditing || !m_textEdit) {
        return;
    }

    m_textEdit->hide();
    m_textEdit->clear();
    m_isEditing = false;
    m_isConfirmMode = false;
    m_isDragging = false;

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
    QFontMetrics fm(m_font);
    int boxX = m_textPosition.x() - PADDING;
    int boxY = m_textPosition.y() - fm.ascent() - PADDING;

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

    // Transparent background + white border + colored text
    QString styleSheet = QString(
        "QTextEdit {"
        "  background: transparent;"
        "  color: %1;"
        "  border: 1px solid white;"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "  font-size: %2pt;"
        "  font-weight: bold;"
        "}"
    ).arg(m_color.name()).arg(m_font.pointSize());
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
    int newWidth = qMax(MIN_WIDTH, maxLineWidth + 30);
    int newHeight = qMax(MIN_HEIGHT, lines.count() * fm.lineSpacing() + 20);

    // Calculate box position from baseline position (m_textPosition)
    int boxX = m_textPosition.x() - PADDING;
    int boxY = m_textPosition.y() - fm.ascent() - PADDING;

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
