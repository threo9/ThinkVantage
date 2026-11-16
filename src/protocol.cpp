#include "protocol.h"
#include <cmath>
namespace Protocol {
bool nearestVolumeStep(double requestedDb,double actualDb,double minimumDb,double maximumDb,double stepDb){
    for(double n:{requestedDb,actualDb,minimumDb,maximumDb,stepDb})if(!std::isfinite(n))return false;
    if(stepDb<=0||maximumDb<=minimumDb||stepDb>maximumDb-minimumDb||requestedDb<minimumDb||requestedDb>maximumDb||actualDb<minimumDb||actualDb>maximumDb)return false;
    double index=(actualDb-minimumDb)/stepDb;
    return std::abs(index-std::round(index))<0.001&&std::abs(actualDb-requestedDb)<=stepDb/2+0.001;
}
QList<double> presenceMeters(int sensor,bool login){
    if(sensor!=1&&sensor!=2)return {};
    if(!login)return {0.9,1.2,1.5};
    return sensor==2?QList<double>{0.55,1.0,1.5}:QList<double>{0.42,0.6,1.2};
}
bool validOffset(int value,int minimum,int maximum,int step){return minimum>=0&&maximum>=minimum&&maximum<=2000&&step>0&&value>=minimum&&value<=maximum&&(value-minimum)%step==0;}
std::optional<int> chargeMode(uint32_t raw) {
    if(raw == UINT32_MAX || ((raw & 0x24) == 0x24)) return {};
    return raw & 0x20 ? 1 : raw & 4 ? 2 : 0;
}
QList<int> chargeOptions(uint32_t raw) {
    if(raw == UINT32_MAX) return {};
    QList<int> r{0};
    if(raw & (1u << 23)) r << 1;
    if(raw & (1u << 17)) r << 2;
    return r;
}
QList<Command> chargeCommands(uint32_t raw, int mode) {
    if(!chargeOptions(raw).contains(mode) || !chargeMode(raw)) return {};
    QList<Command> r;
    // Disable the mutually exclusive mode before enabling the requested one.
    if(mode != 2 && (raw & (1u << 17))) r << Command{Charge,8,1};
    if(mode != 1 && (raw & (1u << 23))) {
        r << Command{Charge,5,1};
        if(raw & 0x4000) r << Command{Charge,15,1};
    }
    if(mode == 1) {
        r << Command{Charge,3,1};
        if(raw & 0x4000) r << Command{Charge,13,1};
    }
    if(mode == 2) r << Command{Charge,7,1};
    return r;
}
std::optional<int> backlightLevels(uint32_t raw) {
    if(!(raw & 1) || raw == UINT32_MAX || (raw >> 1) < 1 || (raw >> 1) > 3) return {};
    return int(raw >> 1);
}
std::optional<int> backlightValue(uint32_t raw, int levels) {
    if(!(raw & 1) || raw == UINT32_MAX) return {};
    int value = int((raw >> 1) & 0x7fff);
    if(levels == 3 && !(raw & 0x10000)) value = 0;
    return value <= levels ? std::optional<int>(value) : std::nullopt;
}
std::optional<Command> backlightCommand(int levels, int value) {
    if(levels < 1 || levels > 3 || value < 0 || value > levels) return {};
    return Command{Backlight,uint32_t((value << 16) | (levels << 4) | 3),4};
}
std::optional<int> itsCommand(int value) {
    switch(value) { case 0:return 163; case 1:return 164; case 3:return 165; case 4:return 172; default:return {}; }
}
bool allowedRequest(const QString &id, int value) {
    if(id=="loginDistance"||id=="lockDistance"||id=="lockTimer")return value>=0&&value<=2;
    if(QStringList{"backlightRemember","fnCtrlSwap","capsOsd","numOsd","touchpad","presenceLogin","loginAuto","presenceLock","lockAuto","cameraAssist","presenceVideo"}.contains(id))return value==0||value==1;
    if(id == "its") return itsCommand(value).has_value();
    if(id == "charge") return value >= 0 && value <= 2;
    if(id == "backlight") return value >= 0 && value <= 3;
    if(id == "brightness" || id == "volume") return value >= 0 && value <= 100;
    if(id == "refresh") return value >= 23 && value <= 500;
    if(id == "power") return value >= 0 && value <= 2;
    if(id == "gpu")return value >= 0 && value <= 2;
    if(id == "ocCore" || id == "ocMemory")return value >= 0 && value <= 2000;
    return (id == "flip" || id == "oc" || id == "fnlock" || id == "usbAc" || id == "usbDc" || id == "night" || id == "speakerMute" || id == "micMute") && (value == 0 || value == 1);
}
}
