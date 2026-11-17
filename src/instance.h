#pragma once
#include <QObject>
#include <QLocalServer>
#include <QLockFile>
#include <memory>
#include <functional>
#include <QPointer>
class QLocalSocket;
class QTimer;

class AppInstance : public QObject {
    Q_OBJECT
public:
    explicit AppInstance(QObject *parent=nullptr,const QString &scope={},const QString &lockDirectory={});
    bool acquire(bool handoff=false);
    bool notify(const QByteArray &command);
    bool beginHandoff(std::function<bool()> prepare,std::function<void()> cancel,
                      std::function<bool(const QString &)> launch={});
    bool joinHandoff(const QString &token);
    bool confirmHandoff();
    ~AppInstance() override;
signals:
    void commandReceived(const QByteArray &command);
    void handoffFinished(bool ok,const QString &error);
private:
    QString name;
    std::unique_ptr<QLockFile> lock;
    QLocalServer server;
    std::unique_ptr<QLocalServer> transferServer;
    std::unique_ptr<QLocalSocket> transferClient;
    QPointer<QLocalSocket> transferPeer;
    QTimer *transferTimer=nullptr;
    void *transferProcess=nullptr;
    bool released=false,preparing=false,finishing=false;
    void finishHandoff(bool success,const QString &error,std::function<void()> cancel);
};
