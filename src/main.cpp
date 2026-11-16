#include "backend.h"
#include "window.h"
#include "tray.h"
#include "instance.h"
#include <QApplication>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QFile>
#include <QStyleFactory>
#include <QMessageBox>
#include <QTimer>
#include <cstdio>
namespace { void output(const QJsonObject &o){auto data=QJsonDocument(o).toJson(QJsonDocument::Compact);std::fwrite(data.constData(),1,size_t(data.size()),stdout);std::fwrite("\n",1,1,stdout);std::fflush(stdout);} }
int main(int argc,char **argv){
    bool worker=argc>1&&(QByteArray(argv[1])=="--probe"||QByteArray(argv[1])=="--apply");
    if(worker){
        QCoreApplication app(argc,argv);auto args=app.arguments();QJsonObject result;
        if(args[1]=="--probe")result=Backend::read();
        else {
            bool valid=false;int value=args.size()==4?args[3].toInt(&valid):0;
            result=valid?Backend::apply(args[2],value):QJsonObject{{"ok",false},{"error","无效的设置请求"}};
            if(args.size()==4&&args[2]=="refresh"&&result["ok"].toBool()){
                auto preview=result;preview["preview"]=true;output(preview);
                result=Backend::confirmRefresh(result["previous"].toInt());
            }
        }
        output(result);return result.contains("ok")&&!result["ok"].toBool()?1:0;
    }
    QApplication app(argc,argv);app.setApplicationName("ThinkVantage");app.setApplicationVersion("0.5.4");app.setOrganizationName("ThinkVantage");app.setStyle(QStyleFactory::create("Fusion"));
    if(!Backend::isWindows11()){QMessageBox::critical(nullptr,"ThinkVantage","此程序仅支持 Windows 11 x64。");return 1;}
    auto args=app.arguments();int ix=args.indexOf("--screenshot");QString capture=ix>=0&&ix+1<args.size()?args[ix+1]:QString();
    bool uiSmoke=args.contains("--ui-smoke"),traySmoke=args.contains("--tray-smoke"),testing=uiSmoke||traySmoke||!capture.isEmpty();
    AppInstance instance;
    if(!testing){
        if(args.contains("--quit"))return instance.notify("quit")?0:1;
        if(!instance.acquire(args.contains("--elevated-handoff"))){bool ok=instance.notify(args.contains("--quick")?"panel":args.contains("--tray")?"noop":"show");if(!ok)QMessageBox::warning(nullptr,"ThinkVantage","另一个实例正在启动或退出，请稍后重试。");return ok?0:1;}
    }
    Window window(traySmoke?QString():capture,uiSmoke);TrayController tray(&window,!uiSmoke&&(capture.isEmpty()||traySmoke));
    QObject::connect(&instance,&AppInstance::commandReceived,&window,[&](const QByteArray &command){if(command=="show")window.showPage();else if(command=="panel")tray.showPanel();else if(command=="quit")window.requestQuit();});
    if(traySmoke){
        auto once=std::make_shared<bool>(false);QObject::connect(&window,&Window::snapshotChanged,&window,[&,once](const QJsonObject &){if(*once)return;*once=true;QTimer::singleShot(150,&window,[&]{auto report=tray.verify(capture);QFile f(QCoreApplication::applicationDirPath()+"/tray-smoke.json");bool written=f.open(QIODevice::WriteOnly)&&f.write(QJsonDocument(report).toJson())>0;QCoreApplication::exit(report["ok"].toBool()&&written?0:2);});});
    }else if(args.contains("--quick"))QTimer::singleShot(0,&window,[&]{tray.showPanel();});
    else if(!args.contains("--tray")||!tray.available())window.show();
    return app.exec();
}
