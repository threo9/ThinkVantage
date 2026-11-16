#include <QtTest>
#include <QJsonArray>
#include "vendor_data.h"

class VendorTests : public QObject {
    Q_OBJECT
private slots:
    void partialBatteryRetainsFallbackAndErrorChain() {
        QJsonObject fallback{{"fullWh", 80.}, {"designWh", 85.}, {"cycles", 77}};
        QJsonObject vendor{{"batteryError", "optional DLL missing"},
            {"battery", QJsonObject{{"FullChargeCapacity", QJsonValue::Null},
                {"Temperature", 31}, {"RemainingCapacity", 45000}}},
            {"diagnostics", QJsonArray{QJsonObject{{"stage", "storage80"}, {"error", "export missing"}}}}};
        const auto result = VendorData::mergeBattery(fallback, vendor);
        QCOMPARE(result["fullWh"].toDouble(), 80.);
        QCOMPARE(result["currentWh"].toDouble(), 45.);
        QCOMPARE(result["cycles"].toInt(), 77);
        QCOMPARE(result["vendorError"].toString(), QString("optional DLL missing"));
        QCOMPARE(result["diagnostics"].toArray().size(), 2);
        QVERIFY(result["healthExact"].isDouble());
        QVERIFY(!result.contains("powerW"));
        QVERIFY(!result.contains("adapterWattageRaw"));
    }
    void explicitPowerAndAdapterSemantics() {
        auto result = VendorData::mergeBattery({}, {{"observedAt", "2026-11-16T01:00:00Z"},
            {"battery", QJsonObject{{"Wattage", 12}, {"AcWattage", 140},
                {"ChargeStatus", 2}, {"BatteryHealth", 0}, {"AcAdapterStatus", "OEM-special-state"}}}});
        QCOMPARE(result["powerW"].toDouble(), 12.);
        QCOMPARE(result["powerDirection"].toString(), QString("discharging"));
        QCOMPARE(result["adapterWattageRaw"].toDouble(), 140.);
        QCOMPARE(result["vendorHealthCode"].toInt(-1), 0);
        QVERIFY(!result.contains("healthExact"));
        QCOMPARE(result["adapterStatus"].toString(), QString("OEM-special-state"));
        auto metadata = result["fieldMetadata"].toObject();
        QCOMPARE(metadata["powerW"].toObject()["unit"].toString(), QString("W"));
        QCOMPARE(metadata["adapterWattageRaw"].toObject()["unit"].toString(), QString("unknown"));
        QCOMPARE(metadata["powerW"].toObject()["observedAt"].toString(), QString("2026-11-16T01:00:00Z"));
        result = VendorData::mergeBattery({}, {{"battery", QJsonObject{{"Wattage", 0}, {"ChargeStatus", 70}}}});
        QCOMPARE(result["powerW"].toDouble(-1), 0.);
        QCOMPARE(result["powerDirection"].toString(), QString("unknown"));
    }
    void invalidValuesDoNotReplaceGoodFallback() {
        const auto result = VendorData::mergeBattery({{"fullWh", 80.}},
            {{"battery", QJsonObject{{"FullChargeCapacity", -1}, {"Temperature", "31"},
                {"Current", 65535}, {"CycleCount", 65535}, {"Wattage", 65535}}}});
        QCOMPARE(result["fullWh"].toDouble(), 80.);
        QVERIFY(!result.contains("temperatureC"));
        QVERIFY(!result.contains("currentMa"));
        QVERIFY(!result.contains("cycles"));
        QVERIFY(!result.contains("powerW"));
        QVERIFY(result["diagnostics"].toArray().size() >= 4);
    }
    void manufactureDates() {
        const auto merge = [](const QJsonValue &date) {
            return VendorData::mergeBattery({}, {{"battery", QJsonObject{{"ManufactureDate", date}}}});
        };
        QCOMPARE(merge("2024-03-15")["manufactureDate"].toString(), QString("2024-03-15"));
        QCOMPARE(merge("/Date(1710460800000)/")["manufactureDate"].toString(), QString("2024-03-15"));
        for (auto date : {"unparseable OEM date", "2024-02-31", "0001-01-01", "/Date(-62135596800000)/"}) {
            const auto result = merge(date);
            QVERIFY(!result.contains("manufactureDate"));
            QCOMPARE(result["manufactureDateRaw"].toString(), QString::fromUtf8(date));
        }
        const auto result = merge(QJsonValue::Null);
        QVERIFY(!result.contains("manufactureDate"));
        QVERIFY(!result.contains("manufactureDateRaw"));
    }
    void gpuListPreservesIdentityAndDeduplicates() {
        auto result = VendorData::parseGpuProcesses("42:game.exe:base64|42:GAME.exe:duplicate|8:dwm.exe:icon|9:editor.exe:C:\\icon.png|");
        QVERIFY(result["available"].toBool());
        QVERIFY(result["busy"].toBool());
        auto processes = result["processes"].toArray();
        QCOMPARE(processes.size(), 3);
        QCOMPARE(processes[0].toObject()["pid"].toInt(), 42);
        QCOMPARE(processes[0].toObject()["name"].toString(), QString("game.exe"));
        QVERIFY(processes[1].toObject()["ignored"].toBool());
        result = VendorData::parseGpuProcesses("8:DWM.EXE:|9:explorer.exe:|");
        QVERIFY(result["available"].toBool());
        QVERIFY(!result["busy"].toBool());
        result = VendorData::parseGpuProcesses("");
        QVERIFY(result["available"].toBool());
        QVERIFY(!result["busy"].toBool());
        QVERIFY(result["processes"].toArray().isEmpty());
    }
    void malformedGpuListNeverMeansClear() {
        for (const auto &text : QStringList{"garbage", "1:app.exe", "x:app.exe:icon", "0:app.exe:icon",
                "4294967296:app.exe:icon", "1::icon", "1:app.exe:icon|bad", "-1:app.exe:icon"}) {
            const auto result = VendorData::parseGpuProcesses(text);
            QVERIFY2(!result["available"].toBool(), qPrintable(text));
            QVERIFY(result["busy"].isNull());
            QVERIFY(!result["error"].toString().isEmpty());
        }
    }
    void workerNoiseDoesNotPolluteProtocol() {
        auto result=VendorData::parseGpuWorkerOutput("luid_counter diagnostic\r\ncounter: pid_42, value 100\r\nTHINKVANTAGE_GPU_JSON={\"available\":true,\"busy\":true,\"processes\":[{\"name\":\"app.exe\",\"pid\":42}]}\r\n");
        QVERIFY(result["available"].toBool());
        QVERIFY(result["busy"].toBool());
        QCOMPARE(result["processes"].toArray().size(),1);
        for(const auto &bytes:QList<QByteArray>{"noise only", "prefix THINKVANTAGE_GPU_JSON={}",
                "THINKVANTAGE_GPU_JSON=not-json", "THINKVANTAGE_GPU_JSON=[]",
                "THINKVANTAGE_GPU_JSON={\"available\":true,\"processes\":[]}",
                "THINKVANTAGE_GPU_JSON={\"available\":true,\"busy\":false,\"processes\":[]}\nTHINKVANTAGE_GPU_JSON=broken"}){
            result=VendorData::parseGpuWorkerOutput(bytes);
            QVERIFY2(!result["available"].toBool(),bytes.constData());
            QVERIFY(result["busy"].isNull());
            QVERIFY(!result["error"].toString().isEmpty());
        }
    }
};
QTEST_GUILESS_MAIN(VendorTests)
#include "vendor_tests.moc"
