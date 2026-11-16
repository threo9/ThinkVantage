#include "backend.h"
#include "window.h"
#include "tray.h"
#include "instance.h"
#include "vendor.h"
#include "operation_journal.h"
#include <QApplication>
#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QStyleFactory>
#include <QMessageBox>
#include <QTimer>
#include <QUuid>
#include <cstdio>
namespace { void output(const QJsonObject &o){auto data=QJsonDocument(o).toJson(QJsonDocument::Compact);std::fwrite(data.constData(),1,size_t(data.size()),stdout);std::fwrite("\n",1,1,stdout);std::fflush(stdout);} }
int main(int argc,char **argv){
    const QByteArray command=argc>1?QByteArray(argv[1]):QByteArray();
    const QList<QByteArray> guiCommands{"--tray","--quick","--quit","--screenshot","--ui-smoke","--tray-smoke","--elevated-handoff"};
    bool worker=!command.isEmpty()&&!guiCommands.contains(command);
    if(worker){
        QCoreApplication app(argc,argv);app.setApplicationName("ThinkVantage");app.setOrganizationName("ThinkVantage");auto args=app.arguments();QJsonObject result;
        if(args[1]=="--help"&&args.size()==2)result={{"ok",true},{"commands",QJsonArray{"--help","--version","--probe","--list","--get ID","--apply ID VALUE","--apply-if ID VALUE EXPECTED [ac|dc]","--gpu-processes","--tray","--quick","--quit"}},{"refreshConfirmation","--apply refresh 输出 preview，15秒内向 stdin 写入 keep\\n 保留；断开或超时恢复"}};
        else if(args[1]=="--version"&&args.size()==2)result={{"ok",true},{"version","0.7.0"}};
        else if(args[1]=="--gpu-processes-worker"&&args.size()==2){
            result=Vendor::gpuProcessesRaw();result["ok"]=result["available"].toBool();result["schema"]=1;
            // Proprietary GPUInfo prints diagnostics to stdout. Its parent
            // captures this entire private stream and extracts only this frame.
            std::fwrite("\nTHINKVANTAGE_GPU_JSON=",1,23,stdout);output(result);return result["ok"].toBool()?0:1;
        }
        else if(args[1]=="--gpu-processes"&&args.size()==2){result=Vendor::gpuProcesses();result["ok"]=result["available"].toBool();}
        else if((args[1]=="--probe"||args[1]=="--list")&&args.size()==2){
            result=Backend::read();if(args[1]=="--list"&&result["ok"].toBool())result={{"ok",true},{"features",result["features"]},{"partial",result["partial"]}};
        }
        else if(args[1]=="--get"&&args.size()==3){auto snapshot=Backend::read();auto features=snapshot["features"].toObject();if(!snapshot["ok"].toBool())result=snapshot;else if(!features.contains(args[2]))result={{"ok",false},{"error","未知设置 ID"}};else {auto state=features[args[2]].toObject();result={{"ok",state["available"].toBool()},{"id",args[2]},{"state",state}};if(!state["available"].toBool())result["error"]=state["error"];}}
        else if(args[1]=="--apply"||args[1]=="--apply-if"){
            const bool conditional=args[1]=="--apply-if";const bool countValid=conditional?(args.size()==5||args.size()==6):args.size()==4;
            bool valid=false,expectedValid=!conditional;int value=countValid?args[3].toInt(&valid):0;int expected=conditional&&countValid?args[4].toInt(&expectedValid):0;
            std::optional<bool> expectedAc;if(conditional&&args.size()==6){if(args[5]=="ac")expectedAc=true;else if(args[5]=="dc")expectedAc=false;else valid=false;}
            const QString operationId=QUuid::createUuid().toString(QUuid::WithoutBraces);
            if(valid&&expectedValid){Journal::append({{"kind","request"},{"operationId",operationId},{"id",args[2]},{"requested",value},{"conditional",conditional}});result=Backend::apply(args[2],value,conditional?std::optional<int>(expected):std::nullopt,expectedAc);}else result={{"ok",false},{"error","无效的设置请求"}};
            if(valid&&args[2]=="refresh"&&result["ok"].toBool()){
                auto preview=result;preview["preview"]=true;output(preview);
                result=Backend::confirmRefresh(result["operationToken"].toString());
            }
            result["operationId"]=operationId;Journal::append({{"kind","result"},{"operationId",operationId},{"result",result}});
        }else result={{"ok",false},{"error","未知命令或参数数量无效；使用 --help 查看命令"}};
        result["schema"]=1;if(!result.contains("ok"))result["ok"]=!result.contains("error");output(result);return result["ok"].toBool()?0:1;
    }
    QApplication app(argc,argv);app.setApplicationName("ThinkVantage");app.setApplicationVersion("0.7.0");app.setOrganizationName("ThinkVantage");app.setStyle(QStyleFactory::create("Fusion"));
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
