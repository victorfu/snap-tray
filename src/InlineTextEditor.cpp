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

    // Calculate initial position (clamped to bounds)
    int x = pos.x();
    int y = pos.y();
    int width = MIN_WIDTH;
    int height = MIN_HEIGHT;

    if (x + width > bounds.right()) {
        x = bounds.right() - width;
    }
    if (y + height > bounds.bottom()) {
        y = bounds.bottom() - height;
    }
    x = qMax(bounds.left(), x);
    y = qMax(bounds.top(), y);

    m_textPosition = QPoint(x, y);

    // Update style and font
    updateStyle();
    m_textEdit->setFont(m_font);

    // Set geometry and show
    m_textEdit->setGeometry(x, y, width, height);
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

    if (!text.isEmpty()) {
        // Calculate baseline position: add padding offset and font ascent
        QFontMetrics fm(m_font);
        QPoint baselinePos = m_textPosition;
        baselinePos.setX(baselinePos.x() + PADDING);
        baselinePos.setY(baselinePos.y() + PADDING + fm.ascent());

        emit editingFinished(text, baselinePos);
    }

    return text;
}

void InlineTextEditor::cancelEditing()
{
    if (!m_isEditing || !m_textEdit) {
        return;
    }

    m_textEdit->hide();
    m_textEdit->clear();
    m_isEditing = false;

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

void InlineTextEditor::onContentsChanged()
{
    if (m_isEditing && m_textEdit) {
        adjustSize();
    }
}

void InlineTextEditor::updateStyle()
{
    if (!m_textEdit) return;

    QString styleSheet = QString(
        "QTextEdit {"
        "  background: rgba(255, 255, 255, 230);"
        "  color: %1;"
        "  font-size: %2pt;"
        "  font-weight: bold;"
        "  border: 2px solid %1;"
        "  border-radius: 4px;"
        "  padding: 4px;"
        "}"
    ).arg(m_color.name()).arg(m_font.pointSize());
    m_textEdit->setStyleSheet(styleSheet);
}

void InlineTextEditor::adjustSize()
{
    if (!m_textEdit) return;

    // Auto-resize based on content
    QSizeF docSize = m_textEdit->document()->size();
    int newWidth = qMax(MIN_WIDTH, static_cast<int>(docSize.width()) + 20);
    int newHeight = qMax(MIN_HEIGHT, static_cast<int>(docSize.height()) + 10);

    // Clamp to bounds
    int x = m_textPosition.x();
    int y = m_textPosition.y();

    if (x + newWidth > m_bounds.right()) {
        newWidth = m_bounds.right() - x;
    }
    if (y + newHeight > m_bounds.bottom()) {
        newHeight = m_bounds.bottom() - y;
    }
    newWidth = qMax(100, newWidth);
    newHeight = qMax(30, newHeight);

    m_textEdit->setFixedSize(newWidth, newHeight);
}
