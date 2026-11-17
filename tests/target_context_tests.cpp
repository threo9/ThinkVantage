#include <QtTest>
#include "target_context.h"
class TargetContextTests:public QObject {
    Q_OBJECT
private slots:
    void queuedAudioCannotFollowNewDefault(){
        QJsonObject a{{"endpointId","endpoint-a"}},b{{"endpointId","endpoint-b"}};
        for(auto id:{"volume","speakerMute","micMute"}){
            auto target=TargetContext::key(id,a);QVERIFY(TargetContext::matches(id,target,a));QVERIFY(!TargetContext::matches(id,target,b));QVERIFY(!TargetContext::matches(id,{},a));
        }
    }
    void monitorIdentityAndSourceAreBothBound(){
        QJsonObject a{{"displaySource","display-1"},{"displayIdentity","panel-a"}};auto b=a;b["displayIdentity"]="panel-b";
        auto target=TargetContext::key("refresh",a);QVERIFY(TargetContext::matches("refresh",target,a));QVERIFY(!TargetContext::matches("refresh",target,b));
        b=a;b["displaySource"]="display-2";QVERIFY(!TargetContext::matches("refresh",target,b));QVERIFY(TargetContext::key("refresh",{}).isEmpty());
        QVERIFY(!TargetContext::matches("brightness","panel-a",QJsonObject{{"instanceName","panel-b"}}));
    }
    void oldProbeCannotPublishAfterEvent(){
        TargetContext::GenerationFence fence;auto started=fence.current;QVERIFY(fence.accepts(started));fence.invalidate();QVERIFY(!fence.accepts(started));
        auto retry=fence.current;QVERIFY(fence.accepts(retry));fence.invalidate();fence.invalidate();QVERIFY(!fence.accepts(retry));
    }
};
QTEST_GUILESS_MAIN(TargetContextTests)
#include "target_context_tests.moc"
