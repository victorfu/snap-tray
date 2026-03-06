#include "qml/QmlRecordingRegionSelector.h"
#include "qml/QmlOverlayManager.h"

#include <QCoreApplication>
#include <QCursor>
#include <QDebug>
#include <QEvent>
#include <QGuiApplication>
#include <QQuickItem>
#include <QQuickView>
#include <QScreen>
#include <QTimer>
#include <QWindow>

#ifdef Q_OS_MACOS
#import <Cocoa/Cocoa.h>
#endif

namespace SnapTray {

namespace {

QString translateRecordingRegionSelector(const char* sourceText)
{
    return QCoreApplication::translate("RecordingRegionSelector", sourceText);
}

} // namespace

QmlRecordingRegionSelector::QmlRecordingRegionSelector(QObject* parent)
    : QObject(parent)
{
    connect(qApp, &QGuiApplication::screenRemoved, this, [this](QScreen* screen) {
        if (m_currentScreen == screen || m_currentScreen.isNull()) {
            qWarning() << "QmlRecordingRegionSelector: Screen disconnected, closing";
            emit cancelled();
            close();
        }
    });
}

QmlRecordingRegionSelector::~QmlRecordingRegionSelector()
{
    close();
}

void QmlRecordingRegionSelector::ensureView()
{
    if (m_view)
        return;

    m_view = new QQuickView(QmlOverlayManager::instance().engine(), nullptr);
    m_view->setFlags(Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::Window);
    m_view->setColor(Qt::transparent);
    m_view->setResizeMode(QQuickView::SizeRootObjectToView);
    m_view->installEventFilter(this);
    m_view->setSource(QUrl(QStringLiteral("qrc:/SnapTrayQml/recording/RecordingRegionSelector.qml")));

    if (m_view->status() == QQuickView::Error) {
        for (const auto& error : m_view->errors())
            qWarning() << "RecordingRegionSelector QML error:" << error.toString();
    }

    m_rootItem = m_view->rootObject();
    if (!m_rootItem) {
        qWarning() << "QmlRecordingRegionSelector: rootObject is null";
        return;
    }

    auto connectOrWarn = [this](const char* signal, const char* slot, const char* description) {
        if (!connect(m_rootItem, signal, this, slot))
            qWarning() << "QmlRecordingRegionSelector: failed to connect" << description;
    };
    connectOrWarn(SIGNAL(confirmSelection(double,double,double,double)),
                  SLOT(onConfirmSelection(double,double,double,double)),
                  "confirmSelection");
    connectOrWarn(SIGNAL(cancelSelection(bool,double,double,double,double)),
                  SLOT(onCancelSelection(bool,double,double,double,double)),
                  "cancelSelection");

    updateTextProperties();
    syncSelectionToQml();

    if (!m_windowGeometry.isEmpty())
        m_view->setGeometry(m_windowGeometry);
}

void QmlRecordingRegionSelector::applyPlatformWindowFlags()
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView* view = reinterpret_cast<NSView*>(m_view->winId());
    if (!view)
        return;

    NSWindow* window = [view window];
    if (!window)
        return;

    [window setLevel:kCGScreenSaverWindowLevel];
    [window setHidesOnDeactivate:NO];

    NSUInteger mask = [window styleMask];
    mask &= ~NSWindowStyleMaskResizable;
    [window setStyleMask:mask];
#endif
}

void QmlRecordingRegionSelector::updateTextProperties()
{
    if (!m_rootItem)
        return;

    m_rootItem->setProperty("idleHelpText",
                            translateRecordingRegionSelector("Click and drag to select recording region"));
    m_rootItem->setProperty("draggingHelpText",
                            translateRecordingRegionSelector("Release to confirm selection"));
    m_rootItem->setProperty("selectedHelpText",
                            translateRecordingRegionSelector("Press Enter or click Start Recording, Escape to cancel"));
    m_rootItem->setProperty("startButtonTooltip",
                            translateRecordingRegionSelector("Start Recording (Enter)"));
    m_rootItem->setProperty("cancelButtonTooltip",
                            translateRecordingRegionSelector("Cancel (Esc)"));
    m_rootItem->setProperty("dimensionFormat",
                            translateRecordingRegionSelector("%1 x %2"));
    m_rootItem->setProperty("minimumSelectionSize", 10);
}

void QmlRecordingRegionSelector::syncSelectionToQml()
{
    if (!m_rootItem)
        return;

    m_rootItem->setProperty("selectionX", m_selectionRect.x());
    m_rootItem->setProperty("selectionY", m_selectionRect.y());
    m_rootItem->setProperty("selectionWidth", m_selectionRect.width());
    m_rootItem->setProperty("selectionHeight", m_selectionRect.height());
    m_rootItem->setProperty("selectionComplete", m_selectionComplete);
}

void QmlRecordingRegionSelector::setGeometry(const QRect& geometry)
{
    m_windowGeometry = geometry;
    if (m_view)
        m_view->setGeometry(geometry);
}

void QmlRecordingRegionSelector::initializeWithRegion(QScreen* screen, const QRect& region)
{
    m_currentScreen = screen;
    if (m_currentScreen.isNull()) {
        qWarning() << "QmlRecordingRegionSelector: No valid screen available";
        emit cancelled();
        QTimer::singleShot(0, this, [this]() { close(); });
        return;
    }

    const QRect screenGeom = m_currentScreen->geometry();
    if (m_windowGeometry.isEmpty())
        m_windowGeometry = screenGeom;

    m_selectionRect = region.translated(-screenGeom.topLeft());
    m_selectionComplete = true;

    ensureView();
    syncSelectionToQml();
}

void QmlRecordingRegionSelector::show()
{
    ensureView();
    if (!m_view)
        return;

    if (!m_windowGeometry.isEmpty())
        m_view->setGeometry(m_windowGeometry);
    syncSelectionToQml();
    if (m_rootItem) {
        const QPoint localCursorPos = m_view->mapFromGlobal(QCursor::pos());
        m_rootItem->setProperty("mousePosition", QPointF(localCursorPos));
    }

    m_view->show();
    applyPlatformWindowFlags();
    raiseAboveMenuBar();
    m_view->raise();
    m_view->requestActivate();
    if (m_rootItem)
        m_rootItem->forceActiveFocus();

    QTimer::singleShot(0, this, [this]() {
        if (!m_view)
            return;
        m_view->raise();
        m_view->requestActivate();
        if (m_rootItem)
            m_rootItem->forceActiveFocus();
    });
}

void QmlRecordingRegionSelector::close()
{
    if (!m_view)
        return;

    m_view->removeEventFilter(this);
    m_view->close();
    m_view->deleteLater();
    m_view = nullptr;
    m_rootItem = nullptr;
}

bool QmlRecordingRegionSelector::isVisible() const
{
    return m_view && m_view->isVisible();
}

void QmlRecordingRegionSelector::activateWindow()
{
    if (!m_view)
        return;

    m_view->requestActivate();
    if (m_rootItem)
        m_rootItem->forceActiveFocus();
}

void QmlRecordingRegionSelector::raise()
{
    if (m_view)
        m_view->raise();
}

void QmlRecordingRegionSelector::raiseAboveMenuBar()
{
#ifdef Q_OS_MACOS
    if (!m_view)
        return;

    NSView* view = reinterpret_cast<NSView*>(m_view->winId());
    if (!view)
        return;

    NSWindow* window = [view window];
    if (window)
        [window setLevel:kCGScreenSaverWindowLevel];
#endif
}

bool QmlRecordingRegionSelector::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view && event && event->type() == QEvent::FocusOut) {
        raiseAboveMenuBar();
        if (m_view) {
            m_view->raise();
            m_view->requestActivate();
        }
        if (m_rootItem)
            m_rootItem->forceActiveFocus();
    }

    return QObject::eventFilter(watched, event);
}

QRect QmlRecordingRegionSelector::toGlobalRect(double x, double y, double width, double height) const
{
    QRect localRect(qRound(x), qRound(y), qRound(width), qRound(height));

    if (!m_currentScreen.isNull())
        localRect.translate(m_currentScreen->geometry().topLeft());

    return localRect;
}

void QmlRecordingRegionSelector::onConfirmSelection(double x, double y,
                                                    double width, double height)
{
    if (m_currentScreen.isNull()) {
        qWarning() << "QmlRecordingRegionSelector: Screen invalid, cannot finish selection";
        emit cancelled();
        close();
        return;
    }

    emit regionSelected(toGlobalRect(x, y, width, height), m_currentScreen.data());
    close();
}

void QmlRecordingRegionSelector::onCancelSelection(bool hasSelection, double x, double y,
                                                   double width, double height)
{
    if (hasSelection && !m_currentScreen.isNull()) {
        emit cancelledWithRegion(toGlobalRect(x, y, width, height), m_currentScreen.data());
    } else {
        emit cancelled();
    }
    close();
}

} // namespace SnapTray
