#pragma once
#include <QJsonObject>
#include <QString>
namespace Vendor {
QJsonObject probe();
QJsonObject ocProbe();
void setOc(int enabled,int core,int memory);
QJsonObject gpuProcesses();
QJsonObject gpuProcessesRaw();
bool gpuBusy();
QString root();
}
