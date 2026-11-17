#include "automation.h"
#include "protocol.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QFile>
#include <QDir>
#include <QFileInfo>
#include <QDateTime>
#include <cmath>
namespace Automation {
QStringList profileIds(){return {"its","power","brightness","backlight"};}
QJsonObject defaults(){
    const QJsonObject ac{{"name","接电场景"},{"actions",QJsonObject{}}};
    const QJsonObject dc{{"name","电池场景"},{"actions",QJsonObject{}}};
    return {{"schema",1},{"enabled",false},{"ac",ac},{"dc",dc}};
}
bool validConfig(const QJsonObject &c,QString *error){
    auto fail=[&](const QString &s){if(error)*error=s;return false;};
    if(c["schema"].toInt(-1)!=1||!c["enabled"].isBool())return fail("不支持的配置版本或启用状态");
    for(const auto key:{"ac","dc"}){
        if(!c[key].isObject())return fail("缺少接电或电池场景");auto p=c[key].toObject();
        if(!p["name"].isString()||p["name"].toString().size()>80||!p["actions"].isObject())return fail("场景名称或动作格式错误");
        auto a=p["actions"].toObject();for(auto i=a.begin();i!=a.end();++i){double n=i.value().toDouble(-9999);if(!i.value().isDouble()||!std::isfinite(n)||n!=std::floor(n)||!profileIds().contains(i.key())||n<0||n>100||!Protocol::allowedRequest(i.key(),int(n)))return fail("场景包含不支持的设置或数值："+i.key());}
    }
    if(c.contains("topUp")){
        if(!c["topUp"].isObject())return fail("充满任务格式错误");auto t=c["topUp"].toObject();
        if(!t.isEmpty()&&((t["original"].toInt(-1)!=1&&t["original"].toInt(-1)!=2)||!QStringList{"switching","charging","restoring","attention"}.contains(t["phase"].toString())||!t["started"].isString()))return fail("充满任务数据无效");
    }
    return true;
}
bool save(const QString &path,const QJsonObject &c,QString *error){
    if(!validConfig(c,error))return false;QDir().mkpath(QFileInfo(path).absolutePath());QSaveFile f(path);
    auto bytes=QJsonDocument(c).toJson();if(!f.open(QIODevice::WriteOnly)||f.write(bytes)!=bytes.size()||!f.commit()){if(error)*error=f.errorString();return false;}return true;
}
QJsonObject load(const QString &path,QString *error){
    if(!QFile::exists(path))return defaults();QFile f(path);if(!f.open(QIODevice::ReadOnly)||f.size()>1024*1024){if(error)*error="无法读取场景配置";return defaults();}
    auto d=QJsonDocument::fromJson(f.readAll());if(!d.isObject()||!validConfig(d.object(),error)){if(error&&error->isEmpty())*error="场景配置 JSON 无效";return defaults();}return d.object();
}
namespace {
std::optional<int> value(const QJsonObject &s,const QString &id){auto f=s["features"].toObject()[id].toObject();if(s["stale"].toBool()||!f["available"].toBool()||f["stale"].toBool()||!f["value"].isDouble())return {};return f["value"].toInt();}
bool fresh(const QJsonObject &s){return !s["stale"].toBool()&&s["features"].isObject()&&s["ac"].isBool();}
}
QList<Action> Engine::restore(const QJsonObject &s){
    auto current=value(s,"charge");auto t=config["topUp"].toObject();if(t.isEmpty())return {};
    // A stale cache is not evidence that restoration is unnecessary. The
    // conditional worker re-reads under the write lock before changing anything.
    if(current&&*current!=0){config.remove("topUp");return {};}
    t["phase"]="restoring";config["topUp"]=t;return {{"charge",t["original"].toInt(),0,"topUpRestore"}};
}
QList<Action> Engine::observe(const QJsonObject &s){
    if(!fresh(s))return {};QList<Action> actions;
    if(hasTask()){
        auto t=config["topUp"].toObject();auto c=value(s,"charge");
        if(recovery){if(!c)return {};recovery=false;return restore(s);} // Crash/restart: restore, never silently resume.
        if(c&&*c!=0&&t["phase"].toString()!="switching"){config.remove("topUp");}
        else if(t["phase"].toString()=="attention"){} // Explicit retry only after failure.
        else if(t["cancelRequested"].toBool()||!s["ac"].toBool()||(s["batteryPercent"].isDouble()&&s["batteryPercent"].toDouble()>=100))actions=restore(s);
    }else recovery=false;
    bool ac=s["ac"].toBool();supplyChanged(ac);if(lastAc&&*lastAc==ac&&!needsSupplyEvaluation())return actions;lastAc=ac;appliedSupplyEpoch=supplyEpoch;
    if(!config["enabled"].toBool())return actions;
    auto a=config[ac?"ac":"dc"].toObject()["actions"].toObject();lastEvaluation={};
    for(const auto &id:profileIds())if(a.contains(id)&&!overrides.contains(id)){
        auto v=value(s,id);auto f=s["features"].toObject()[id].toObject();int target=a[id].toInt();
        bool allowed=f["enabled"].toBool(true);if(f["requiresAc"].toInt(-999)==target&&!ac)allowed=false;
        if(!f["options"].toArray().isEmpty()){bool offered=false;for(auto opt:f["options"].toArray())if(opt.toObject()["value"].toInt()==target)offered=true;allowed=allowed&&offered;}
        const QString outcome=!v||!allowed?"skipped":*v==target?"unchanged":"queued";
        lastEvaluation[id]=QJsonObject{{"target",target},{"outcome",outcome},{"reason",outcome=="skipped"?f["error"].toString("当前能力、电源或父开关不允许"):QString()}};
        if(v&&allowed&&*v!=target)actions.append({id,target,*v,ac?"acProfile":"dcProfile"});
    }return actions;
}
QList<Action> Engine::startTopUp(const QJsonObject &s,QString *error){
    auto c=value(s,"charge");if(!fresh(s)||!s["ac"].toBool()||!c||*c==0||hasTask()||!s["batteryPercent"].isDouble()||s["batteryPercent"].toDouble()>=100){if(error)*error="请在接电、未充满且养护或快充模式可用时开始；先处理现有任务。";return {};}
    config["topUp"]=QJsonObject{{"original",*c},{"phase","switching"},{"started",QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)}};recovery=false;
    return {{"charge",0,*c,"topUpStart"}};
}
QList<Action> Engine::cancelTopUp(const QJsonObject &s){return restore(s);}
void Engine::complete(const Action &a,const QJsonObject &r){
    if(!a.reason.startsWith("topUp"))return;auto t=config["topUp"].toObject();if(t.isEmpty())return;
    if(r["conflict"].toBool()){config.remove("topUp");return;}
    if(r["ok"].toBool()){if(a.reason=="topUpRestore")config.remove("topUp");else {t["phase"]="charging";config["topUp"]=t;}}
    else {t["phase"]="attention";t["error"]=r["error"];config["topUp"]=t;}
}
void Engine::manual(const QString &id){overrides.insert(id);}
}
