#include <QtTest>
#include "refresh_safety.h"
class RefreshSafetyTests:public QObject {
    Q_OBJECT
private slots:
    void ownedPreviewCanKeepOrRestore(){
        RefreshSafety::Mode mode{"monitor-a",1920,1200,32,120,0,0,0,0};
        QCOMPARE(RefreshSafety::decide("first","first",mode,mode,true),RefreshSafety::Decision::Keep);
        QCOMPARE(RefreshSafety::decide("first","first",mode,mode,false),RefreshSafety::Decision::Restore);
    }
    void newerRequestCannotBeRevertedByOldPreview(){
        RefreshSafety::Mode expected{"monitor-a",1920,1200,32,120,0,0,0,0};
        auto external=expected;external.hz=60;
        QCOMPARE(RefreshSafety::decide("first","first",expected,external,false),RefreshSafety::Decision::Superseded);
        QCOMPARE(RefreshSafety::decide("first","first",expected,external,true),RefreshSafety::Decision::Superseded);
        QCOMPARE(RefreshSafety::decide("second","first",expected,expected,false),RefreshSafety::Decision::InvalidToken);
    }
    void recycledDisplayOrChangedTopologyCannotBeReverted(){
        RefreshSafety::Mode expected{"monitor-a",1920,1200,32,120,0,0,0,0};
        auto current=expected;current.identity="monitor-b";
        QCOMPARE(RefreshSafety::decide("t","t",expected,current,false),RefreshSafety::Decision::Superseded);
        current=expected;current.width=2560;
        QCOMPARE(RefreshSafety::decide("t","t",expected,current,false),RefreshSafety::Decision::Superseded);
        current=expected;current.x=1920;
        QCOMPARE(RefreshSafety::decide("t","t",expected,current,false),RefreshSafety::Decision::Superseded);
        current=expected;current.orientation=1;
        QCOMPARE(RefreshSafety::decide("t","t",expected,current,false),RefreshSafety::Decision::Superseded);
        current=expected;current.flags=2;
        QCOMPARE(RefreshSafety::decide("t","t",expected,current,false),RefreshSafety::Decision::Superseded);
    }
    void unknownIdentityIsNeverSafeToRestore(){
        RefreshSafety::Mode unknown;
        QCOMPARE(RefreshSafety::decide("t","t",unknown,unknown,false),RefreshSafety::Decision::Superseded);
        QCOMPARE(RefreshSafety::decide("","",unknown,unknown,false),RefreshSafety::Decision::InvalidToken);
    }
};
QTEST_GUILESS_MAIN(RefreshSafetyTests)
#include "refresh_safety_tests.moc"
