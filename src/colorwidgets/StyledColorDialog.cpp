#include "colorwidgets/StyledColorDialog.h"

#include "platform/WindowLevel.h"

#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace snaptray {
namespace colorwidgets {

StyledColorDialog::StyledColorDialog(QWidget* parent) : ColorDialog(parent)
{
    applySnapTrayStyle();
    if (m_customTitleBar) {
        setupCustomTitleBar();
    }
}

StyledColorDialog::StyledColorDialog(const QColor& initial, QWidget* parent)
    : ColorDialog(initial, parent)
{
    applySnapTrayStyle();
    if (m_customTitleBar) {
        setupCustomTitleBar();
    }
}

StyledColorDialog::~StyledColorDialog() = default;

void StyledColorDialog::applySnapTrayStyle()
{
    // Apply SnapTray dark theme
    setStyleSheet(R"(
        StyledColorDialog {
            background-color: rgb(40, 40, 40);
            border: 1px solid rgb(70, 70, 70);
            border-radius: 8px;
        }
        QLabel {
            color: white;
            font-size: 12px;
        }
        QSpinBox {
            background-color: rgb(55, 55, 55);
            border: 1px solid rgb(70, 70, 70);
            border-radius: 4px;
            color: white;
            padding: 4px;
        }
        QSpinBox::up-button, QSpinBox::down-button {
            background-color: rgb(60, 60, 60);
            border: none;
        }
        QSpinBox::up-button:hover, QSpinBox::down-button:hover {
            background-color: rgb(80, 80, 80);
        }
        QLineEdit {
            background-color: rgb(55, 55, 55);
            border: 1px solid rgb(70, 70, 70);
            border-radius: 4px;
            color: white;
            padding: 4px 8px;
        }
        QPushButton {
            background-color: rgb(60, 60, 60);
            border: 1px solid rgb(70, 70, 70);
            border-radius: 4px;
            color: white;
            padding: 6px 16px;
            min-width: 60px;
        }
        QPushButton:hover {
            background-color: rgb(80, 80, 80);
        }
        QPushButton:pressed {
            background-color: rgb(50, 50, 50);
        }
        QPushButton#okButton {
            background-color: rgb(0, 120, 200);
            border: none;
        }
        QPushButton#okButton:hover {
            background-color: rgb(0, 140, 220);
        }
        QPushButton#okButton:pressed {
            background-color: rgb(0, 100, 180);
        }
    )");

    // Make frameless if using custom title bar, and stay on top
    if (m_customTitleBar) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Dialog | Qt::WindowStaysOnTopHint);
        setAttribute(Qt::WA_TranslucentBackground, true);
    }
}

void StyledColorDialog::setupCustomTitleBar()
{
    // Create a custom title bar widget
    auto* titleBar = new QWidget(this);
    titleBar->setFixedHeight(32);
    titleBar->setStyleSheet(
        "background-color: rgb(35, 35, 35); border-top-left-radius: 8px; "
        "border-top-right-radius: 8px;");

    auto* titleLayout = new QHBoxLayout(titleBar);
    titleLayout->setContentsMargins(12, 0, 8, 0);
    titleLayout->setSpacing(0);

    m_titleLabel = new QLabel(windowTitle(), titleBar);
    m_titleLabel->setStyleSheet("color: white; font-weight: bold;");
    titleLayout->addWidget(m_titleLabel);

    titleLayout->addStretch();

    m_closeButton = new QPushButton(titleBar);
    m_closeButton->setFixedSize(20, 20);
    m_closeButton->setStyleSheet(R"(
        QPushButton {
            background-color: transparent;
            border: none;
            border-radius: 10px;
            color: white;
            font-weight: bold;
        }
        QPushButton:hover {
            background-color: rgb(200, 60, 60);
        }
    )");
    m_closeButton->setText("\u2715");  // Unicode X
    titleLayout->addWidget(m_closeButton);

    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);

    // Insert title bar at the top
    auto* mainLayout = qobject_cast<QVBoxLayout*>(layout());
    if (mainLayout) {
        mainLayout->insertWidget(0, titleBar);
    }
}

void StyledColorDialog::showEvent(QShowEvent* event)
{
    ColorDialog::showEvent(event);

    // Set window level above RegionSelector/ScreenCanvas overlays
    raiseWindowAboveOverlays(this);
}

void StyledColorDialog::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Draw rounded rectangle background
    painter.setPen(QPen(QColor(70, 70, 70), 1));
    painter.setBrush(QColor(40, 40, 40));
    painter.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 8, 8);

    // Don't call base class - we're fully custom painting
    // ColorDialog::paintEvent(event);
    Q_UNUSED(event);
}

void StyledColorDialog::mousePressEvent(QMouseEvent* event)
{
    if (m_customTitleBar && event->button() == Qt::LeftButton) {
        // Check if in title bar area (top 32 pixels)
        if (event->position().y() < 32) {
            m_dragging = true;
            m_dragStartPos = event->globalPosition().toPoint() - frameGeometry().topLeft();
            event->accept();
            return;
        }
    }
    ColorDialog::mousePressEvent(event);
}

void StyledColorDialog::mouseMoveEvent(QMouseEvent* event)
{
    if (m_dragging) {
        move(event->globalPosition().toPoint() - m_dragStartPos);
        event->accept();
        return;
    }
    ColorDialog::mouseMoveEvent(event);
}

void StyledColorDialog::mouseReleaseEvent(QMouseEvent* event)
{
    if (m_dragging) {
        m_dragging = false;
        event->accept();
        return;
    }
    ColorDialog::mouseReleaseEvent(event);
}

bool StyledColorDialog::hasCustomTitleBar() const
{
    return m_customTitleBar;
}

void StyledColorDialog::setCustomTitleBar(bool custom)
{
    if (m_customTitleBar != custom) {
        m_customTitleBar = custom;
        // Would need to rebuild UI to change this at runtime
    }
}

QColor StyledColorDialog::getColor(const QColor& initial, QWidget* parent, const QString& title)
{
    StyledColorDialog dialog(initial, parent);
    if (!title.isEmpty()) {
        dialog.setWindowTitle(title);
        if (dialog.m_titleLabel) {
            dialog.m_titleLabel->setText(title);
        }
    }

    if (dialog.exec() == QDialog::Accepted) {
        return dialog.color();
    }
    return initial;
}

}  // namespace colorwidgets
}  // namespace snaptray
