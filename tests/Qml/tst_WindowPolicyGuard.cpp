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
    void testRecordingPreviewActionsLeaveQmlHandlerBeforeFileSideEffects();
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

void tst_WindowPolicyGuard::testRecordingPreviewActionsLeaveQmlHandlerBeforeFileSideEffects()
{
    QFile file(QDir(QStringLiteral(WINDOW_POLICY_GUARD_SOURCE_ROOT))
                   .filePath(QStringLiteral("MainApplication.cpp")));
    QVERIFY2(file.open(QIODevice::ReadOnly | QIODevice::Text),
             qPrintable(file.fileName() + QStringLiteral(": failed to open")));

    const QString content = QString::fromUtf8(file.readAll());
    const QRegularExpression saveConnection(
        QStringLiteral("connect\\s*\\(\\s*m_previewBackend\\s*,\\s*"
                       "&RecordingPreviewBackend::saveRequested\\s*,\\s*"
                       "this\\s*,\\s*&MainApplication::onPreviewSaveRequested\\s*,\\s*"
                       "Qt::QueuedConnection\\s*\\)"));
    const QRegularExpression discardConnection(
        QStringLiteral("connect\\s*\\(\\s*m_previewBackend\\s*,\\s*"
                       "&RecordingPreviewBackend::discardRequested\\s*,\\s*"
                       "this\\s*,\\s*&MainApplication::onPreviewDiscardRequested\\s*,\\s*"
                       "Qt::QueuedConnection\\s*\\)"));

    QVERIFY2(saveConnection.match(content).hasMatch(),
             "Recording preview save must be queued so the native save dialog cannot "
             "process preview deletion while a QML handler is still running");
    QVERIFY2(discardConnection.match(content).hasMatch(),
             "Recording preview discard should follow the same queued teardown boundary");
}

QTEST_MAIN(tst_WindowPolicyGuard)
#include "tst_WindowPolicyGuard.moc"
