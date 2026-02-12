#include "update/UpdateDialog.h"
#include "update/UpdateChecker.h"
#include "update/UpdateSettingsManager.h"
#include "version.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTextBrowser>
#include <QDesktopServices>
#include <QUrl>
#include <QFont>
#include <QApplication>
#include <QPalette>

namespace {
// Dialog sizes
constexpr int kDialogWidthUpdate = 480;
constexpr int kDialogMinHeightUpdate = 320;
constexpr int kDialogMaxHeightUpdate = 500;
constexpr int kDialogWidthSmall = 320;
constexpr int kDialogHeightSmall = 220;

// Spacing
constexpr int kDialogPadding = 24;
constexpr int kSectionSpacing = 20;
constexpr int kElementSpacing = 12;
constexpr int kButtonSpacing = 12;

// Icon sizes
constexpr int kIconSize = 48;

// Release notes
constexpr int kReleaseNotesMinHeight = 120;
constexpr int kReleaseNotesMaxHeight = 200;
}

UpdateDialog::UpdateDialog(const ReleaseInfo& release,
                           QWidget* parent)
    : QDialog(parent)
    , m_mode(Mode::UpdateAvailable)
    , m_release(release)
    , m_iconLabel(nullptr)
    , m_titleLabel(nullptr)
    , m_messageLabel(nullptr)
    , m_releaseNotes(nullptr)
    , m_primaryButton(nullptr)
    , m_secondaryButton(nullptr)
    , m_tertiaryButton(nullptr)
{
    setupCommonStyles();
    setupUpdateAvailableUI();
}

// Private constructor for factory methods - no UI setup
UpdateDialog::UpdateDialog(Mode mode, QWidget* parent)
    : QDialog(parent)
    , m_mode(mode)
    , m_iconLabel(nullptr)
    , m_titleLabel(nullptr)
    , m_messageLabel(nullptr)
    , m_releaseNotes(nullptr)
    , m_primaryButton(nullptr)
    , m_secondaryButton(nullptr)
    , m_tertiaryButton(nullptr)
{
    setupCommonStyles();
}

UpdateDialog* UpdateDialog::createUpToDateDialog(QWidget* parent)
{
    UpdateDialog* dialog = new UpdateDialog(Mode::UpToDate, parent);
    dialog->setupUpToDateUI();
    return dialog;
}

UpdateDialog* UpdateDialog::createErrorDialog(const QString& error, QWidget* parent)
{
    UpdateDialog* dialog = new UpdateDialog(Mode::Error, parent);
    dialog->setupErrorUI(error);
    return dialog;
}

void UpdateDialog::setupCommonStyles()
{
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setAttribute(Qt::WA_DeleteOnClose);
}

void UpdateDialog::setupUpdateAvailableUI()
{
    setWindowTitle(tr("Update Available"));
    setFixedWidth(kDialogWidthUpdate);
    setMinimumHeight(kDialogMinHeightUpdate);
    setMaximumHeight(kDialogMaxHeightUpdate);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(kDialogPadding, kDialogPadding,
                                   kDialogPadding, kDialogPadding);
    mainLayout->setSpacing(kSectionSpacing);

    // Header: Icon + Title
    QHBoxLayout* headerLayout = new QHBoxLayout();
    headerLayout->setSpacing(16);

    m_iconLabel = new QLabel(this);
    m_iconLabel->setText(QString::fromUtf8("\xF0\x9F\x8E\x89")); // 🎉
    QFont iconFont = m_iconLabel->font();
    iconFont.setPointSize(32);
    m_iconLabel->setFont(iconFont);
    m_iconLabel->setFixedSize(kIconSize, kIconSize);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    headerLayout->addWidget(m_iconLabel);

    m_titleLabel = new QLabel(tr("New Version Available!"), this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(18);
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    headerLayout->addWidget(m_titleLabel);
    headerLayout->addStretch();

    mainLayout->addLayout(headerLayout);

    // Version info
    QVBoxLayout* versionLayout = new QVBoxLayout();
    versionLayout->setSpacing(4);

    QString mainMsg = tr("%1 %2 is now available.")
                          .arg(SNAPTRAY_APP_NAME, m_release.version);
    QLabel* mainMsgLabel = new QLabel(mainMsg, this);
    QFont mainFont = mainMsgLabel->font();
    mainFont.setPointSize(14);
    mainFont.setWeight(QFont::Medium);
    mainMsgLabel->setFont(mainFont);
    versionLayout->addWidget(mainMsgLabel);

    QString subMsg = tr("You are currently using %1").arg(UpdateChecker::currentVersion());
    m_messageLabel = new QLabel(subMsg, this);
    QPalette pal = m_messageLabel->palette();
    pal.setColor(QPalette::WindowText, QColor(100, 100, 100));
    m_messageLabel->setPalette(pal);
    versionLayout->addWidget(m_messageLabel);

    mainLayout->addLayout(versionLayout);

    // Release notes
    QLabel* notesLabel = new QLabel(tr("What's New"), this);
    QFont notesFont = notesLabel->font();
    notesFont.setPointSize(13);
    notesFont.setWeight(QFont::DemiBold);
    notesLabel->setFont(notesFont);
    mainLayout->addWidget(notesLabel);

    m_releaseNotes = new QTextBrowser(this);
    m_releaseNotes->setMinimumHeight(kReleaseNotesMinHeight);
    m_releaseNotes->setMaximumHeight(kReleaseNotesMaxHeight);
    m_releaseNotes->setOpenExternalLinks(true);
    m_releaseNotes->setMarkdown(m_release.releaseNotes);
    m_releaseNotes->setStyleSheet(
        "QTextBrowser { "
        "  background-color: palette(base); "
        "  border: 1px solid palette(mid); "
        "  border-radius: 8px; "
        "  padding: 12px; "
        "}");
    mainLayout->addWidget(m_releaseNotes);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(kButtonSpacing);
    buttonLayout->addStretch();

    m_primaryButton = createPrimaryButton(tr("Download"));
    connect(m_primaryButton, &QPushButton::clicked, this, &UpdateDialog::onDownload);
    buttonLayout->addWidget(m_primaryButton);

    m_secondaryButton = createSecondaryButton(tr("Remind Later"));
    connect(m_secondaryButton, &QPushButton::clicked, this, &UpdateDialog::onRemindLater);
    buttonLayout->addWidget(m_secondaryButton);

    m_tertiaryButton = createTertiaryButton(tr("Skip Version"));
    connect(m_tertiaryButton, &QPushButton::clicked, this, &UpdateDialog::onSkipVersion);
    buttonLayout->addWidget(m_tertiaryButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

void UpdateDialog::setupUpToDateUI()
{
    setWindowTitle(tr("Update Check"));
    setFixedSize(kDialogWidthSmall, kDialogHeightSmall);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(kDialogPadding, kDialogPadding,
                                   kDialogPadding, kDialogPadding);
    mainLayout->setSpacing(kElementSpacing);
    mainLayout->setAlignment(Qt::AlignCenter);

    // Checkmark icon
    m_iconLabel = new QLabel(this);
    m_iconLabel->setText(QString::fromUtf8("\xE2\x9C\x93")); // ✓
    QFont iconFont = m_iconLabel->font();
    iconFont.setPointSize(32);
    iconFont.setBold(true);
    m_iconLabel->setFont(iconFont);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    m_iconLabel->setStyleSheet("QLabel { color: #34C759; }"); // Green
    mainLayout->addWidget(m_iconLabel);

    mainLayout->addSpacing(kElementSpacing);

    // Title
    m_titleLabel = new QLabel(tr("You're up to date!"), this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);

    // Message
    m_messageLabel = new QLabel(
        tr("%1 %2 is the latest version.")
            .arg(SNAPTRAY_APP_NAME, UpdateChecker::currentVersion()), this);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    QPalette pal = m_messageLabel->palette();
    pal.setColor(QPalette::WindowText, QColor(100, 100, 100));
    m_messageLabel->setPalette(pal);
    mainLayout->addWidget(m_messageLabel);

    mainLayout->addSpacing(kSectionSpacing);

    // OK button
    m_primaryButton = createPrimaryButton(tr("OK"));
    connect(m_primaryButton, &QPushButton::clicked, this, &QDialog::accept);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    buttonLayout->addWidget(m_primaryButton);
    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

void UpdateDialog::setupErrorUI(const QString& error)
{
    setWindowTitle(tr("Update Check"));
    setFixedSize(340, 240);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(kDialogPadding, kDialogPadding,
                                   kDialogPadding, kDialogPadding);
    mainLayout->setSpacing(kElementSpacing);
    mainLayout->setAlignment(Qt::AlignCenter);

    // Warning icon
    m_iconLabel = new QLabel(this);
    m_iconLabel->setText(QString::fromUtf8("\xE2\x9A\xA0\xEF\xB8\x8F")); // ⚠️
    QFont iconFont = m_iconLabel->font();
    iconFont.setPointSize(32);
    m_iconLabel->setFont(iconFont);
    m_iconLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_iconLabel);

    mainLayout->addSpacing(kElementSpacing);

    // Title
    m_titleLabel = new QLabel(tr("Unable to check for updates"), this);
    QFont titleFont = m_titleLabel->font();
    titleFont.setPointSize(16);
    titleFont.setWeight(QFont::DemiBold);
    m_titleLabel->setFont(titleFont);
    m_titleLabel->setAlignment(Qt::AlignCenter);
    mainLayout->addWidget(m_titleLabel);

    // Message
    QString displayError = error.isEmpty()
                               ? tr("Please check your internet connection and try again.")
                               : error;
    m_messageLabel = new QLabel(displayError, this);
    m_messageLabel->setWordWrap(true);
    m_messageLabel->setAlignment(Qt::AlignCenter);
    QPalette pal = m_messageLabel->palette();
    pal.setColor(QPalette::WindowText, QColor(100, 100, 100));
    m_messageLabel->setPalette(pal);
    mainLayout->addWidget(m_messageLabel);

    mainLayout->addSpacing(kSectionSpacing);

    // Buttons
    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->setSpacing(kButtonSpacing);
    buttonLayout->addStretch();

    m_primaryButton = createPrimaryButton(tr("Try Again"));
    connect(m_primaryButton, &QPushButton::clicked, this, &UpdateDialog::onRetry);
    buttonLayout->addWidget(m_primaryButton);

    m_secondaryButton = createSecondaryButton(tr("Close"));
    connect(m_secondaryButton, &QPushButton::clicked, this, &QDialog::accept);
    buttonLayout->addWidget(m_secondaryButton);

    buttonLayout->addStretch();
    mainLayout->addLayout(buttonLayout);
}

QPushButton* UpdateDialog::createPrimaryButton(const QString& text)
{
    QPushButton* button = new QPushButton(text, this);
    button->setMinimumWidth(100);
    button->setMinimumHeight(36);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(
        "QPushButton { "
        "  background-color: #007AFF; "
        "  color: white; "
        "  border: none; "
        "  border-radius: 8px; "
        "  padding: 0 20px; "
        "  font-size: 14px; "
        "  font-weight: 500; "
        "} "
        "QPushButton:hover { "
        "  background-color: #0066DD; "
        "} "
        "QPushButton:pressed { "
        "  background-color: #0055CC; "
        "}");
    return button;
}

QPushButton* UpdateDialog::createSecondaryButton(const QString& text)
{
    QPushButton* button = new QPushButton(text, this);
    button->setMinimumWidth(100);
    button->setMinimumHeight(36);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(
        "QPushButton { "
        "  background-color: transparent; "
        "  border: 1px solid rgba(0, 0, 0, 0.15); "
        "  border-radius: 8px; "
        "  padding: 0 20px; "
        "  font-size: 14px; "
        "} "
        "QPushButton:hover { "
        "  background-color: rgba(0, 0, 0, 0.05); "
        "} "
        "QPushButton:pressed { "
        "  background-color: rgba(0, 0, 0, 0.1); "
        "}");
    return button;
}

QPushButton* UpdateDialog::createTertiaryButton(const QString& text)
{
    QPushButton* button = new QPushButton(text, this);
    button->setMinimumHeight(36);
    button->setCursor(Qt::PointingHandCursor);
    button->setStyleSheet(
        "QPushButton { "
        "  background-color: transparent; "
        "  border: none; "
        "  color: #666666; "
        "  font-size: 13px; "
        "  text-decoration: none; "
        "} "
        "QPushButton:hover { "
        "  text-decoration: underline; "
        "}");
    return button;
}

void UpdateDialog::onDownload()
{
    // Open the release page in browser
    QString url = m_release.htmlUrl;
    if (url.isEmpty()) {
        // Fallback to GitHub releases page
        url = QString("https://github.com/%1/%2/releases")
                  .arg(UpdateChecker::kGitHubOwner, UpdateChecker::kGitHubRepo);
    }
    QDesktopServices::openUrl(QUrl(url));
    accept();
}

void UpdateDialog::onRemindLater()
{
    // Just close the dialog, don't modify any settings
    accept();
}

void UpdateDialog::onSkipVersion()
{
    if (!m_release.version.isEmpty()) {
        UpdateSettingsManager::instance().setSkippedVersion(m_release.version);
    }
    accept();
}

void UpdateDialog::onRetry()
{
    emit retryRequested();
    accept();
}
