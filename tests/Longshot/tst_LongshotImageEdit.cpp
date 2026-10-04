#include "longshot/LongshotImageEdit.h"
#include <QtTest>
using namespace SnapTray::Longshot;
static QImage rows(int count) {
    QImage image(5,count,QImage::Format_ARGB32);
    for (int y=0;y<count;++y) for (int x=0;x<5;++x) image.setPixel(x,y,qRgba(y%256,x,42,128));
    return image;
}
class tst_LongshotImageEdit : public QObject {
    Q_OBJECT
private slots:
    void editsRetainExactRowsAndMapping() {
        const auto original=rows(10);
        LongshotImageEdit edit(original);
        QVERIFY(edit.removeRows(2,4));
        QCOMPARE(edit.height(),8);
        QVERIFY(!edit.outputRow(2)); QCOMPARE(*edit.outputRow(6),4);
        QVERIFY(edit.keepRows(1,6));
        const auto result=edit.image();
        QCOMPARE(result.height(),5);
        const int expected[]={1,4,5,6,7};
        for(int y=0;y<5;++y) {
            QCOMPARE(result.pixel(3,y),original.pixel(3,expected[y]));
            QCOMPARE(*edit.outputRow(expected[y]),y);
        }
        QVERIFY(!edit.outputRow(0)); QVERIFY(!edit.outputRow(8));
        const auto warnings=edit.mapRows(2,7); // warning starts on a deleted row, but survives below it
        QCOMPARE(warnings.size(),size_t(1));
        QCOMPARE(warnings[0].originalBegin,4); QCOMPARE(warnings[0].outputBegin,1); QCOMPARE(warnings[0].count,3);
        QVERIFY(edit.undo()); QCOMPARE(edit.height(),8);
        QVERIFY(edit.undo()); QCOMPARE(edit.image(),original);
        QVERIFY(edit.redo()); QCOMPARE(edit.height(),8);
        QVERIFY(edit.keepRows(0,3)); QVERIFY(!edit.canRedo());
    }
    void invalidAndEmptyEditsAreNoops() {
        LongshotImageEdit edit(rows(10));
        QVERIFY(!edit.removeRows(0,10)); QVERIFY(!edit.keepRows(0,0));
        QVERIFY(!edit.keepRows(-1,5)); QVERIFY(!edit.keepRows(0,11));
        QVERIFY(!edit.removeRows(6,3)); QVERIFY(!edit.keepRows(0,10));
        QVERIFY(!edit.canUndo()); QVERIFY(!edit.edited()); QCOMPARE(edit.height(),10);
    }
    void historyBoundNeverDiscardsOriginal() {
        const auto original=rows(200);
        LongshotImageEdit edit(original);
        for(int i=0;i<80;++i) QVERIFY(edit.removeRows(0,1));
        int undos=0; while(edit.undo()) ++undos;
        QCOMPARE(undos,32); QCOMPARE(edit.height(),152);
        QVERIFY(edit.reset()); QCOMPARE(edit.image(),original);
        QVERIFY(edit.undo()); QCOMPARE(edit.height(),152);
    }
    void externalStrideIsNotCopiedPastDestination() {
        QByteArray bytes(64*8,'\0');
        QImage external(reinterpret_cast<uchar*>(bytes.data()),5,8,64,QImage::Format_ARGB32);
        for(int y=0;y<8;++y) external.setPixel(0,y,qRgb(y,11,12));
        LongshotImageEdit edit(external);
        QVERIFY(edit.removeRows(2,6));
        auto result=edit.image();
        QCOMPARE(result.size(),QSize(5,4));
        QCOMPARE(result.pixel(0,2),external.pixel(0,6));
        QCOMPARE(result.pixel(0,3),external.pixel(0,7));
    }
};
QTEST_GUILESS_MAIN(tst_LongshotImageEdit)
#include "tst_LongshotImageEdit.moc"
