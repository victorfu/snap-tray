#ifndef COMPOSETEMPLATEMANAGER_H
#define COMPOSETEMPLATEMANAGER_H

#include <QString>
#include <QVector>

struct ComposeTemplate
{
    QString id;
    QString displayName;
    QString titlePlaceholder;
    QString bodyTemplate;
};

class ComposeTemplateManager
{
public:
    static ComposeTemplateManager& instance();

    QVector<ComposeTemplate> templates() const;
    ComposeTemplate templateById(const QString& id) const;

private:
    ComposeTemplateManager();
    ~ComposeTemplateManager() = default;
    ComposeTemplateManager(const ComposeTemplateManager&) = delete;
    ComposeTemplateManager& operator=(const ComposeTemplateManager&) = delete;

    QVector<ComposeTemplate> m_templates;
};

#endif // COMPOSETEMPLATEMANAGER_H
