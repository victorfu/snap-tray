#include "compose/ComposeTemplateManager.h"

#include <QObject>

ComposeTemplateManager& ComposeTemplateManager::instance()
{
    static ComposeTemplateManager instance;
    return instance;
}

ComposeTemplateManager::ComposeTemplateManager()
{
    m_templates = {
        {
            QStringLiteral("bug_report"),
            QObject::tr("Bug Report"),
            QObject::tr("Brief description of the bug..."),
            QStringLiteral(
                "<p><b>Steps to Reproduce:</b></p>"
                "<ol><li></li><li></li><li></li></ol>"
                "<p><b>Expected Behavior:</b></p><p></p>"
                "<p><b>Actual Behavior:</b></p><p></p>"
                "<p><b>Environment:</b></p><p></p>")
        },
        {
            QStringLiteral("feature_request"),
            QObject::tr("Feature Request"),
            QObject::tr("Feature title..."),
            QStringLiteral(
                "<p><b>Problem:</b></p><p></p>"
                "<p><b>Proposed Solution:</b></p><p></p>"
                "<p><b>Alternatives Considered:</b></p><p></p>")
        },
        {
            QStringLiteral("note"),
            QObject::tr("Note"),
            QObject::tr("Note title..."),
            QString()
        },
        {
            QStringLiteral("free"),
            QObject::tr("Free Form"),
            QObject::tr("Title (optional)..."),
            QString()
        }
    };
}

QVector<ComposeTemplate> ComposeTemplateManager::templates() const
{
    return m_templates;
}

ComposeTemplate ComposeTemplateManager::templateById(const QString& id) const
{
    for (const ComposeTemplate& tmpl : m_templates) {
        if (tmpl.id == id) {
            return tmpl;
        }
    }

    for (const ComposeTemplate& tmpl : m_templates) {
        if (tmpl.id == QStringLiteral("free")) {
            return tmpl;
        }
    }

    return m_templates.isEmpty() ? ComposeTemplate{} : m_templates.first();
}
