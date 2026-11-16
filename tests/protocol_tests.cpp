#include <QtTest>
#include "protocol.h"
class ProtocolTests:public QObject{
    Q_OBJECT
private slots:
    void hardwareVolumeReadback(){
        QVERIFY(Protocol::nearestVolumeStep(-12.72,-12.5,-63.5,0,0.5));
        QVERIFY(!Protocol::nearestVolumeStep(-12.72,-12,-63.5,0,0.5));
        QVERIFY(!Protocol::nearestVolumeStep(-12.72,-12.7,-63.5,0,0.5));
        QVERIFY(!Protocol::nearestVolumeStep(-12.72,-12.5,-63.5,0,0));
        QVERIFY(!Protocol::nearestVolumeStep(-64,-63.5,-63.5,0,0.5));
        QVERIFY(!Protocol::nearestVolumeStep(qQNaN(),-12.5,-63.5,0,0.5));
    }
    void presenceRangesAndRequests(){
        QCOMPARE(Protocol::presenceMeters(1,true),QList<double>({0.42,0.6,1.2}));
        QCOMPARE(Protocol::presenceMeters(2,true),QList<double>({0.55,1.0,1.5}));
        QCOMPARE(Protocol::presenceMeters(2,false),QList<double>({0.9,1.2,1.5}));
        QVERIFY(Protocol::presenceMeters(4,true).isEmpty());
        for(const auto &id:QStringList{"backlightRemember","fnCtrlSwap","capsOsd","numOsd","touchpad","presenceLogin","presenceLock","presenceVideo","loginAuto","lockAuto","cameraAssist"}){
            QVERIFY(Protocol::allowedRequest(id,0));QVERIFY(Protocol::allowedRequest(id,1));
            QVERIFY(!Protocol::allowedRequest(id,-1));QVERIFY(!Protocol::allowedRequest(id,2));
        }
        for(const auto &id:QStringList{"loginDistance","lockDistance","lockTimer"}){
            QVERIFY(Protocol::allowedRequest(id,2));QVERIFY(!Protocol::allowedRequest(id,3));QVERIFY(!Protocol::allowedRequest(id,-1));
        }
    }
    void firmwareOffsetLimits(){
        QVERIFY(Protocol::validOffset(200,0,200,1));QVERIFY(!Protocol::validOffset(201,0,200,1));
        QVERIFY(Protocol::validOffset(400,0,400,1));QVERIFY(!Protocol::validOffset(401,0,400,1));
        QVERIFY(!Protocol::validOffset(101,0,200,5));QVERIFY(Protocol::validOffset(100,0,200,5));
        QVERIFY(!Protocol::validOffset(0,0,200,0));QVERIFY(!Protocol::validOffset(0,5,1,1));
    }
    void realMachineCapabilities(){
        QCOMPARE(Protocol::chargeOptions(0x860200),QList<int>({0,1,2}));
        QCOMPARE(*Protocol::chargeMode(0x860200),0);
        QCOMPARE(*Protocol::backlightLevels(7),3);
        QVERIFY(!Protocol::backlightLevels(0));QVERIFY(!Protocol::backlightLevels(UINT32_MAX));
    }
    void mutualExclusionAndOrder(){
        auto conserve=Protocol::chargeCommands(0x860200,1);QCOMPARE(conserve.size(),2);QCOMPARE(conserve[0].token,8u);QCOMPARE(conserve[1].token,3u);
        auto rapid=Protocol::chargeCommands(0x860200,2);QCOMPARE(rapid.size(),2);QCOMPARE(rapid[0].token,5u);QCOMPARE(rapid[1].token,7u);
        for(const auto &c:rapid)QCOMPARE(c.bytes,1);
        auto extended=Protocol::chargeCommands(0x864200,2);QCOMPARE(extended.size(),3);QCOMPARE(extended[1].token,15u);
    }
    void unsupportedAndUnknownDoNotBecomeOff(){
        QVERIFY(!Protocol::chargeMode(UINT32_MAX));QVERIFY(!Protocol::chargeMode(0x860224));
        QVERIFY(Protocol::chargeCommands(0x20000,1).isEmpty());QVERIFY(Protocol::chargeCommands(UINT32_MAX,0).isEmpty());
        QVERIFY(!Protocol::backlightValue(0,3));QVERIFY(!Protocol::backlightValue(0x10009,3));
        QVERIFY(!Protocol::backlightCommand(2,3));QVERIFY(!Protocol::backlightCommand(0,0));
    }
    void backlightGeneration(){
        QCOMPARE(Protocol::backlightCommand(1,1)->token,0x10013u);
        QCOMPARE(Protocol::backlightCommand(2,2)->token,0x20023u);
        QCOMPARE(Protocol::backlightCommand(3,3)->token,0x30033u);
        QCOMPARE(*Protocol::backlightValue(0x10007,3),3);
        QCOMPARE(*Protocol::backlightValue(7,3),0);
    }
    void whitelist(){
        QVERIFY(Protocol::allowedRequest("its",3));QVERIFY(Protocol::allowedRequest("its",4));QCOMPARE(*Protocol::itsCommand(4),172);QVERIFY(!Protocol::allowedRequest("its",5));
        QVERIFY(Protocol::allowedRequest("gpu",2));QVERIFY(!Protocol::allowedRequest("gpu",3));QVERIFY(Protocol::allowedRequest("flip",1));QVERIFY(!Protocol::allowedRequest("flip",2));
        QVERIFY(!Protocol::allowedRequest("ocCore",2001));QVERIFY(!Protocol::allowedRequest("ocMemory",-1));
        QVERIFY(!Protocol::allowedRequest("ioctl",2));QVERIFY(!Protocol::allowedRequest("charge",3));
        QVERIFY(!Protocol::allowedRequest("volume",101));QVERIFY(!Protocol::allowedRequest("brightness",-1));
        QVERIFY(!Protocol::allowedRequest("fnlock",2));QVERIFY(!Protocol::allowedRequest("refresh",0));
        QCOMPARE(*Protocol::itsCommand(0),163);QCOMPARE(*Protocol::itsCommand(1),164);QCOMPARE(*Protocol::itsCommand(3),165);
    }
};
QTEST_GUILESS_MAIN(ProtocolTests)
#include "protocol_tests.moc"
