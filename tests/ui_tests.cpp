#include <QtTest>
#include "mode_slider.h"
#include "ui_update.h"
#include "window_widgets.h"

namespace {
QJsonObject modeState(int value,const QList<int> &modes={0,1,3,4}){QJsonArray options;for(int m:modes)options.append(QJsonObject{{"value",m},{"label",QString::number(m)}});return {{"available",true},{"value",value},{"options",options}};}
QPoint stop(const ModeSlider &slider,int index){return {qRound(28+(slider.width()-56)*index/3.),51};}
}
class UiTests:public QObject {
    Q_OBJECT
private slots:
    void failedSnapshotRetainsEvidenceAndBlocksWrites(){
        QJsonObject source{{"timestamp","2026-11-16T01:00:00Z"},{"features",QJsonObject{{"volume",modeState(3)},{"micMute",QJsonObject{{"available",false},{"error","offline"}}}}},{"_pending",QJsonObject{{"volume",1}}}};
        QVERIFY(Ui::writable(source,"volume"));
        auto failed=Ui::expiredSnapshot(source,"timeout","2026-11-16T01:01:00Z");
        QVERIFY(failed["stale"].toBool());QCOMPARE(failed["timestamp"],source["timestamp"]);QVERIFY(!failed.contains("_pending"));
        auto volume=failed["features"].toObject()["volume"].toObject();QCOMPARE(volume["value"].toInt(),3);QVERIFY(volume["available"].toBool());QVERIFY(volume["stale"].toBool());
        QVERIFY(!Ui::writable(failed,"volume"));QCOMPARE(failed["features"].toObject()["micMute"].toObject()["error"].toString(),QString("offline"));
        QVERIFY(Ui::writable(source,"volume"));QVERIFY(!Ui::writable(source,"micMute"));QVERIFY(!Ui::writable(source,"missing"));
    }
    void initTestCase(){qApp->setStyle("Fusion");int id=QFontDatabase::addApplicationFont("C:/Windows/Fonts/msyh.ttc");auto families=QFontDatabase::applicationFontFamilies(id);if(!families.isEmpty())qApp->setFont(QFont(families.first(),9));}
    void stopMappingAndReleaseOnly(){
        ModeSlider slider;slider.resize(360,112);slider.setState(modeState(0),true);slider.show();QSignalSpy requests(&slider,&ModeSlider::modeRequested);
        QCOMPARE(slider.indexForMode(1),0);QCOMPARE(slider.indexForMode(0),1);QCOMPARE(slider.indexForMode(3),2);QCOMPARE(slider.indexForMode(4),3);
        QTest::mousePress(&slider,Qt::LeftButton,{},stop(slider,0));QTest::mouseMove(&slider,stop(slider,3));QCOMPARE(requests.size(),0);QTest::mouseRelease(&slider,Qt::LeftButton,{},stop(slider,3));QCOMPARE(requests.size(),1);QCOMPARE(requests.first().first().toInt(),4);
    }
    void keyboardAndAcBoundary(){
        ModeSlider slider;slider.resize(360,112);slider.setState(modeState(0),true);slider.show();QSignalSpy requests(&slider,&ModeSlider::modeRequested);
        QTest::keyPress(&slider,Qt::Key_End);QCOMPARE(requests.size(),0);QTest::keyRelease(&slider,Qt::Key_End);QCOMPARE(requests.size(),1);QCOMPARE(requests.takeFirst().first().toInt(),4);
        slider.setState(modeState(4),true);QTest::keyClick(&slider,Qt::Key_Left);QCOMPARE(requests.takeFirst().first().toInt(),3);
        slider.setState(modeState(3),true);QTest::keyClick(&slider,Qt::Key_Left);QCOMPARE(requests.takeFirst().first().toInt(),0);
        slider.setState(modeState(0),false);QTest::keyClick(&slider,Qt::Key_End);QVERIFY(requests.isEmpty());QCOMPARE(slider.mode(),0);
        slider.setState(modeState(0,{0,1,3}),true);QCOMPARE(slider.indexForMode(4),-1);QTest::keyClick(&slider,Qt::Key_End);QCOMPARE(requests.takeFirst().first().toInt(),3);
    }
    void stableSnapshotsAndHiddenAnimation(){
        ModeSlider slider;slider.resize(360,112);slider.setState(modeState(4),true);slider.show();QTest::qWait(50);QSignalSpy requests(&slider,&ModeSlider::modeRequested);auto count=slider.transitionCount();
        for(int i=0;i<100;++i)slider.setState(modeState(4),true);QCOMPARE(slider.transitionCount(),count);QVERIFY(requests.isEmpty());
        if(Ui::motionEnabled())QVERIFY(slider.particleAnimationActive());slider.hide();QVERIFY(!slider.particleAnimationActive());slider.show();QTest::qWait(20);if(Ui::motionEnabled())QVERIFY(slider.particleAnimationActive());
    }
    void comboModelAndPopupSurviveRefresh(){
        QComboBox combo;auto state=modeState(0);Ui::combo(&combo,state,0);QSignalSpy changes(&combo,&QComboBox::currentIndexChanged);QSignalSpy removals(combo.model(),&QAbstractItemModel::rowsRemoved);QSignalSpy resets(combo.model(),&QAbstractItemModel::modelReset);
        for(int i=0;i<100;++i)Ui::combo(&combo,state,0);QCOMPARE(changes.size(),0);QCOMPARE(removals.size(),0);QCOMPARE(resets.size(),0);
        combo.show();combo.showPopup();QTest::qWait(20);QVERIFY(combo.view()->isVisible());Ui::combo(&combo,state,3);QVERIFY(combo.view()->isVisible());QCOMPARE(combo.currentData().toInt(),0);combo.hidePopup();Ui::combo(&combo,state,3);QCOMPARE(combo.currentData().toInt(),3);QCOMPARE(removals.size(),0);QCOMPARE(changes.size(),0);
    }
    void activeSliderEditIsPreserved(){
        SmoothSlider slider(Qt::Horizontal);slider.setRange(0,100);auto state=QJsonObject{{"available",true}};Ui::slider(&slider,state,30);QCOMPARE(slider.value(),30);QSignalSpy changes(&slider,&QSlider::valueChanged);
        slider.setSliderDown(true);slider.setValue(70);Ui::slider(&slider,state,35);QCOMPARE(slider.value(),70);slider.setSliderDown(false);Ui::slider(&slider,state,35);QCOMPARE(slider.value(),35);QCOMPARE(changes.size(),1);
    }
    void renderReferenceModes(){
        auto destination=qEnvironmentVariable("THINKVANTAGE_UI_ARTIFACT_DIR");if(destination.isEmpty())return;QDir().mkpath(destination);
        QFrame frame;frame.setFocusPolicy(Qt::StrongFocus);frame.setStyleSheet("QFrame {background:white;border:1px solid #e3e3e7;border-radius:16px;}");auto layout=new QVBoxLayout(&frame);layout->setContentsMargins(10,8,10,8);auto slider=new ModeSlider;layout->addWidget(slider);frame.resize(430,116);frame.show();frame.setFocus();
        auto capture=[&](const QString &name){QPixmap image(frame.size()*2);image.setDevicePixelRatio(2);image.fill(Qt::transparent);frame.render(&image);return image.save(QDir(destination).filePath(name));};
        QList<QPixmap> previews;
        for(int mode:{1,0,3,4}){slider->setState(modeState(mode),true);QTest::qWait(280);const auto name=QString("mode-%1.png").arg(mode);QVERIFY(capture(name));previews.append(QPixmap(QDir(destination).filePath(name)));}
        QPixmap palette(previews[0].width()*2,previews[0].height()*2);palette.fill(Qt::white);{QPainter p(&palette);for(int i=0;i<previews.size();++i)p.drawPixmap((i%2)*previews[i].width(),(i/2)*previews[i].height(),previews[i]);}QVERIFY(palette.save(QDir(destination).filePath("palette.png")));
        for(int i=0;i<28;++i){QTest::qWait(50);QVERIFY(capture(QString("ultra-%1.png").arg(i,2,10,QChar('0'))));}
    }
};
QTEST_MAIN(UiTests)
#include "ui_tests.moc"
