#pragma once
#include <QJsonObject>
#include <QString>
#include <optional>
namespace Backend {
QJsonObject read();
QJsonObject apply(const QString &id, int value, std::optional<int> expectedPrevious = std::nullopt, std::optional<bool> expectedAc = std::nullopt, const QString &expectedTarget = {});
QJsonObject confirmRefresh(const QString &operationToken);
bool isAdministrator();
bool isWindows11();
bool relaunchElevated();
}
