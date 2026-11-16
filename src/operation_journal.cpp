#include "operation_journal.h"
#include <QStandardPaths>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QDateTime>
#include <QLockFile>
#include <QCoreApplication>
namespace Journal {
void append(const QJsonObject &entry){
    QString path=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/operations.jsonl";QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile lock(path+".lock");if(!lock.tryLock(200))return;
    if(QFileInfo(path).size()>2*1024*1024){QFile::remove(path+".1");QFile::rename(path,path+".1");}
    QFile f(path);if(!f.open(QIODevice::WriteOnly|QIODevice::Append))return;
    auto e=entry;e["schema"]=1;e["processId"]=QCoreApplication::applicationPid();e["recordedAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);f.write(QJsonDocument(e).toJson(QJsonDocument::Compact)+"\n");
}
}
