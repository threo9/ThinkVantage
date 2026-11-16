#pragma once
#include <QJsonObject>
#include <QString>

// Pure transformations, shared by the isolated providers and offline tests.
namespace VendorData {
QJsonObject mergeBattery(const QJsonObject &fallback, const QJsonObject &vendor);
QJsonObject parseGpuProcesses(const QString &text);
QJsonObject parseGpuWorkerOutput(const QByteArray &output);
}
