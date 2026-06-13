#pragma once

#include <QObject>

class TestCaptureShortcutHintsOverlay : public QObject
{
    Q_OBJECT

private slots:
    void testRuntimeTranslationLookup();
    void testTranslationSourcesPresentInAllCatalogs();
    void testOverlayAndQmlHintSourcesStayInSync();
    void testRowCount();
    void testRowCountWithoutMagnifierHints();
    void testLayoutMetrics();
    void testLayoutMetricsWithoutMagnifierHints();
    void testPanelRectWithinViewport();
    void testRepaintRectCoversPanelEdges();
};
