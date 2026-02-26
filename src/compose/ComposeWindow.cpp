#include "compose/ComposeWindow.h"

#include "compose/ComposeRichTextEdit.h"
#include "compose/ComposeSettingsManager.h"
#include "compose/ComposeTemplateManager.h"
#include "compose/RichClipboardExporter.h"
#include "utils/DialogThemeUtils.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QCursor>
#include <QDateTime>
#include <QEvent>
#include <QFileDialog>
#include <QGridLayout>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QKeyEvent>
#include <QKeySequence>
#include <QLabel>
#include <QListView>
#include <QLineEdit>
#include <QPushButton>
#include <QScopedValueRollback>
#include <QScreen>
#include <QTextCharFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextListFormat>
#include <QToolButton>
#include <QVBoxLayout>

namespace {
constexpr int kWindowMinWidth = 860;
constexpr int kWindowMinHeight = 620;
}

ComposeWindow::ComposeWindow(QWidget* parent)
    : QWidget(parent, Qt::Window)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle(tr("Compose"));

    setupUi();
    setMinimumSize(kWindowMinWidth, kWindowMinHeight);
    resize(980, 760);

    applyTheme();
}

ComposeWindow::~ComposeWindow() = default;

void ComposeWindow::setCaptureContext(const CaptureContext& context)
{
    m_context = context;

    if (m_context.timestamp.trimmed().isEmpty()) {
        m_context.timestamp = QDateTime::currentDateTime().toString(Qt::ISODate);
    }

    updateMetadataLabel();
    insertCaptureImageIfNeeded();
}

void ComposeWindow::showAt(const QPoint& pos)
{
    if (pos.isNull()) {
        QScreen* screen = parentWidget() ? parentWidget()->screen() : QGuiApplication::screenAt(QCursor::pos());
        if (!screen) {
            screen = QGuiApplication::primaryScreen();
        }
        if (screen) {
            const QRect screenGeometry = screen->geometry();
            move(screenGeometry.center() - rect().center());
        }
    } else {
        move(pos - QPoint(width() / 2, height() / 2));
    }

    show();
    raise();
    activateWindow();
    if (m_titleEdit) {
        m_titleEdit->setFocus();
    }
}

void ComposeWindow::onCopyAsHtml()
{
    if (!m_descriptionEdit) {
        return;
    }

    RichClipboardExporter::ComposeContent content;
    content.title = m_titleEdit ? m_titleEdit->text().trimmed() : QString();
    content.descriptionHtml = m_descriptionEdit->toHtml();
    content.descriptionMarkdown = m_descriptionEdit->toMarkdownContent();
    content.descriptionPlainText = m_descriptionEdit->toPlainText();
    content.screenshot = m_context.screenshot;

    if (ComposeSettingsManager::instance().autoIncludeMetadata()) {
        content.windowTitle = m_context.windowTitle;
        content.appName = m_context.appName;
        content.timestamp = m_context.timestamp;
        content.screenName = m_context.screenName;
        content.screenResolution = m_context.screenResolution;
        content.captureRegion = m_context.captureRegion;
    }

    QGuiApplication::clipboard()->setMimeData(
        RichClipboardExporter::exportToClipboard(content, RichClipboardExporter::ClipboardFlavor::HtmlPreferred));

    m_hasCopied = true;
    emit composeCopied(QStringLiteral("html"));
    close();
}

void ComposeWindow::onCopyAsMarkdown()
{
    if (!m_descriptionEdit) {
        return;
    }

    RichClipboardExporter::ComposeContent content;
    content.title = m_titleEdit ? m_titleEdit->text().trimmed() : QString();
    content.descriptionHtml = m_descriptionEdit->toHtml();
    content.descriptionMarkdown = m_descriptionEdit->toMarkdownContent();
    content.descriptionPlainText = m_descriptionEdit->toPlainText();
    content.screenshot = m_context.screenshot;

    if (ComposeSettingsManager::instance().autoIncludeMetadata()) {
        content.windowTitle = m_context.windowTitle;
        content.appName = m_context.appName;
        content.timestamp = m_context.timestamp;
        content.screenName = m_context.screenName;
        content.screenResolution = m_context.screenResolution;
        content.captureRegion = m_context.captureRegion;
    }

    QGuiApplication::clipboard()->setMimeData(
        RichClipboardExporter::exportToClipboard(content, RichClipboardExporter::ClipboardFlavor::MarkdownPreferred));

    m_hasCopied = true;
    emit composeCopied(QStringLiteral("markdown"));
    close();
}

void ComposeWindow::onTemplateChanged(int index)
{
    if (!m_templateCombo || index < 0 || index >= m_templateCombo->count()) {
        return;
    }

    const QString templateId = m_templateCombo->itemData(index).toString();
    applyTemplate(templateId);
    ComposeSettingsManager::instance().setDefaultTemplate(templateId);
}

void ComposeWindow::onInsertImage()
{
    if (!m_descriptionEdit) {
        return;
    }

    const QString filePath = QFileDialog::getOpenFileName(
        this,
        tr("Insert Image"),
        QString(),
        tr("Image Files (*.png *.jpg *.jpeg *.bmp *.gif *.webp *.tif *.tiff)"));
    if (filePath.isEmpty()) {
        return;
    }

    m_descriptionEdit->insertImageFromFile(filePath);
}

void ComposeWindow::onCancel()
{
    close();
}

void ComposeWindow::keyPressEvent(QKeyEvent* event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        event->accept();
        return;
    }

    const Qt::KeyboardModifiers modifiers = event->modifiers();
    const bool hasCommandModifier = modifiers.testFlag(Qt::ControlModifier) || modifiers.testFlag(Qt::MetaModifier);

    if (hasCommandModifier && modifiers.testFlag(Qt::ShiftModifier) &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        onCopyAsMarkdown();
        event->accept();
        return;
    }

    if (hasCommandModifier && !modifiers.testFlag(Qt::ShiftModifier) &&
        (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)) {
        onCopyAsHtml();
        event->accept();
        return;
    }

    if (hasCommandModifier && (event->key() == Qt::Key_Plus || event->key() == Qt::Key_Equal)) {
        applyEditorZoomDelta(1);
        event->accept();
        return;
    }

    if (hasCommandModifier && event->key() == Qt::Key_Minus) {
        applyEditorZoomDelta(-1);
        event->accept();
        return;
    }

    if (hasCommandModifier && event->key() == Qt::Key_0) {
        applyEditorZoomDelta(-m_editorZoomSteps);
        event->accept();
        return;
    }

    if (event->key() == Qt::Key_Tab && QApplication::focusWidget() == m_titleEdit && m_descriptionEdit) {
        m_descriptionEdit->setFocus();
        event->accept();
        return;
    }

    QWidget::keyPressEvent(event);
}

void ComposeWindow::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::PaletteChange ||
        event->type() == QEvent::ApplicationPaletteChange ||
        event->type() == QEvent::ThemeChange) {
        applyTheme();
    }

    QWidget::changeEvent(event);
}

void ComposeWindow::closeEvent(QCloseEvent* event)
{
    if (!m_hasCopied) {
        emit composeCancelled();
    }
    QWidget::closeEvent(event);
}

void ComposeWindow::setupUi()
{
    auto* rootLayout = new QVBoxLayout(this);
    rootLayout->setContentsMargins(14, 12, 14, 12);
    rootLayout->setSpacing(10);

    auto* headerGrid = new QGridLayout();
    headerGrid->setHorizontalSpacing(8);
    headerGrid->setVerticalSpacing(8);
    headerGrid->setColumnStretch(1, 1);

    auto* templateLabel = new QLabel(tr("Template:"), this);
    templateLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_templateCombo = new QComboBox(this);
    m_templateCombo->setObjectName(QStringLiteral("templateCombo"));
    m_templateCombo->setMaxVisibleItems(8);
    m_templateCombo->setMaximumWidth(320);
    auto* templateView = new QListView(m_templateCombo);
    templateView->setObjectName(QStringLiteral("templateComboView"));
    templateView->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    templateView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    templateView->setMinimumWidth(220);
    templateView->setMaximumWidth(360);
    m_templateCombo->setView(templateView);
    headerGrid->addWidget(templateLabel, 0, 0);
    headerGrid->addWidget(m_templateCombo, 0, 1);

    auto* titleLabel = new QLabel(tr("Title:"), this);
    titleLabel->setObjectName(QStringLiteral("fieldLabel"));
    m_titleEdit = new QLineEdit(this);
    m_titleEdit->setObjectName(QStringLiteral("titleEdit"));
    headerGrid->addWidget(titleLabel, 1, 0);
    headerGrid->addWidget(m_titleEdit, 1, 1);
    rootLayout->addLayout(headerGrid);

    auto* formattingToolbar = new QWidget(this);
    formattingToolbar->setObjectName(QStringLiteral("formattingToolbar"));
    auto* formattingLayout = new QHBoxLayout(formattingToolbar);
    formattingLayout->setContentsMargins(6, 6, 6, 6);
    formattingLayout->setSpacing(4);
    setupFormattingToolbar(formattingLayout);
    rootLayout->addWidget(formattingToolbar);

    auto* descriptionLabel = new QLabel(tr("Description:"), this);
    descriptionLabel->setObjectName(QStringLiteral("fieldLabel"));
    rootLayout->addWidget(descriptionLabel);

    m_descriptionEdit = new ComposeRichTextEdit(this);
    m_descriptionEdit->setObjectName(QStringLiteral("descriptionEdit"));
    m_descriptionEdit->setPlaceholderText(tr("Add context and notes..."));
    m_descriptionEdit->setImageMaxWidth(ComposeSettingsManager::instance().screenshotMaxWidth());
    rootLayout->addWidget(m_descriptionEdit, 1);

    auto* metadataTitle = new QLabel(tr("Metadata"), this);
    metadataTitle->setObjectName(QStringLiteral("fieldLabel"));
    rootLayout->addWidget(metadataTitle);

    m_metadataLabel = new QLabel(this);
    m_metadataLabel->setObjectName(QStringLiteral("metadataLabel"));
    m_metadataLabel->setWordWrap(true);
    rootLayout->addWidget(m_metadataLabel);

    auto* buttonBar = new QWidget(this);
    buttonBar->setObjectName(QStringLiteral("buttonBar"));
    auto* buttonLayout = new QHBoxLayout(buttonBar);
    buttonLayout->setContentsMargins(0, 0, 0, 0);
    buttonLayout->setSpacing(8);
    buttonLayout->addStretch();

    m_copyMarkdownButton = new QPushButton(tr("Copy as Markdown"), buttonBar);
    m_copyMarkdownButton->setObjectName(QStringLiteral("copyMarkdownButton"));
    connect(m_copyMarkdownButton, &QPushButton::clicked, this, &ComposeWindow::onCopyAsMarkdown);
    buttonLayout->addWidget(m_copyMarkdownButton);

    m_copyHtmlButton = new QPushButton(tr("Copy as HTML"), buttonBar);
    m_copyHtmlButton->setObjectName(QStringLiteral("copyHtmlButton"));
    connect(m_copyHtmlButton, &QPushButton::clicked, this, &ComposeWindow::onCopyAsHtml);
    buttonLayout->addWidget(m_copyHtmlButton);

    auto* cancelButton = new QPushButton(tr("Cancel"), buttonBar);
    cancelButton->setObjectName(QStringLiteral("cancelButton"));
    connect(cancelButton, &QPushButton::clicked, this, &ComposeWindow::onCancel);
    buttonLayout->addWidget(cancelButton);
    rootLayout->addWidget(buttonBar);

    const QVector<ComposeTemplate> templates = ComposeTemplateManager::instance().templates();
    for (const ComposeTemplate& tmpl : templates) {
        m_templateCombo->addItem(tmpl.displayName, tmpl.id);
    }

    connect(m_templateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ComposeWindow::onTemplateChanged);

    const QString initialTemplateId = resolveInitialTemplateId();
    const int initialIndex = m_templateCombo->findData(initialTemplateId);
    if (initialIndex >= 0) {
        m_templateCombo->setCurrentIndex(initialIndex);
    }
    applyTemplate(m_templateCombo->currentData().toString());
}

void ComposeWindow::setupFormattingToolbar(QHBoxLayout* toolbarLayout)
{
    auto createButton = [this](const QString& text, const QString& tooltip, bool checkable = false) {
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(tooltip);
        button->setCheckable(checkable);
        return button;
    };

    auto* boldButton = createButton(QStringLiteral("B"), tr("Bold (Ctrl/Cmd+B)"), true);
    boldButton->setShortcut(QKeySequence::Bold);
    connect(boldButton, &QToolButton::toggled, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontWeight(checked ? QFont::Bold : QFont::Normal);
        m_descriptionEdit->mergeCurrentCharFormat(format);
    });
    toolbarLayout->addWidget(boldButton);

    auto* italicButton = createButton(QStringLiteral("I"), tr("Italic (Ctrl/Cmd+I)"), true);
    italicButton->setShortcut(QKeySequence::Italic);
    connect(italicButton, &QToolButton::toggled, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontItalic(checked);
        m_descriptionEdit->mergeCurrentCharFormat(format);
    });
    toolbarLayout->addWidget(italicButton);

    auto* underlineButton = createButton(QStringLiteral("U"), tr("Underline (Ctrl/Cmd+U)"), true);
    underlineButton->setShortcut(QKeySequence::Underline);
    connect(underlineButton, &QToolButton::toggled, this, [this](bool checked) {
        QTextCharFormat format;
        format.setFontUnderline(checked);
        m_descriptionEdit->mergeCurrentCharFormat(format);
    });
    toolbarLayout->addWidget(underlineButton);

    auto* codeButton = createButton(QStringLiteral("</>"), tr("Inline Code (Ctrl/Cmd+Shift+C)"));
    codeButton->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+C")));
    connect(codeButton, &QToolButton::clicked, this, &ComposeWindow::applyInlineCodeFormat);
    toolbarLayout->addWidget(codeButton);

    auto* linkButton = createButton(QStringLiteral("Link"), tr("Insert Link"));
    connect(linkButton, &QToolButton::clicked, this, [this]() {
        QTextCursor cursor = m_descriptionEdit->textCursor();
        const QString selectedText = cursor.selectedText();
        const QString url = QInputDialog::getText(this, tr("Insert Link"), tr("URL:"));
        if (url.trimmed().isEmpty()) {
            return;
        }
        const QString linkText = selectedText.isEmpty() ? url.trimmed() : selectedText;
        cursor.insertHtml(QStringLiteral("<a href=\"%1\">%2</a>")
            .arg(url.toHtmlEscaped(), linkText.toHtmlEscaped()));
    });
    toolbarLayout->addWidget(linkButton);

    auto* imageButton = createButton(QStringLiteral("Img"), tr("Insert Image"));
    connect(imageButton, &QToolButton::clicked, this, &ComposeWindow::onInsertImage);
    toolbarLayout->addWidget(imageButton);

    auto* bulletButton = createButton(QStringLiteral("•"), tr("Bullet List"));
    connect(bulletButton, &QToolButton::clicked, this, [this]() {
        QTextCursor cursor = m_descriptionEdit->textCursor();
        QTextListFormat listFormat;
        listFormat.setStyle(QTextListFormat::ListDisc);
        cursor.createList(listFormat);
    });
    toolbarLayout->addWidget(bulletButton);

    auto* numberButton = createButton(QStringLiteral("1."), tr("Numbered List"));
    connect(numberButton, &QToolButton::clicked, this, [this]() {
        QTextCursor cursor = m_descriptionEdit->textCursor();
        QTextListFormat listFormat;
        listFormat.setStyle(QTextListFormat::ListDecimal);
        cursor.createList(listFormat);
    });
    toolbarLayout->addWidget(numberButton);

    toolbarLayout->addStretch();
}

void ComposeWindow::applyTheme()
{
    if (m_isApplyingTheme) {
        return;
    }

    QScopedValueRollback<bool> applyingThemeGuard(m_isApplyingTheme, true);
    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();
    const bool isLight = SnapTray::DialogTheme::isLightToolbarStyle();
    const QString popupBackground = SnapTray::DialogTheme::toCssColor(
        isLight ? QColor(252, 252, 252) : QColor(26, 30, 36));
    const QString selectedBackground = SnapTray::DialogTheme::toCssColor(
        isLight ? QColor(11, 98, 208) : QColor(76, 148, 255));
    const QString selectedText = SnapTray::DialogTheme::toCssColor(QColor(255, 255, 255));
    const QString comboBackground = SnapTray::DialogTheme::toCssColor(
        isLight ? QColor(255, 255, 255) : QColor(24, 27, 33));
    const QString comboText = SnapTray::DialogTheme::toCssColor(
        isLight ? QColor(24, 28, 34) : QColor(245, 247, 250));
    const QString comboBorder = SnapTray::DialogTheme::toCssColor(
        isLight ? QColor(134, 143, 157) : QColor(94, 103, 116));
    const QString comboHover = SnapTray::DialogTheme::toCssColor(
        isLight ? QColor(229, 236, 247) : QColor(51, 60, 73));
    const QString comboDropBackground = SnapTray::DialogTheme::toCssColor(
        isLight ? QColor(245, 247, 251) : QColor(39, 45, 55));

    setStyleSheet(QStringLiteral(R"(
        ComposeWindow {
            background-color: %1;
        }
        #fieldLabel {
            color: %2;
            font-size: 12px;
            font-weight: 600;
        }
        QLineEdit, #descriptionEdit {
            background-color: %3;
            color: %4;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 6px;
            selection-background-color: %6;
        }
        #templateCombo {
            background-color: %15;
            color: %16;
            border: 1px solid %17;
            border-radius: 6px;
            padding: 4px 28px 4px 8px;
            font-weight: 600;
        }
        #templateCombo::drop-down {
            subcontrol-origin: padding;
            subcontrol-position: top right;
            width: 22px;
            border-left: 1px solid %17;
            background-color: %18;
            border-top-right-radius: 6px;
            border-bottom-right-radius: 6px;
        }
        QListView#templateComboView {
            background-color: %8;
            color: %16;
            border: 1px solid %17;
            selection-background-color: %9;
            selection-color: %10;
            outline: 0;
            padding: 4px;
        }
        QListView#templateComboView::item {
            min-height: 24px;
            padding: 4px 8px;
            border-radius: 4px;
        }
        QListView#templateComboView::item:hover {
            background-color: %19;
            color: %16;
        }
        QListView#templateComboView::item:selected {
            background-color: %9;
            color: %10;
        }
        #formattingToolbar {
            background-color: %11;
            border: 1px solid %5;
            border-radius: 6px;
        }
        QToolButton {
            background-color: %12;
            color: %4;
            border: 1px solid %5;
            border-radius: 4px;
            min-width: 30px;
            min-height: 24px;
            padding: 2px 6px;
        }
        QToolButton:hover, QToolButton:checked {
            background-color: %7;
        }
        #metadataLabel {
            background-color: %11;
            color: %2;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 8px;
        }
        #buttonBar QPushButton {
            background-color: %12;
            color: %4;
            border: 1px solid %5;
            border-radius: 6px;
            padding: 6px 12px;
            min-height: 30px;
        }
        #buttonBar QPushButton:hover {
            background-color: %7;
        }
        #copyHtmlButton {
            background-color: %13;
            border-color: %13;
            color: %10;
        }
        #copyHtmlButton:hover {
            background-color: %14;
            border-color: %14;
        }
    )")
        .arg(SnapTray::DialogTheme::toCssColor(palette.windowBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.textSecondary))
        .arg(SnapTray::DialogTheme::toCssColor(palette.inputBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.textPrimary))
        .arg(SnapTray::DialogTheme::toCssColor(palette.inputBorder))
        .arg(SnapTray::DialogTheme::toCssColor(palette.selectionBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonHoverBackground))
        .arg(popupBackground)
        .arg(selectedBackground)
        .arg(selectedText)
        .arg(SnapTray::DialogTheme::toCssColor(palette.panelBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.buttonBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.accentBackground))
        .arg(SnapTray::DialogTheme::toCssColor(palette.accentHoverBackground))
        .arg(comboBackground)
        .arg(comboText)
        .arg(comboBorder)
        .arg(comboDropBackground)
        .arg(comboHover));

    updateEditorDocumentTheme();
}

void ComposeWindow::updateEditorDocumentTheme()
{
    if (!m_descriptionEdit || !m_descriptionEdit->document()) {
        return;
    }

    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();
    const QString linkColor = SnapTray::DialogTheme::toCssColor(palette.accentBackground);
    const QString codeBackground = SnapTray::DialogTheme::toCssColor(
        SnapTray::DialogTheme::isLightToolbarStyle() ? QColor(235, 235, 235) : QColor(66, 66, 66));
    const QString codeColor = SnapTray::DialogTheme::toCssColor(palette.textPrimary);

    m_descriptionEdit->document()->setDefaultStyleSheet(QStringLiteral(
        "a { color:%1; text-decoration: underline; }"
        "code { background-color:%2; color:%3; font-family:'Consolas','Menlo',monospace; }"
        "pre { background-color:%2; color:%3; padding:6px; border-radius:4px; }")
        .arg(linkColor, codeBackground, codeColor));
}

void ComposeWindow::applyTemplate(const QString& templateId)
{
    const ComposeTemplate tmpl = ComposeTemplateManager::instance().templateById(templateId);
    if (m_titleEdit) {
        m_titleEdit->setPlaceholderText(tmpl.titlePlaceholder);
    }
    if (m_descriptionEdit) {
        if (tmpl.bodyTemplate.isEmpty()) {
            m_descriptionEdit->clear();
        } else {
            m_descriptionEdit->setHtml(tmpl.bodyTemplate);
        }
        m_captureImageInserted = false;
        insertCaptureImageIfNeeded();
    }
}

void ComposeWindow::updateMetadataLabel()
{
    if (!m_metadataLabel) {
        return;
    }

    QStringList metadataParts;
    if (!m_context.timestamp.trimmed().isEmpty()) {
        metadataParts << tr("Captured: %1").arg(m_context.timestamp);
    }
    if (!m_context.windowTitle.trimmed().isEmpty()) {
        metadataParts << tr("Window: %1").arg(m_context.windowTitle);
    }
    if (!m_context.appName.trimmed().isEmpty()) {
        metadataParts << tr("App: %1").arg(m_context.appName);
    }
    if (!m_context.screenName.trimmed().isEmpty()) {
        metadataParts << tr("Screen: %1").arg(m_context.screenName);
    }
    if (m_context.screenResolution.isValid()) {
        metadataParts << tr("Resolution: %1x%2")
            .arg(m_context.screenResolution.width())
            .arg(m_context.screenResolution.height());
    }
    if (m_context.captureRegion.isValid()) {
        metadataParts << tr("Region: %1x%2")
            .arg(m_context.captureRegion.width())
            .arg(m_context.captureRegion.height());
    }

    m_metadataLabel->setText(metadataParts.isEmpty()
        ? tr("Metadata unavailable")
        : metadataParts.join(QStringLiteral("\n")));
}

void ComposeWindow::applyInlineCodeFormat()
{
    if (!m_descriptionEdit) {
        return;
    }

    QTextCursor cursor = m_descriptionEdit->textCursor();
    if (!cursor.hasSelection()) {
        return;
    }

    const SnapTray::DialogTheme::Palette palette = SnapTray::DialogTheme::paletteForToolbarStyle();
    const QColor codeBackground = SnapTray::DialogTheme::isLightToolbarStyle()
        ? QColor(235, 235, 235)
        : QColor(66, 66, 66);

    QTextCharFormat format;
    format.setFontFamilies({QStringLiteral("Menlo"), QStringLiteral("Consolas"), QStringLiteral("monospace")});
    format.setFontFixedPitch(true);
    format.setBackground(codeBackground);
    format.setForeground(palette.textPrimary);
    cursor.mergeCharFormat(format);
}

QString ComposeWindow::resolveInitialTemplateId() const
{
    const QString defaultTemplate = ComposeSettingsManager::instance().defaultTemplate().trimmed();
    const QVector<ComposeTemplate> templates = ComposeTemplateManager::instance().templates();
    for (const ComposeTemplate& tmpl : templates) {
        if (tmpl.id == defaultTemplate) {
            return defaultTemplate;
        }
    }
    return QStringLiteral("free");
}

void ComposeWindow::insertCaptureImageIfNeeded()
{
    if (!m_descriptionEdit || m_context.screenshot.isNull() || m_captureImageInserted) {
        return;
    }

    m_descriptionEdit->setImageMaxWidth(ComposeSettingsManager::instance().screenshotMaxWidth());
    if (m_descriptionEdit->insertImageFromPixmap(m_context.screenshot, true)) {
        m_captureImageInserted = true;
    }
}

void ComposeWindow::applyEditorZoomDelta(int deltaSteps)
{
    if (!m_descriptionEdit || deltaSteps == 0) {
        return;
    }

    if (deltaSteps > 0) {
        m_descriptionEdit->zoomIn(deltaSteps);
    } else {
        m_descriptionEdit->zoomOut(-deltaSteps);
    }
    m_editorZoomSteps += deltaSteps;
}
