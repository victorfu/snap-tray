#pragma once

#include <QObject>
#include <QVariantList>

class ShortcutHintsViewModel : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList hints READ hints CONSTANT)

public:
    explicit ShortcutHintsViewModel(QObject* parent = nullptr);

    QVariantList hints() const { return m_hints; }

private:
    QVariantList m_hints;
};
