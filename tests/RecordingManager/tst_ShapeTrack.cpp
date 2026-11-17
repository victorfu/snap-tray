#include <QtTest>
#include <QPainter>
#include "recording/RecordingFrameFilter.h"
#include "recording/RecordingProject.h"
using namespace SnapTray;
class TestShapeTrack : public QObject {
    Q_OBJECT
private slots:
    void kindsRoundTripAndDrawDeterministically();
    void contentTracksZoomAndScreenStaysFixed();
    void masksNeverLeakAndBlurIncludesShapes();
};
void TestShapeTrack::kindsRoundTripAndDrawDeterministically()
{
    PreviewEditState state;
    for (int kind = 0; kind <= ShapeParameters::Step; ++kind) {
        ShapeSegment segment;
        segment.shape.kind = ShapeParameters::Kind(kind);
        segment.shape.from = {40, 30}; segment.shape.to = {140, 90};
        segment.shape.width = 12; segment.shape.color = Qt::cyan;
        segment.fadeInMs = segment.fadeOutMs = 100;
        segment.startMs = 100; segment.endMs = 1000;
        state.shapes.segments = {segment};
        const auto restored = RecordingRecipe::editState(RecordingRecipe::toJson(state, {}));
        QVERIFY(restored); QVERIFY(restored->shapes == state.shapes);
        QImage blank(200, 120, QImage::Format_RGB32); blank.fill(Qt::black);
        auto a = blank, b = blank;
        state.shapes.paint(a, 500, ShapeSegment::Content, blank.rect(), blank.size());
        state.shapes.paint(b, 500, ShapeSegment::Content, blank.rect(), blank.size());
        QCOMPARE(a, b); QVERIFY(a != blank);
        auto hidden = blank;
        state.shapes.paint(hidden, 1000, ShapeSegment::Content, blank.rect(), blank.size());
        QCOMPARE(hidden, blank);
        QCOMPARE(segment.opacityAt(150), 0.5);
    }
    QVERIFY(ShapeTrack::fromJson({}).isEmpty());
    QVERIFY(!ShapeParameters::fromJson({{"kind", 100}}));
}
void TestShapeTrack::contentTracksZoomAndScreenStaysFixed()
{
    PreviewEditState state;
    state.crop = QRect(20, 10, 160, 100);
    state.zoom.setSegments({{1000, 3000, 2.0, QPointF(100, 60), true}});
    ShapeSegment segment;
    segment.shape.kind = ShapeParameters::Rectangle;
    segment.shape.from = {70, 45}; segment.shape.to = {130, 75};
    segment.shape.width = 24; segment.shape.color = Qt::cyan;
    segment.fadeInMs = segment.fadeOutMs = 0;
    state.shapes.segments = {segment};
    const TimelineMap map({{0, 4000, 1.0}});
    QImage original(state.crop.size(), QImage::Format_RGB32); original.fill(Qt::black);
    auto rendered = original;
    recordingFrameFilter(state, QSize(200, 120), map)(rendered, 1500, 1600);
    // Viewport [60,35,80,50]; source corners map to (20,20),(140,80).
    QVERIFY(rendered.pixelColor(20, 40).green() > 150);
    QVERIFY(rendered.pixelColor(140, 40).green() > 150);
    QCOMPARE(rendered.pixelColor(80, 50), QColor(Qt::black));
    segment.space = ShapeSegment::Screen;
    segment.shape.from = {0.2, 0.2}; segment.shape.to = {0.8, 0.8};
    state.shapes.segments = {segment};
    auto first = original, zoomed = original;
    const auto filter = recordingFrameFilter(state, QSize(200, 120), map);
    filter(first, 0, 100); filter(zoomed, 1500, 1600);
    QCOMPARE(first, zoomed);
}
void TestShapeTrack::masksNeverLeakAndBlurIncludesShapes()
{
    PreviewEditState state;
    const QSize size(160, 100);
    state.redaction.masks.push_back({QRect(QPoint(), size), 0, -1, RedactionMask::Style::Solid});
    ShapeSegment shape;
    shape.shape.kind = ShapeParameters::Highlight;
    shape.shape.from = {0, 0}; shape.shape.to = {160, 100};
    shape.shape.color = Qt::cyan; shape.fadeInMs = shape.fadeOutMs = 0;
    state.shapes.segments.push_back(shape);
    state.appearance.enabled = true;
    state.appearance.background = RecordingAppearance::Background::BlurredVideo;
    state.appearance.padding = RecordingAppearance::Amount::Large;
    const auto filter = recordingFrameFilter(state, size, TimelineMap({{0, 1000, 1.0}}));
    QImage a(size, QImage::Format_RGB32), b(size, QImage::Format_RGB32);
    a.fill(Qt::red); b.fill(Qt::green);
    filter(a, 500, 600); filter(b, 500, 600);
    QCOMPARE(a, b); // Every secret source pixel is masked before either path.
    QImage masked(size, QImage::Format_RGB32); masked.fill(Qt::black);
    state.shapes.paint(masked, 500, ShapeSegment::Content, masked.rect(), size);
    RecordingAppearanceRenderer appearance(state.appearance, size);
    const auto expected = appearance.compose(masked, RecordingAppearanceRenderer::blurSourceFor(masked));
    QCOMPARE(a, expected);
}
QTEST_MAIN(TestShapeTrack)
#include "tst_ShapeTrack.moc"
