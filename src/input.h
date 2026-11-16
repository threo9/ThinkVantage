#pragma once
#include <QJsonObject>
#include <QStringList>
namespace Input {
QStringList ids();
bool owns(const QString &id);
QJsonObject feature(const QString &id);
void set(const QString &id,int value);
}
