#include "instance.h"
#include <QLocalSocket>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <windows.h>

AppInstance::AppInstance(QObject *parent):QObject(parent){
    DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);
    auto key=QDir::homePath().toCaseFolded().toUtf8()+QByteArray::number(session);
    name="ThinkVantage-"+QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex().left(20));
    lock=std::make_unique<QLockFile>(QDir(QStandardPaths::writableLocation(QStandardPaths::TempLocation)).filePath(name+".lock"));lock->setStaleLockTime(0);
    server.setSocketOptions(QLocalServer::UserAccessOption);
    connect(&server,&QLocalServer::newConnection,this,[this]{
        while(auto socket=server.nextPendingConnection()){
            socket->setParent(this);auto data=std::make_shared<QByteArray>();
            auto consume=[this,socket,data]{if(socket->property("handled").toBool())return;data->append(socket->readAll());if(data->size()>32){socket->abort();return;}int end=data->indexOf('\n');if(end<0)return;socket->setProperty("handled",true);auto command=data->left(end);if(command=="show"||command=="panel"||command=="quit"||command=="noop")emit commandReceived(command);socket->write("ok\n");socket->flush();socket->disconnectFromServer();};
            connect(socket,&QLocalSocket::readyRead,this,consume);connect(socket,&QLocalSocket::disconnected,socket,&QObject::deleteLater);QTimer::singleShot(1500,socket,[socket]{socket->abort();socket->deleteLater();});consume();
        }
    });
}
bool AppInstance::acquire(bool handoff){
    QElapsedTimer timer;timer.start();
    do{if(lock->tryLock(0)){if(server.listen(name))return true;lock->unlock();return false;}if(!handoff)break;QThread::msleep(50);}while(timer.elapsed()<7000);
    return false;
}
bool AppInstance::notify(const QByteArray &command){
    QElapsedTimer timer;timer.start();
    do{QLocalSocket socket;socket.connectToServer(name);if(socket.waitForConnected(200)){socket.write(command+'\n');if(socket.bytesToWrite()>0&&!socket.waitForBytesWritten(300))return false;return (socket.bytesAvailable()>0||socket.waitForReadyRead(500))&&socket.readLine().trimmed()=="ok";}QThread::msleep(50);}while(timer.elapsed()<1800);
    return false;
}
