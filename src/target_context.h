#pragma once
#include <QJsonObject>
#include <QString>
#include <QStringList>
namespace TargetContext {
inline QString key(const QString &id,const QJsonObject &state){
    if(id=="volume"||id=="speakerMute"||id=="micMute")return state["endpointId"].toString();
    if(id=="brightness")return state["instanceName"].toString();
    if(id=="refresh"){auto source=state["displaySource"].toString(),identity=state["displayIdentity"].toString();return source.isEmpty()||identity.isEmpty()?QString():source+"|"+identity;}
    return {};
}
inline bool required(const QString &id){return QStringList{"volume","speakerMute","micMute","brightness","refresh"}.contains(id);}
inline bool matches(const QString &id,const QString &expected,const QJsonObject &current){return !required(id)||(!expected.isEmpty()&&expected==key(id,current));}
struct GenerationFence {quint64 current=0;void invalidate(){++current;}bool accepts(quint64 started)const{return started==current;}};
}
