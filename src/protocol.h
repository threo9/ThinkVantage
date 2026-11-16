#pragma once
#include <QList>
#include <QString>
#include <optional>
#include <cstdint>
namespace Protocol {
constexpr uint32_t Settings = 0x831020e8, Charge = 0x831020f8, Backlight = 0x83102144, Night = 0x83102150;
struct Command { uint32_t ioctl; uint32_t token; int bytes; };
std::optional<int> chargeMode(uint32_t raw);
QList<int> chargeOptions(uint32_t raw);
QList<Command> chargeCommands(uint32_t raw, int mode);
std::optional<int> backlightLevels(uint32_t raw);
std::optional<int> backlightValue(uint32_t raw, int levels);
std::optional<Command> backlightCommand(int levels, int value);
std::optional<int> itsCommand(int value);
bool allowedRequest(const QString &id, int value);
bool validOffset(int value,int minimum,int maximum,int step);
QList<double> presenceMeters(int sensor,bool login);
bool nearestVolumeStep(double requestedDb,double actualDb,double minimumDb,double maximumDb,double stepDb);
}
