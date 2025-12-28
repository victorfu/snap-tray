#include "video/FormatSelectionWidget.h"

#include <QButtonGroup>
#include <QHBoxLayout>
#include <QPushButton>

FormatSelectionWidget::FormatSelectionWidget(QWidget *parent)
    : QWidget(parent)
    , m_currentFormat(Format::MP4)
{
    setupUI();
}

void FormatSelectionWidget::setupUI()
{
    QHBoxLayout *layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    m_buttonGroup = new QButtonGroup(this);
    m_buttonGroup->setExclusive(true);

    // Create format buttons with tooltips
    m_mp4Btn = new QPushButton("MP4", this);
    m_mp4Btn->setFixedSize(kButtonWidth, kButtonHeight);
    m_mp4Btn->setCheckable(true);
    m_mp4Btn->setChecked(true);
    m_mp4Btn->setCursor(Qt::PointingHandCursor);
    m_mp4Btn->setToolTip(tr("Best quality, widely compatible"));
    m_buttonGroup->addButton(m_mp4Btn, static_cast<int>(Format::MP4));
    layout->addWidget(m_mp4Btn);

    m_gifBtn = new QPushButton("GIF", this);
    m_gifBtn->setFixedSize(kButtonWidth, kButtonHeight);
    m_gifBtn->setCheckable(true);
    m_gifBtn->setCursor(Qt::PointingHandCursor);
    m_gifBtn->setToolTip(tr("Universal support, larger file size"));
    m_buttonGroup->addButton(m_gifBtn, static_cast<int>(Format::GIF));
    layout->addWidget(m_gifBtn);

    m_webpBtn = new QPushButton("WebP", this);
    m_webpBtn->setFixedSize(kButtonWidth, kButtonHeight);
    m_webpBtn->setCheckable(true);
    m_webpBtn->setCursor(Qt::PointingHandCursor);
    m_webpBtn->setToolTip(tr("Small file size, modern browsers only"));
    m_buttonGroup->addButton(m_webpBtn, static_cast<int>(Format::WebP));
    layout->addWidget(m_webpBtn);

    // Apply initial styles
    updateButtonStyles();

    // Connect button group signal
    connect(m_buttonGroup, &QButtonGroup::idClicked,
            this, &FormatSelectionWidget::onButtonClicked);
}

FormatSelectionWidget::Format FormatSelectionWidget::selectedFormat() const
{
    return m_currentFormat;
}

void FormatSelectionWidget::setSelectedFormat(Format format)
{
    if (m_currentFormat != format) {
        m_currentFormat = format;
        m_buttonGroup->button(static_cast<int>(format))->setChecked(true);
        updateButtonStyles();
    }
}

void FormatSelectionWidget::onButtonClicked(int id)
{
    Format newFormat = static_cast<Format>(id);
    if (m_currentFormat != newFormat) {
        m_currentFormat = newFormat;
        updateButtonStyles();
        emit formatChanged(newFormat);
    }
}

void FormatSelectionWidget::updateButtonStyles()
{
    // Style for selected and unselected states
    QString selectedStyle = R"(
        QPushButton {
            background-color: #007AFF;
            color: white;
            border: 1px solid #0066CC;
            font-weight: bold;
        }
    )";

    QString unselectedStyle = R"(
        QPushButton {
            background-color: #3A3A3C;
            color: #CCCCCC;
            border: 1px solid #48484A;
        }
        QPushButton:hover {
            background-color: #48484A;
        }
    )";

    // Left button (rounded left corners)
    QString leftStyle = R"(
        QPushButton {
            border-top-left-radius: 6px;
            border-bottom-left-radius: 6px;
            border-top-right-radius: 0px;
            border-bottom-right-radius: 0px;
        }
    )";

    // Middle button (no rounded corners)
    QString middleStyle = R"(
        QPushButton {
            border-radius: 0px;
            border-left: none;
        }
    )";

    // Right button (rounded right corners)
    QString rightStyle = R"(
        QPushButton {
            border-top-left-radius: 0px;
            border-bottom-left-radius: 0px;
            border-top-right-radius: 6px;
            border-bottom-right-radius: 6px;
            border-left: none;
        }
    )";

    m_mp4Btn->setStyleSheet(
        (m_currentFormat == Format::MP4 ? selectedStyle : unselectedStyle) + leftStyle);
    m_gifBtn->setStyleSheet(
        (m_currentFormat == Format::GIF ? selectedStyle : unselectedStyle) + middleStyle);
    m_webpBtn->setStyleSheet(
        (m_currentFormat == Format::WebP ? selectedStyle : unselectedStyle) + rightStyle);
}
