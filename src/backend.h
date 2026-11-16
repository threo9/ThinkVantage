#pragma once
#include <QJsonObject>
#include <QString>
namespace Backend {
QJsonObject read();
QJsonObject apply(const QString &id, int value);
QJsonObject confirmRefresh(int previous);
bool isAdministrator();
bool isWindows11();
bool relaunchElevated();
}
