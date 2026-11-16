#include "vendor_data.h"
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>
#include <QTimeZone>
#include <cmath>
#include <limits>

namespace {
const QString batteryProvider = QStringLiteral("Lenovo IdeaBatteryAgent");
QJsonObject diagnostic(const QString &stage, const QString &error) {
    return {{"provider", batteryProvider}, {"stage", stage}, {"error", error}};
}
QString manufactureDate(const QJsonValue &value) {
    if (!value.isString()) return {};
    const QString text = value.toString();
    QDate date;
    // Old bridge builds use JavaScriptSerializer's /Date(milliseconds)/ form.
    static const QRegularExpression legacy(QStringLiteral("^/Date\\((-?[0-9]+)\\)/$"));
    auto match = legacy.match(text);
    if (match.hasMatch()) {
        bool ok = false;
        auto milliseconds = match.captured(1).toLongLong(&ok);
        if (ok) date = QDateTime::fromMSecsSinceEpoch(milliseconds, QTimeZone::UTC).date();
    } else if (text.size() == 10) {
        date = QDate::fromString(text, Qt::ISODate);
    } else {
        date = QDateTime::fromString(text, Qt::ISODate).date();
    }
    // OEM uses DateTimeOffset.MinValue for unknown dates. Do not display it.
    if (!date.isValid() || date.year() < 1980 || date.year() > 2107) return {};
    return date.toString(Qt::ISODate);
}
}

QJsonObject VendorData::mergeBattery(const QJsonObject &fallback, const QJsonObject &vendor) {
    QJsonObject result = fallback;
    const auto raw = vendor.value("battery").toObject();
    auto diagnostics = vendor.value("diagnostics").toArray();
    auto metadata = result.value("fieldMetadata").toObject();
    const QString observedAt = vendor.value("observedAt").toString();
    for (auto field = fallback.begin(); field != fallback.end(); ++field) {
        if (!field.value().isDouble() || metadata.contains(field.key())) continue;
        metadata[field.key()] = QJsonObject{{"provider", "Windows WMI"},
            {"observationKind", "wmiReport"}, {"observedAt", fallback.value("observedAt").toString()}};
    }
    bool copied = false;
    // Retain successful WMI fields when a vendor field is null or malformed.
    const auto number = [&](const char *source, const char *target, double divisor,
                            double low, double high, const char *unit) {
        const auto value = raw.value(QLatin1String(source));
        if (value.isUndefined() || value.isNull()) return;
        const double n = value.toDouble(std::numeric_limits<double>::quiet_NaN());
        if (!value.isDouble() || !std::isfinite(n) || std::trunc(n) != n || n < low || n > high) {
            diagnostics.append(diagnostic(QString("field:%1").arg(source), "原厂字段类型或范围无效"));
            return;
        }
        result[QLatin1String(target)] = n / divisor;
        metadata[QLatin1String(target)] = QJsonObject{{"provider", batteryProvider},
            {"sourceField", QLatin1String(source)}, {"unit", QLatin1String(unit)},
            {"observedAt", observedAt}, {"observationKind", "vendorReport"}};
        copied = true;
    };
    number("FullChargeCapacity", "fullWh", 1000, 1, 1000000, "Wh");
    number("DesignCapacity", "designWh", 1000, 1, 1000000, "Wh");
    number("RemainingCapacity", "currentWh", 1000, 0, 1000000, "Wh");
    number("Temperature", "temperatureC", 1, -50, 150, "C");
    number("CycleCount", "cycles", 1, 0, 65534, "count");
    number("Voltage", "voltageV", 1000, 1, 65534, "V");
    // The native signed current uses +65535 as its unavailable sentinel.
    if (raw.value("Current").toDouble() != 65535)
        number("Current", "currentMa", 1, -100000, 100000, "mA");
    number("DesignVoltage", "designVoltageV", 1000, 1, 65534, "V");
    number("ChargeStatus", "chargeStatus", 1, 0, 65534, "enum");
    number("BatteryHealth", "vendorHealthCode", 1, 0, 65534, "enum");
    // The same native slot is called WattageinW in BatteryAgent.GetBatteryStatus.
    // It is an unsigned magnitude, never a signed rate or wall-input reading.
    number("Wattage", "powerW", 1, 0, 65534, "W");
    number("AcWattage", "adapterWattageRaw", 1, 0, 65534, "unknown");
    const auto string = [&](const char *source, const char *target) {
        auto value = raw.value(QLatin1String(source));
        if (!value.isString() || value.toString().trimmed().isEmpty()) return;
        result[QLatin1String(target)] = value.toString().trimmed();
        metadata[QLatin1String(target)] = QJsonObject{{"provider", batteryProvider},
            {"sourceField", QLatin1String(source)}, {"observedAt", observedAt}};
        copied = true;
    };
    string("DeviceChemistry", "chemistry");
    string("ManufactureName", "manufacturer");
    string("AcAdapterStatus", "adapterStatus");
    if (!raw.value("ManufactureDate").isNull() && !raw.value("ManufactureDate").isUndefined()) {
        result["manufactureDateRaw"] = raw.value("ManufactureDate");
        auto date = manufactureDate(raw.value("ManufactureDate"));
        if (!date.isEmpty()) {
            result["manufactureDate"] = date;
            metadata["manufactureDate"] = QJsonObject{{"provider", batteryProvider},
                {"sourceField", "ManufactureDate"}, {"observedAt", observedAt}, {"unit", "date"}};
        }
    }
    if (vendor.value("storage80").isBool()) result["storage80"] = vendor.value("storage80");
    if (result.value("fullWh").toDouble() > 0 && result.value("designWh").toDouble() > 0) {
        result["healthExact"] = result.value("fullWh").toDouble() * 100 / result.value("designWh").toDouble();
        metadata["healthExact"] = QJsonObject{{"observationKind", "calculatedCapacityRatio"},
            {"inputs", QJsonArray{"fullWh", "designWh"}}, {"unit", "%"}};
    }
    const int status = result.value("chargeStatus").toInt(-1);
    result["powerDirection"] = status == 0 ? "idle" : status == 1 ? "charging" : status == 2 ? "discharging" : "unknown";
    // Preserve old diagnostic key while keeping stage/source in the new chain.
    for (const auto *key : {"error", "batteryError"}) {
        auto error = vendor.value(QLatin1String(key));
        if (!error.isString() || error.toString().isEmpty()) continue;
        if (!result.contains("vendorError")) result["vendorError"] = error;
        bool recorded = false;
        for (const auto &item : diagnostics)
            if (item.toObject().value("error") == error) { recorded = true; break; }
        if (!recorded) diagnostics.append(diagnostic(QLatin1String(key), error.toString()));
    }
    if (copied) result["provider"] = fallback.isEmpty() ? batteryProvider : "Windows WMI + Lenovo IdeaBatteryAgent";
    else if (!fallback.isEmpty() && !result.contains("provider")) result["provider"] = "Windows WMI";
    result["raw"] = raw;
    result["fieldMetadata"] = metadata;
    result["diagnostics"] = diagnostics;
    return result;
}

QJsonObject VendorData::parseGpuProcesses(const QString &text) {
    QJsonObject result{{"available", true}, {"provider", "Lenovo GPUInfo"}};
    QJsonArray processes;
    QSet<QString> seen;
    bool busy = false;
    static const QSet<QString> ignoredNames{"dwm.exe", "explorer.exe", "lenovovantage.exe", "thinkvantage.exe"};
    for (const auto &entry : text.split('|', Qt::SkipEmptyParts)) {
        // OEM layout is PID:name:icon. Icon payloads may themselves contain ':';
        // only the first two separators define process identity.
        const auto first = entry.indexOf(':');
        const auto second = first < 0 ? -1 : entry.indexOf(':', first + 1);
        bool validPid = false;
        const auto pidText = entry.left(first).trimmed();
        const qulonglong pid = pidText.toULongLong(&validPid);
        const auto name = second < 0 ? QString() : entry.mid(first + 1, second - first - 1).trimmed();
        static const QRegularExpression digits("^[0-9]+$");
        if (first <= 0 || second <= first + 1 || !validPid || !digits.match(pidText).hasMatch() ||
            pid == 0 || pid > std::numeric_limits<quint32>::max() || name.isEmpty() ||
            name.contains(QChar::Null) || name.contains('\n') || name.contains('\r')) {
            result["available"] = false;
            result["error"] = "原厂独显进程列表格式异常，无法确认占用状态";
            result["stage"] = "parse";
            result["processes"] = processes;
            // An invalid record must not turn into a confident empty/busy=false.
            result["busy"] = QJsonValue::Null;
            return result;
        }
        const auto identity = QString::number(pid) + ':' + name.toLower();
        if (seen.contains(identity)) continue;
        seen.insert(identity);
        const bool ignored = ignoredNames.contains(name.toLower());
        processes.append(QJsonObject{{"pid", double(pid)}, {"name", name}, {"ignored", ignored}});
        busy |= !ignored;
    }
    result["processes"] = processes;
    result["busy"] = busy;
    return result;
}

QJsonObject VendorData::parseGpuWorkerOutput(const QByteArray &output) {
    const auto invalid=[](const QString &error){return QJsonObject{{"available",false},
        {"provider","Lenovo GPUInfo"},{"stage","workerProtocol"},{"error",error},
        {"busy",QJsonValue::Null},{"processes",QJsonArray{}}};};
    constexpr auto marker="THINKVANTAGE_GPU_JSON=";
    const auto lines=output.split('\n');
    for(auto it=lines.crbegin();it!=lines.crend();++it){
        QByteArray line=*it;if(line.endsWith('\r'))line.chop(1);
        if(!line.startsWith(marker))continue;
        auto document=QJsonDocument::fromJson(line.mid(int(sizeof("THINKVANTAGE_GPU_JSON=")-1)));
        if(!document.isObject())return invalid("独显信息进程返回的标记结果不是有效 JSON");
        auto result=document.object();
        if(!result["available"].isBool()||!result["processes"].isArray()||
           (result["available"].toBool()&&!result["busy"].isBool())||
           (!result["available"].toBool()&&result["error"].toString().isEmpty()))
            return invalid("独显信息进程返回的结构无效");
        return result;
    }
    return invalid("独显信息进程未返回带标记的结果");
}
