#include "instance.h"
#include <QLocalSocket>
#include <QCryptographicHash>
#include <QDir>
#include <QStandardPaths>
#include <QThread>
#include <QTimer>
#include <QElapsedTimer>
#include <windows.h>
#include <shellapi.h>
#include <QCoreApplication>
#include <QUuid>
#include <QRegularExpression>

AppInstance::AppInstance(QObject *parent,const QString &scope,const QString &lockDirectory):QObject(parent){
    DWORD session=0;ProcessIdToSessionId(GetCurrentProcessId(),&session);
    auto key=QDir::homePath().toCaseFolded().toUtf8()+QByteArray::number(session)+scope.toUtf8();
    name="ThinkVantage-"+QString::fromLatin1(QCryptographicHash::hash(key,QCryptographicHash::Sha256).toHex().left(20));
    lock=std::make_unique<QLockFile>(QDir(lockDirectory.isEmpty()?QStandardPaths::writableLocation(QStandardPaths::TempLocation):lockDirectory).filePath(name+".lock"));lock->setStaleLockTime(0);
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
    Q_UNUSED(handoff);
    if(lock->tryLock(0)){if(server.listen(name))return true;lock->unlock();}
    return false;
}
bool AppInstance::notify(const QByteArray &command){
    QElapsedTimer timer;timer.start();
    do{QLocalSocket socket;socket.connectToServer(name);if(socket.waitForConnected(200)){socket.write(command+'\n');if(socket.bytesToWrite()>0&&!socket.waitForBytesWritten(300))return false;return (socket.bytesAvailable()>0||socket.waitForReadyRead(500))&&socket.readLine().trimmed()=="ok";}QThread::msleep(50);}while(timer.elapsed()<1800);
    return false;
}
AppInstance::~AppInstance(){if(transferProcess)CloseHandle(static_cast<HANDLE>(transferProcess));}
void AppInstance::finishHandoff(bool success,const QString &error,std::function<void()> cancel){
    if(finishing)return;finishing=true;
    QString reportedError=error;
    if(transferTimer){transferTimer->stop();transferTimer->deleteLater();transferTimer=nullptr;}
    if(!success&&transferPeer){transferPeer->write("abort\n");transferPeer->flush();}
    if(!success&&released){
        // A pre-commit replacement never runs its event loop or automation.
        // Ask it to abort, then reclaim only after it has actually released.
        if(transferProcess){TerminateProcess(static_cast<HANDLE>(transferProcess),1);WaitForSingleObject(static_cast<HANDLE>(transferProcess),1000);}
        if(!acquire())reportedError+="；原窗口保留，但未能收回单实例控制，请关闭未完成的提升实例后重试。";
    }
    if(transferProcess){CloseHandle(static_cast<HANDLE>(transferProcess));transferProcess=nullptr;}
    if(transferPeer){auto peer=transferPeer.data();peer->disconnect(this);peer->disconnectFromServer();peer->deleteLater();}
    if(transferServer)transferServer->close();transferServer.reset();transferPeer.clear();released=false;preparing=false;
    if(!success&&cancel)cancel();finishing=false;emit handoffFinished(success,reportedError);
}
bool AppInstance::beginHandoff(std::function<bool()> prepare,std::function<void()> cancel,std::function<bool(const QString &)> launch){
    if(transferServer)return false;
    auto token=QUuid::createUuid().toString(QUuid::Id128);
    transferServer=std::make_unique<QLocalServer>();transferServer->setSocketOptions(QLocalServer::UserAccessOption);
    if(!transferServer->listen("ThinkVantage-handoff-"+token)){transferServer.reset();return false;}
    connect(transferServer.get(),&QLocalServer::newConnection,this,[this,cancel]{
        auto peer=transferServer->nextPendingConnection();if(transferPeer){peer->abort();peer->deleteLater();return;}transferPeer=peer;peer->setParent(this);
        auto consume=[this,peer,cancel]{
            while(peer->canReadLine()){
                auto line=peer->readLine(64).trimmed();
                if(line=="prepare"&&!released)preparing=true;
                else if(line=="ready"&&released){
                    const auto written=peer->write("commit\n");peer->flush();
                    if(written!=7||(peer->bytesToWrite()>0&&!peer->waitForBytesWritten(500))){finishHandoff(false,"接管完成确认未能发送，原实例保留",cancel);return;}
                    finishHandoff(true,{},cancel);return;
                }
                else {finishHandoff(false,"接管协议状态无效",cancel);return;}
            }
        };
        connect(peer,&QLocalSocket::readyRead,this,consume);
        connect(peer,&QLocalSocket::disconnected,this,[this,cancel]{if(transferServer)finishHandoff(false,"提升实例在完成接管前断开，原实例保留",cancel);});
        consume();
    });
    bool launched=false;
    if(launch)launched=launch(token);
    else {
        auto executable=QCoreApplication::applicationFilePath();QString arguments="--elevated-handoff "+token;
        SHELLEXECUTEINFOW info{};info.cbSize=sizeof(info);info.fMask=SEE_MASK_NOCLOSEPROCESS;info.lpVerb=L"runas";info.lpFile=reinterpret_cast<LPCWSTR>(executable.utf16());info.lpParameters=reinterpret_cast<LPCWSTR>(arguments.utf16());info.nShow=SW_SHOWNORMAL;
        launched=ShellExecuteExW(&info)!=FALSE;transferProcess=info.hProcess;
    }
    if(!launched){finishHandoff(false,"未完成管理员启动，原实例保留",cancel);return false;}
    transferTimer=new QTimer(this);transferTimer->setInterval(100);auto elapsed=std::make_shared<QElapsedTimer>();elapsed->start();
    connect(transferTimer,&QTimer::timeout,this,[this,prepare,cancel,elapsed]{
        if(transferProcess&&WaitForSingleObject(static_cast<HANDLE>(transferProcess),0)==WAIT_OBJECT_0){finishHandoff(false,"提升实例已退出，原实例保留",cancel);return;}
        if(elapsed->elapsed()>120000){finishHandoff(false,"管理员交接未在两分钟内完成，原实例保留",cancel);return;}
        if(preparing&&!released&&prepare()){
            server.close();lock->unlock();released=true;transferPeer->write("go\n");transferPeer->flush();
        }
    });transferTimer->start();return true;
}
namespace {
bool transferLine(QLocalSocket &socket,const QByteArray &expected,int timeout){
    QElapsedTimer timer;timer.start();
    while(timer.elapsed()<timeout){if(socket.canReadLine())return socket.readLine(64).trimmed()==expected;if(!socket.waitForReadyRead(qMax(1,timeout-int(timer.elapsed()))))return false;}return false;
}
}
bool AppInstance::joinHandoff(const QString &token){
    if(!QRegularExpression("^[0-9a-fA-F]{32}$").match(token).hasMatch())return false;
    transferClient=std::make_unique<QLocalSocket>();transferClient->connectToServer("ThinkVantage-handoff-"+token);
    if(!transferClient->waitForConnected(3000))return false;
    transferClient->write("prepare\n");transferClient->flush();
    return transferLine(*transferClient,"go",120000)&&acquire();
}
bool AppInstance::confirmHandoff(){
    if(!transferClient)return false;transferClient->write("ready\n");transferClient->flush();
    return transferLine(*transferClient,"commit",5000);
}
