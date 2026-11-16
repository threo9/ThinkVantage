#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QSet>
#include <optional>

namespace Automation {
struct Action { QString id; int value; int expected; QString reason; };
QStringList profileIds();
bool validConfig(const QJsonObject &config, QString *error=nullptr);
QJsonObject defaults();
bool save(const QString &path,const QJsonObject &config,QString *error=nullptr);
QJsonObject load(const QString &path,QString *error=nullptr);
// Pure state machine: all writes are returned as conditional actions. Persist
// task changes before executing them; report completion with complete().
class Engine {
public:
    QJsonObject config=defaults();
    bool recovery=true;
    QSet<QString> overrides;
    std::optional<bool> lastAc;
    QJsonObject lastEvaluation;
    QList<Action> observe(const QJsonObject &snapshot);
    QList<Action> startTopUp(const QJsonObject &snapshot,QString *error);
    QList<Action> cancelTopUp(const QJsonObject &snapshot);
    void complete(const Action &action,const QJsonObject &result);
    void manual(const QString &id);
    void reapply(){lastAc.reset();overrides.clear();}
    bool hasTask()const{return !config["topUp"].toObject().isEmpty();}
private:
    QList<Action> restore(const QJsonObject &snapshot);
};
}
