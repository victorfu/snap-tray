#ifndef OCRMANAGER_H
#define OCRMANAGER_H

#include <QList>
#include <QObject>
#include <QPixmap>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>

class QThread;

// Callback type for async OCR results
using OCRCallback = std::function<void(bool success, const QString &text, const QString &error)>;

/**
 * @brief Information about an available OCR language.
 */
struct OCRLanguageInfo {
    QString code;        ///< Language code (e.g., "en-US", "zh-Hant", "ja-JP")
    QString nativeName;  ///< Native name (e.g., "English", "繁體中文", "日本語")
    QString englishName; ///< English name (e.g., "English", "Traditional Chinese", "Japanese")
};

class OCRManager : public QObject
{
    Q_OBJECT

public:
    explicit OCRManager(QObject *parent = nullptr);
    ~OCRManager();

    /**
     * @brief Get list of available OCR languages on this system.
     * @return List of available languages with their display names.
     */
    static QList<OCRLanguageInfo> availableLanguages();

    /**
     * @brief Set the recognition languages in priority order.
     * @param languageCodes List of language codes, first = highest priority.
     */
    void setRecognitionLanguages(const QStringList &languageCodes);

    /**
     * @brief Get the current recognition languages.
     * @return List of language codes in priority order.
     */
    QStringList recognitionLanguages() const;

    /**
     * @brief Perform OCR on the given pixmap.
     * @param pixmap The image to recognize text from.
     * @param callback Callback for async result delivery.
     */
    void recognizeText(const QPixmap &pixmap, const OCRCallback &callback);

    /**
     * @brief Check if OCR is available on this platform.
     * @return true if OCR engine is available.
     */
    static bool isAvailable();

signals:
    void recognitionComplete(bool success, const QString &text, const QString &error);

private:
    void beginShutdown();

    QStringList m_languages{"en-US"};
    QSet<QThread*> m_activeWorkers;
    bool m_shuttingDown = false;
    static constexpr int kWorkerShutdownTimeoutMs = 1500;
};

#endif // OCRMANAGER_H
