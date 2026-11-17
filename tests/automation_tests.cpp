#include <QtTest>
#include <QTemporaryDir>
#include <QFile>
#include "automation.h"
using namespace Automation;
namespace {
QJsonObject snapshot(bool ac=true,int charge=1,int percent=60){
    QJsonObject f;for(auto id:profileIds())f[id]=QJsonObject{{"available",true},{"value",id=="its"?0:1},{"enabled",true}};
    f["charge"]=QJsonObject{{"available",true},{"value",charge}};return {{"ok",true},{"ac",ac},{"batteryPercent",percent},{"features",f}};
}
void profile(Engine &e,const char *key,const QJsonObject &actions){e.config["enabled"]=true;e.config[key]=QJsonObject{{"name",key},{"actions",actions}};}
}
class AutomationTests:public QObject {
    Q_OBJECT
private slots:
    void delayedSupplyDoesNotEraseLaterManualIntent(){
        Engine e;profile(e,"dc",{{"brightness",25}});e.observe(snapshot(true));
        e.supplyChanged(false);e.manual("brightness");
        QVERIFY(e.observe(snapshot(false)).isEmpty());QVERIFY(e.overrides.contains("brightness"));
        e.supplyChanged(true);QVERIFY(!e.overrides.contains("brightness"));
        e.observe(snapshot(true));e.manual("brightness");e.supplyChanged(false);
        QCOMPARE(e.observe(snapshot(false)).size(),1);
    }
    void rapidSupplyRoundTripStillStartsANewContext(){
        Engine e;profile(e,"ac",{{"brightness",80}});QCOMPARE(e.observe(snapshot(true)).size(),1);
        e.manual("brightness");e.supplyChanged(false);e.supplyChanged(true);
        QCOMPARE(e.observe(snapshot(true)).size(),1);QVERIFY(e.observe(snapshot(true)).isEmpty());
    }
    void cancellationSurvivesAnInflightStartAndRestart(){
        Engine e;QString error;auto actions=e.startTopUp(snapshot(),&error);
        auto t=e.config["topUp"].toObject();t["cancelRequested"]=true;e.config["topUp"]=t;
        e.complete(actions[0],{{"ok",true}});QVERIFY(e.config["topUp"].toObject()["cancelRequested"].toBool());
        actions=e.observe(snapshot(true,0));QCOMPARE(actions.size(),1);QCOMPARE(actions[0].reason,QString("topUpRestore"));
    }
    void transitionOnlyAndManualOverride(){
        Engine e;profile(e,"ac",{{"its",3},{"brightness",75}});profile(e,"dc",{{"its",1}});
        auto a=e.observe(snapshot());QCOMPARE(a.size(),2);QCOMPARE(a[0].id,QString("its"));QCOMPARE(a[0].expected,0);
        QVERIFY(e.observe(snapshot()).isEmpty());e.manual("its");QVERIFY(e.overrides.contains("its"));QVERIFY(e.observe(snapshot()).isEmpty());
        a=e.observe(snapshot(false));QCOMPARE(a.size(),1);QVERIFY(e.overrides.isEmpty());QCOMPARE(a[0].value,1);
        QCOMPARE(e.observe(snapshot()).size(),2);
    }
    void unsupportedAndStaleDoNotWrite(){
        Engine e;profile(e,"ac",{{"its",3},{"brightness",80}});auto s=snapshot();s["stale"]=true;QVERIFY(e.observe(s).isEmpty());QVERIFY(!e.lastAc);
        s=snapshot();auto f=s["features"].toObject();f["its"]=QJsonObject{{"available",false},{"value",QJsonValue::Null}};s["features"]=f;auto a=e.observe(s);QCOMPARE(a.size(),1);QCOMPARE(a[0].id,QString("brightness"));
    }
    void topUpCompletesAndRestoresConditionally(){
        Engine e;QString error;auto a=e.startTopUp(snapshot(),&error);QCOMPARE(a.size(),1);QCOMPARE(a[0].expected,1);QCOMPARE(a[0].value,0);QVERIFY(e.hasTask());
        e.complete(a[0],{{"ok",true}});QCOMPARE(e.config["topUp"].toObject()["phase"].toString(),QString("charging"));
        QVERIFY(e.observe(snapshot(true,0,99)).isEmpty());a=e.observe(snapshot(true,0,100));QCOMPARE(a.size(),1);QCOMPARE(a[0].value,1);QCOMPARE(a[0].expected,0);
        e.complete(a[0],{{"ok",true}});QVERIFY(!e.hasTask());
    }
    void unplugCancelAndExternalChange(){
        Engine e;QString error;auto a=e.startTopUp(snapshot(),&error);e.complete(a[0],{{"ok",true}});a=e.observe(snapshot(false,0));QCOMPARE(a.size(),1);QCOMPARE(a[0].reason,QString("topUpRestore"));
        e.complete(a[0],{{"ok",false},{"conflict",true}});QVERIFY(!e.hasTask());
        a=e.startTopUp(snapshot(),&error);e.complete(a[0],{{"ok",true}});QVERIFY(e.observe(snapshot(true,2)).isEmpty());QVERIFY(!e.hasTask());
        a=e.startTopUp(snapshot(),&error);e.manual("charge");QVERIFY(e.hasTask()); // Request is not proof of a successful manual write.
    }
    void failedRestoreRequiresRetry(){
        Engine e;QString error;auto a=e.startTopUp(snapshot(),&error);e.complete(a[0],{{"ok",true}});a=e.cancelTopUp(snapshot(true,0));e.complete(a[0],{{"ok",false},{"error","test failure"}});
        QVERIFY(e.hasTask());QCOMPARE(e.config["topUp"].toObject()["phase"].toString(),QString("attention"));
        QVERIFY(e.observe(snapshot(false,0)).isEmpty());QCOMPARE(e.cancelTopUp(snapshot(false,0)).size(),1);
    }
    void staleCacheStillAttemptsConditionalRestore(){
        Engine e;QString error;auto a=e.startTopUp(snapshot(),&error);e.complete(a[0],{{"ok",true}});
        auto stale=snapshot(true,0);stale["stale"]=true;a=e.cancelTopUp(stale);QCOMPARE(a.size(),1);QCOMPARE(a[0].expected,0);QCOMPARE(a[0].value,1);
        e.complete(a[0],{{"ok",false},{"conflict",true}});QVERIFY(!e.hasTask());
    }
    void crashRecoveryRestoresWithoutResuming(){
        Engine original;QString error;original.startTopUp(snapshot(),&error);Engine restored;restored.config=original.config;
        auto a=restored.observe(snapshot(true,0));QCOMPARE(a.size(),1);QCOMPARE(a[0].value,1);QCOMPARE(a[0].expected,0);
        Engine unchanged;unchanged.config=original.config;QVERIFY(unchanged.observe(snapshot()).isEmpty());QVERIFY(!unchanged.hasTask());
        Engine delayed;delayed.config=original.config;auto missing=snapshot(true,0);auto f=missing["features"].toObject();f.remove("charge");missing["features"]=f;QVERIFY(delayed.observe(missing).isEmpty());QVERIFY(delayed.recovery);QCOMPARE(delayed.observe(snapshot(true,0)).size(),1);
    }
    void acCapabilityAndDisabled(){
        Engine e;profile(e,"dc",{{"its",4},{"brightness",50}});auto s=snapshot(false);auto f=s["features"].toObject();f["its"]=QJsonObject{{"available",true},{"value",0},{"requiresAc",4}};f["brightness"]=QJsonObject{{"available",true},{"value",1},{"enabled",false}};s["features"]=f;QVERIFY(e.observe(s).isEmpty());
    }
    void versionedStoreRejectsBadValues(){
        QTemporaryDir dir(QDir::currentPath()+"/automation-store-XXXXXX");QString path=dir.filePath("config.json"),error;QVERIFY2(save(path,defaults(),&error),qPrintable(error));QCOMPARE(load(path),defaults());
        auto invalid=defaults();invalid["schema"]=2;QVERIFY(!save(path,invalid,&error));invalid=defaults();invalid["ac"]=QJsonObject{{"name","bad"},{"actions",QJsonObject{{"ioctl",3}}}};QVERIFY(!validConfig(invalid));
        invalid["ac"]=QJsonObject{{"name","bad"},{"actions",QJsonObject{{"brightness",75.5}}}};QVERIFY(!validConfig(invalid));
        QFile f(path);QVERIFY(f.open(QIODevice::WriteOnly));f.write("broken");f.close();error.clear();QCOMPARE(load(path,&error),defaults());QVERIFY(!error.isEmpty());
    }
};
QTEST_GUILESS_MAIN(AutomationTests)
#include "automation_tests.moc"
