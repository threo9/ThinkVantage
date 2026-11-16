#pragma once
#include <QJsonObject>
#include <QString>
namespace Vendor {
QJsonObject probe();
QJsonObject ocProbe();
void setOc(int enabled,int core,int memory);
bool gpuBusy();
QString root();
}
