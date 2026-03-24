#include <QtTest/QtTest>

#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QRegularExpression>

class tst_WindowPolicyGuard : public QObject
{
    Q_OBJECT

private slots:
    void testNoDirectQQuickViewConstructionOutsideOverlayManager();
};

void tst_WindowPolicyGuard::testNoDirectQQuickViewConstructionOutsideOverlayManager()
{
    const QRegularExpression pattern(QStringLiteral("\\bnew\\s+QQuickView\\s*\\("));
    QStringList offenders;

    QDirIterator it(QStringLiteral(WINDOW_POLICY_GUARD_SOURCE_ROOT),
                    QStringList() << QStringLiteral("*.cpp")
                                  << QStringLiteral("*.mm")
                                  << QStringLiteral("*.h"),
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        const QString normalizedPath = QDir::fromNativeSeparators(path);
        if (normalizedPath.endsWith(QStringLiteral("/src/qml/QmlOverlayManager.mm"))) {
            continue;
        }

        QFile file(path);
        QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
                 qPrintable(path + QStringLiteral(": failed to open")));
        const QString content = QString::fromUtf8(file.readAll());
        if (pattern.match(content).hasMatch()) {
            offenders << normalizedPath;
        }
    }

    QVERIFY2(offenders.isEmpty(), qPrintable(offenders.join(QLatin1Char('\n'))));
}

QTEST_MAIN(tst_WindowPolicyGuard)
#include "tst_WindowPolicyGuard.moc"
