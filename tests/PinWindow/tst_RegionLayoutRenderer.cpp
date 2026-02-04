#include <QtTest/QtTest>
#include <QRect>

#include "pinwindow/RegionLayoutRenderer.h"
#include "pinwindow/RegionLayoutManager.h"

class TestRegionLayoutRenderer : public QObject
{
    Q_OBJECT

private slots:
    // =========================================================================
    // Button Rect Tests
    // =========================================================================

    void testConfirmButtonRect() {
        QRect canvasBounds(0, 0, 400, 300);
        QRect confirmRect = RegionLayoutRenderer::confirmButtonRect(canvasBounds);

        // Button should be valid
        QVERIFY(confirmRect.isValid());
        QVERIFY(confirmRect.width() > 0);
        QVERIFY(confirmRect.height() > 0);

        // Button should be within canvas bounds (with some margin)
        QVERIFY(confirmRect.left() >= 0);
        QVERIFY(confirmRect.right() <= canvasBounds.right());
        QVERIFY(confirmRect.top() >= 0);
        QVERIFY(confirmRect.bottom() <= canvasBounds.bottom());

        // Button should have correct dimensions from constants
        QCOMPARE(confirmRect.width(), LayoutModeConstants::kButtonWidth);
        QCOMPARE(confirmRect.height(), LayoutModeConstants::kButtonHeight);
    }

    void testCancelButtonRect() {
        QRect canvasBounds(0, 0, 400, 300);
        QRect cancelRect = RegionLayoutRenderer::cancelButtonRect(canvasBounds);

        // Button should be valid
        QVERIFY(cancelRect.isValid());
        QVERIFY(cancelRect.width() > 0);
        QVERIFY(cancelRect.height() > 0);

        // Button should have correct dimensions
        QCOMPARE(cancelRect.width(), LayoutModeConstants::kButtonWidth);
        QCOMPARE(cancelRect.height(), LayoutModeConstants::kButtonHeight);
    }

    void testButtonsDontOverlap() {
        QRect canvasBounds(0, 0, 400, 300);
        QRect confirmRect = RegionLayoutRenderer::confirmButtonRect(canvasBounds);
        QRect cancelRect = RegionLayoutRenderer::cancelButtonRect(canvasBounds);

        // Buttons should not overlap
        QVERIFY(!confirmRect.intersects(cancelRect));

        // Cancel button should be to the right of confirm button
        QVERIFY(cancelRect.left() > confirmRect.right());
    }

    void testButtonsAreCentered() {
        QRect canvasBounds(0, 0, 400, 300);
        QRect confirmRect = RegionLayoutRenderer::confirmButtonRect(canvasBounds);
        QRect cancelRect = RegionLayoutRenderer::cancelButtonRect(canvasBounds);

        // Calculate combined button area
        int totalWidth = cancelRect.right() - confirmRect.left();
        int combinedCenter = (confirmRect.left() + cancelRect.right()) / 2;

        // Should be roughly centered (within 5 pixels)
        int canvasCenter = canvasBounds.center().x();
        QVERIFY(qAbs(combinedCenter - canvasCenter) <= 5);
    }

    void testButtonsAtBottomOfCanvas() {
        QRect canvasBounds(0, 0, 400, 300);
        QRect confirmRect = RegionLayoutRenderer::confirmButtonRect(canvasBounds);
        QRect cancelRect = RegionLayoutRenderer::cancelButtonRect(canvasBounds);

        // Buttons should be near the bottom
        int margin = LayoutModeConstants::kButtonMargin + LayoutModeConstants::kButtonHeight;
        QVERIFY(confirmRect.bottom() <= canvasBounds.bottom());
        QVERIFY(cancelRect.bottom() <= canvasBounds.bottom());
        QVERIFY(confirmRect.top() > canvasBounds.center().y());  // In lower half
    }

    void testButtonRectsWithSmallCanvas() {
        // Test with a canvas that's barely big enough for buttons
        int minWidth = LayoutModeConstants::kButtonWidth * 2 +
                       LayoutModeConstants::kButtonSpacing +
                       LayoutModeConstants::kButtonMargin * 2;
        int minHeight = LayoutModeConstants::kButtonHeight +
                        LayoutModeConstants::kButtonMargin * 2;

        QRect smallCanvas(0, 0, minWidth, minHeight);
        QRect confirmRect = RegionLayoutRenderer::confirmButtonRect(smallCanvas);
        QRect cancelRect = RegionLayoutRenderer::cancelButtonRect(smallCanvas);

        // Buttons should still be valid
        QVERIFY(confirmRect.isValid());
        QVERIFY(cancelRect.isValid());
    }

    void testButtonRectsWithOffsetCanvas() {
        // Test with canvas not at origin
        QRect offsetCanvas(100, 200, 400, 300);
        QRect confirmRect = RegionLayoutRenderer::confirmButtonRect(offsetCanvas);
        QRect cancelRect = RegionLayoutRenderer::cancelButtonRect(offsetCanvas);

        // Buttons should still be within canvas bounds
        QVERIFY(confirmRect.left() >= offsetCanvas.left());
        QVERIFY(cancelRect.right() <= offsetCanvas.right());
    }

    // =========================================================================
    // Constants Tests
    // =========================================================================

    void testConstantsAreReasonable() {
        // Verify constants have reasonable values
        QVERIFY(LayoutModeConstants::kMinRegionSize > 0);
        QVERIFY(LayoutModeConstants::kHandleSize > 0);
        QVERIFY(LayoutModeConstants::kBorderWidth > 0);
        QVERIFY(LayoutModeConstants::kSelectedBorderWidth >= LayoutModeConstants::kBorderWidth);
        QVERIFY(LayoutModeConstants::kButtonWidth > 0);
        QVERIFY(LayoutModeConstants::kButtonHeight > 0);
        QVERIFY(LayoutModeConstants::kMaxCanvasSize > 0);
        QVERIFY(LayoutModeConstants::kMaxRegionCount > 0);
    }
};

QTEST_MAIN(TestRegionLayoutRenderer)
#include "tst_RegionLayoutRenderer.moc"
