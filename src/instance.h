#pragma once
#include <QObject>
#include <QLocalServer>
#include <QLockFile>
#include <memory>

class AppInstance : public QObject {
    Q_OBJECT
public:
    explicit AppInstance(QObject *parent=nullptr);
    bool acquire(bool handoff=false);
    bool notify(const QByteArray &command);
signals:
    void commandReceived(const QByteArray &command);
private:
    QString name;
    std::unique_ptr<QLockFile> lock;
    QLocalServer server;
};
