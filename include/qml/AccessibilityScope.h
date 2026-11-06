#pragma once

#include <QQuickItem>
#include <QtQml/qqmlregistration.h>

namespace SnapTray {

// A visual subtree that stays painted while a modal surface owns accessibility.
class AccessibilityScope : public QQuickItem
{
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(bool accessibilityBlocked READ accessibilityBlocked WRITE setAccessibilityBlocked
               NOTIFY accessibilityBlockedChanged)
public:
    explicit AccessibilityScope(QQuickItem* parent = nullptr);
    bool accessibilityBlocked() const { return m_blocked; }
    void setAccessibilityBlocked(bool blocked);
signals:
    void accessibilityBlockedChanged();
private:
    bool m_blocked = false;
};

} // namespace SnapTray
