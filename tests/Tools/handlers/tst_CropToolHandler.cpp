#include <QtTest/QtTest>

#include <QImage>
#include <QPainter>

#include "tools/ToolContext.h"
#include "tools/handlers/CropToolHandler.h"

class TestCropToolHandler : public QObject
{
    Q_OBJECT

private slots:
    void init();
    void releaseKeepsDraftWithoutCropping();
    void draftIsInSourcePixelsNotToolPixels();
    void clickWithoutDragKeepsPreviousDraft();
    void dragInsideMovesDraft();
    void dragHandleResizesDraft();
    void narrowDraftStillMovesFromItsMiddle();
    void newDragOutsideReplacesDraft();
    void escapeCancelsDragThenDraft();
    void commitAppliesOnceThroughCallback();
    void refusedCommitKeepsDraft();
    void commitDuringDragIsIgnored();
    void deactivateDiscardsDraft();
    void replacedSourceDiscardsDraft();
    void resizedViewKeepsDraft();
    void aspectAppliesToExistingDraft();
    void nudgeNeedsDraft();
    void cursorFollowsHandleAndOrientation();
    void draftChangedIsReported();
    void previewDimsOutsideDraftOnly();

private:
    void drag(const QPointF& from, const QPointF& to);

    CropToolHandler m_handler;
    ToolContext m_ctx;
    QVector<QRect> m_committed;
    bool m_acceptCrop = true;
    int m_changes = 0;
};

void TestCropToolHandler::init()
{
    m_handler = CropToolHandler();
    m_committed.clear();
    m_acceptCrop = true;
    m_changes = 0;
    m_handler.setImageSize(QSize(200, 100));
    m_handler.setSource(QSize(200, 100), 1.0, 1);
    m_handler.setCropCallback([this](const QRect& rect) {
        m_committed.append(rect);
        return m_acceptCrop;
    });
    m_handler.setDraftChangedCallback([this]() { ++m_changes; });
    m_handler.onActivate(&m_ctx);
}

void TestCropToolHandler::drag(const QPointF& from, const QPointF& to)
{
    m_handler.onMousePressF(&m_ctx, from);
    m_handler.onMouseMoveF(&m_ctx, (from + to) / 2.0);
    m_handler.onMouseMoveF(&m_ctx, to);
    m_handler.onMouseReleaseF(&m_ctx, to);
}

void TestCropToolHandler::releaseKeepsDraftWithoutCropping()
{
    m_handler.onMousePressF(&m_ctx, QPointF(20, 10));
    QVERIFY(m_handler.isDrawing());
    m_handler.onMouseMoveF(&m_ctx, QPointF(120, 70));
    m_handler.onMouseReleaseF(&m_ctx, QPointF(120, 70));

    QVERIFY(!m_handler.isDrawing());
    QVERIFY(m_handler.hasDraft());
    QCOMPARE(m_handler.draftRect(), QRect(20, 10, 100, 60));
    QVERIFY2(m_committed.isEmpty(), "Releasing the mouse must not crop");
}

void TestCropToolHandler::draftIsInSourcePixelsNotToolPixels()
{
    // The view is zoomed to 2x of a 2x-DPR image: 400x200 source pixels are
    // shown in a 400x200 tool space that is itself 200x100 logical px at 100%.
    m_handler.setImageSize(QSize(400, 200));
    m_handler.setSource(QSize(400, 200), 2.0, 2);
    drag(QPointF(40, 20), QPointF(240, 140));
    QCOMPARE(m_handler.draftRect(), QRect(40, 20, 200, 120));

    // Same image shown at half size: the same source rect, not a smaller one.
    m_handler.setImageSize(QSize(100, 50));
    QCOMPARE(m_handler.draftRect(), QRect(40, 20, 200, 120));
    QCOMPARE(m_handler.draftToolRect(), QRectF(10, 5, 50, 30));
    m_handler.discardDraft();
    drag(QPointF(10, 5), QPointF(60, 35));
    QCOMPARE(m_handler.draftRect(), QRect(40, 20, 200, 120));
}

void TestCropToolHandler::clickWithoutDragKeepsPreviousDraft()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    const QRect draft = m_handler.draftRect();

    // A click outside the draft, with no drag, changes nothing.
    m_handler.onMousePressF(&m_ctx, QPointF(180, 90));
    m_handler.onMouseReleaseF(&m_ctx, QPointF(181, 90));
    QCOMPARE(m_handler.draftRect(), draft);

    // Neither does a click inside it.
    m_handler.onMousePressF(&m_ctx, QPointF(60, 40));
    m_handler.onMouseReleaseF(&m_ctx, QPointF(60, 40));
    QCOMPARE(m_handler.draftRect(), draft);
    QVERIFY(m_committed.isEmpty());
}

void TestCropToolHandler::dragInsideMovesDraft()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    drag(QPointF(70, 40), QPointF(100, 50));
    QCOMPARE(m_handler.draftRect(), QRect(50, 20, 100, 60));

    // It stops at the image edge and keeps its size.
    drag(QPointF(100, 50), QPointF(400, 400));
    QCOMPARE(m_handler.draftRect(), QRect(100, 40, 100, 60));
}

void TestCropToolHandler::dragHandleResizesDraft()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    // Right edge.
    drag(QPointF(120, 40), QPointF(160, 40));
    QCOMPARE(m_handler.draftRect(), QRect(20, 10, 140, 60));
    // Top-left corner.
    drag(QPointF(20, 10), QPointF(40, 30));
    QCOMPARE(m_handler.draftRect(), QRect(40, 30, 120, 40));
}

void TestCropToolHandler::narrowDraftStillMovesFromItsMiddle()
{
    // Five pixels wide: narrower than a handle's reach on either side.
    drag(QPointF(50, 50), QPointF(55, 90));
    QCOMPARE(m_handler.draftRect(), QRect(50, 50, 5, 40));

    drag(QPointF(52.5, 70), QPointF(92.5, 70));
    QCOMPARE(m_handler.draftRect(), QRect(90, 50, 5, 40));

    // Its edges still resize, grabbed from just outside.
    drag(QPointF(97, 70), QPointF(120, 70));
    QCOMPARE(m_handler.draftRect(), QRect(90, 50, 30, 40));
}

void TestCropToolHandler::newDragOutsideReplacesDraft()
{
    drag(QPointF(20, 10), QPointF(60, 40));
    drag(QPointF(100, 50), QPointF(180, 90));
    QCOMPARE(m_handler.draftRect(), QRect(100, 50, 80, 40));
}

void TestCropToolHandler::escapeCancelsDragThenDraft()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    const QRect draft = m_handler.draftRect();

    // Escape in the middle of a move puts the draft back where it was.
    m_handler.onMousePressF(&m_ctx, QPointF(70, 40));
    m_handler.onMouseMoveF(&m_ctx, QPointF(110, 60));
    QVERIFY(m_handler.draftRect() != draft);
    QVERIFY(m_handler.handleEscape(&m_ctx));
    QVERIFY(!m_handler.isDrawing());
    QCOMPARE(m_handler.draftRect(), draft);
    // The release that follows the cancelled drag does nothing.
    m_handler.onMouseReleaseF(&m_ctx, QPointF(110, 60));
    QCOMPARE(m_handler.draftRect(), draft);

    // A second Escape discards the draft, a third is not handled.
    QVERIFY(m_handler.handleEscape(&m_ctx));
    QVERIFY(!m_handler.hasDraft());
    QVERIFY(!m_handler.handleEscape(&m_ctx));
    QVERIFY(m_committed.isEmpty());
}

void TestCropToolHandler::commitAppliesOnceThroughCallback()
{
    QVERIFY(!m_handler.commitDraft());
    QVERIFY(m_committed.isEmpty());

    drag(QPointF(20, 10), QPointF(120, 70));
    QVERIFY(m_handler.commitDraft());
    QCOMPARE(m_committed.size(), 1);
    QCOMPARE(m_committed.first(), QRect(20, 10, 100, 60));
}

void TestCropToolHandler::refusedCommitKeepsDraft()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    m_acceptCrop = false;
    QVERIFY(!m_handler.commitDraft());
    QVERIFY(m_handler.hasDraft());
    QCOMPARE(m_handler.draftRect(), QRect(20, 10, 100, 60));
}

void TestCropToolHandler::commitDuringDragIsIgnored()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    m_handler.onMousePressF(&m_ctx, QPointF(70, 40));
    m_handler.onMouseMoveF(&m_ctx, QPointF(90, 50));
    QVERIFY(!m_handler.commitDraft());
    QVERIFY(m_committed.isEmpty());
}

void TestCropToolHandler::deactivateDiscardsDraft()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    m_handler.onDeactivate(&m_ctx);
    QVERIFY(!m_handler.hasDraft());
    m_handler.onActivate(&m_ctx);
    QVERIFY(!m_handler.hasDraft());
    QVERIFY(m_committed.isEmpty());
}

void TestCropToolHandler::replacedSourceDiscardsDraft()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    // Same image again: the draft stays.
    m_handler.setSource(QSize(200, 100), 1.0, 1);
    QVERIFY(m_handler.hasDraft());

    // Another image of the same size must not inherit the selection.
    m_handler.setSource(QSize(200, 100), 1.0, 7);
    QVERIFY(!m_handler.hasDraft());

    // Nor may a drag in progress survive the replacement.
    m_handler.onMousePressF(&m_ctx, QPointF(20, 10));
    m_handler.onMouseMoveF(&m_ctx, QPointF(120, 70));
    m_handler.setSource(QSize(200, 100), 1.0, 8);
    QVERIFY(!m_handler.isDrawing());
    m_handler.onMouseReleaseF(&m_ctx, QPointF(120, 70));
    QVERIFY(!m_handler.hasDraft());
}

void TestCropToolHandler::resizedViewKeepsDraft()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    m_handler.setImageSize(QSize(300, 150));   // zoomed to 150%
    QCOMPARE(m_handler.draftRect(), QRect(20, 10, 100, 60));
    QCOMPARE(m_handler.draftToolRect(), QRectF(30, 15, 150, 90));
}

void TestCropToolHandler::aspectAppliesToExistingDraft()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    m_handler.setAspect(CropToolHandler::Aspect::Square);
    QCOMPARE(m_handler.draftRect().width(), m_handler.draftRect().height());
    QVERIFY(QRect(0, 0, 200, 100).contains(m_handler.draftRect()));
    QCOMPARE(m_handler.aspect(), CropToolHandler::Aspect::Square);

    // New drags, started outside the draft, follow the preset as well.
    drag(QPointF(130, 10), QPointF(190, 20));
    QCOMPARE(m_handler.draftRect(), QRect(130, 10, 60, 60));
}

void TestCropToolHandler::nudgeNeedsDraft()
{
    QVERIFY(!m_handler.nudgeDraft(QPoint(1, 0)));
    drag(QPointF(20, 10), QPointF(120, 70));
    QVERIFY(m_handler.nudgeDraft(QPoint(1, 0)));
    QCOMPARE(m_handler.draftRect(), QRect(21, 10, 100, 60));
    QVERIFY(m_handler.nudgeDraft(QPoint(0, -10)));
    QCOMPARE(m_handler.draftRect(), QRect(21, 0, 100, 60));
    QVERIFY(m_committed.isEmpty());
}

void TestCropToolHandler::cursorFollowsHandleAndOrientation()
{
    QCOMPARE(m_handler.cursor().shape(), Qt::CrossCursor);
    drag(QPointF(20, 10), QPointF(120, 70));

    m_handler.onMouseMoveF(&m_ctx, QPointF(70, 40));
    QCOMPARE(m_handler.cursor().shape(), Qt::SizeAllCursor);
    m_handler.onMouseMoveF(&m_ctx, QPointF(120, 40));
    QCOMPARE(m_handler.cursor().shape(), Qt::SizeHorCursor);
    m_handler.onMouseMoveF(&m_ctx, QPointF(20, 10));
    QCOMPARE(m_handler.cursor().shape(), Qt::SizeFDiagCursor);
    m_handler.onMouseMoveF(&m_ctx, QPointF(190, 95));
    QCOMPARE(m_handler.cursor().shape(), Qt::CrossCursor);

    // Shown rotated by 90 degrees, the same handles point the other way.
    m_handler.setViewOrientation(90, false, false);
    m_handler.onMouseMoveF(&m_ctx, QPointF(120, 40));
    QCOMPARE(m_handler.cursor().shape(), Qt::SizeVerCursor);
    m_handler.onMouseMoveF(&m_ctx, QPointF(20, 10));
    QCOMPARE(m_handler.cursor().shape(), Qt::SizeBDiagCursor);

    // A mirror on top of the turn swaps the diagonals back.
    m_handler.setViewOrientation(90, true, false);
    QCOMPARE(m_handler.cursor().shape(), Qt::SizeFDiagCursor);
}

void TestCropToolHandler::draftChangedIsReported()
{
    drag(QPointF(20, 10), QPointF(120, 70));
    QVERIFY(m_changes > 0);

    m_changes = 0;
    m_handler.nudgeDraft(QPoint(1, 0));
    QCOMPARE(m_changes, 1);
    m_handler.setAspect(CropToolHandler::Aspect::Square);
    QCOMPARE(m_changes, 2);
    m_handler.discardDraft();
    QCOMPARE(m_changes, 3);
    // Nothing left to discard: no report.
    m_handler.discardDraft();
    QCOMPARE(m_changes, 3);
}

void TestCropToolHandler::previewDimsOutsideDraftOnly()
{
    QImage canvas(200, 100, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::white);
    {
        QPainter painter(&canvas);
        m_handler.drawPreview(painter);
    }
    QCOMPARE(canvas.pixelColor(5, 5), QColor(Qt::white));   // no draft, no overlay

    drag(QPointF(40, 20), QPointF(160, 80));
    {
        QPainter painter(&canvas);
        m_handler.drawPreview(painter);
    }
    QVERIFY(canvas.pixelColor(5, 5).red() < 200);            // dimmed outside
    // Clear inside, away from the grid lines and the size label.
    QCOMPARE(canvas.pixelColor(50, 65), QColor(Qt::white));
}

QTEST_MAIN(TestCropToolHandler)
#include "tst_CropToolHandler.moc"
