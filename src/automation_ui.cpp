#include "automation_ui.h"
#include "automation.h"
#include "operation_journal.h"
#include "window.h"
#include <QtWidgets>
#include <QProcess>
#include <QStandardPaths>

namespace {
class Controller : public QObject {
    Window *window;
    Automation::Engine engine;
    QString path=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/automation.json";
    QJsonObject latest;
    QList<Automation::Action> queue;
    QProcess *worker=nullptr;
    QLabel *status,*task;
    QCheckBox *enabled;
    QMap<QString,QLineEdit*> names;
    QMap<QString,QComboBox*> choices;
    bool preview=false,storageHealthy=true,quitting=false,manualChargePending=false,awaitingFresh=false,taskRecorded=false;
    QString detail,storageError;
    bool cancelPending=false,handoffPreparing=false;
    bool persist(){
        if(preview)return false;QString error;storageHealthy=Automation::save(path,engine.config,&error);
        if(!storageHealthy){storageError="配置保存失败，已停止普通自动写入；未完成恢复会在下次启动核对："+error;queue.clear();}
        else {storageError.clear();taskRecorded=engine.hasTask();}return storageHealthy;
    }
    void render(){
        task->setText(engine.hasTask()?QString("充满任务：%1\n原模式：%2；达到 100%、拔电、取消或退出后恢复。%3").arg(engine.config["topUp"].toObject()["phase"].toString()).arg(engine.config["topUp"].toObject()["original"].toInt()==1?"养护":"快充").arg(engine.config["topUp"].toObject()["error"].toString()):"没有一次性充满任务。程序需驻留以检测电量；异常退出后下次启动尝试恢复。");
        status->setText((detail.isEmpty()?(preview?QString("预览模式，自动化不会运行。"):engine.config["enabled"].toBool()?QString("已启用；只在供电转换时执行一次，手动更改保留到下次转换。"):QString("自动场景已暂停。")):detail)+(storageError.isEmpty()?QString():"\n"+storageError));
    }
    void enqueue(const QList<Automation::Action> &actions){queue.append(actions);pump();}
    void pump(){
        if(!preview&&cancelPending&&!worker&&!window->isBusy()){
            cancelPending=false;queue.clear();auto actions=engine.cancelTopUp(latest);const bool saved=persist();
            if(saved||taskRecorded)queue.append(actions);awaitingFresh=false;detail=actions.isEmpty()?"取消已完成。":"正在恢复原充电模式…";render();
        }
        if(preview||worker||window->isBusy()||awaitingFresh||queue.isEmpty())return;
        auto action=queue.takeFirst();
        const bool emergencyRestore=action.reason=="topUpRestore"&&taskRecorded;
        if((!storageHealthy&&!emergencyRestore)||((quitting||handoffPreparing)&&action.reason!="topUpRestore")){queue.clear();return;}
        if((action.reason.startsWith("topUp")&&!engine.hasTask())||(!action.reason.startsWith("topUp")&&(engine.overrides.contains(action.id)||!engine.config["enabled"].toBool()))) {pump();return;}
        if(!persist()&&!emergencyRestore){render();return;}
        worker=new QProcess(this);window->setAutomationBusy(true);auto p=worker;p->setProgram(QCoreApplication::applicationFilePath());
        QStringList arguments{"--apply-if",action.id,QString::number(action.value),QString::number(action.expected)};
        if(action.reason=="acProfile"||action.reason=="topUpStart")arguments<<"ac";else if(action.reason=="dcProfile")arguments<<"dc";
        p->setArguments(arguments);
        auto timer=new QTimer(p);timer->setSingleShot(true);timer->setInterval(22000);connect(timer,&QTimer::timeout,p,&QProcess::kill);
        auto once=std::make_shared<bool>(false);
        auto finish=[this,p,once,action]{
            if(*once)return;*once=true;auto doc=QJsonDocument::fromJson(p->readAllStandardOutput());auto result=doc.isObject()?doc.object():QJsonObject{{"ok",false},{"unknownOutcome",true},{"error","自动化进程未返回有效结果，请刷新检查。"}};
            if(p->exitStatus()==QProcess::CrashExit||!result["ok"].isBool())result={{"ok",false},{"unknownOutcome",true},{"error","自动化进程异常结束，实际结果需要重新读取确认。"}};
            if(result["ok"].toBool()&&result["state"].isObject()){auto f=latest["features"].toObject();f[action.id]=result["state"];latest["features"]=f;}
            engine.complete(action,result);persist();Journal::append({{"kind","automation"},{"reason",action.reason},{"id",action.id},{"result",result}});
            detail=QString("%1：%2").arg(action.id,result["ok"].toBool()?"已确认":result["error"].toString("未完成"));
            worker=nullptr;awaitingFresh=true;window->setAutomationBusy(false);if(result["unknownOutcome"].toBool())window->markUnknownOutcome(result["error"].toString("自动化设置结果未知"));p->deleteLater();render();window->refreshState();QTimer::singleShot(250,this,[this]{pump();if(quitting&&!worker&&queue.isEmpty())window->requestQuit();});
        };
        connect(p,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish](int,QProcess::ExitStatus){finish();});
        connect(p,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish();});
        p->start();timer->start();render();
    }
    void observe(const QJsonObject &s){
        latest=s;if(preview||worker||window->isBusy()||manualChargePending)return;
        if(s["stale"].toBool()||!s["features"].isObject())return;awaitingFresh=false;
        if(quitting||handoffPreparing){render();return;}
        // Replace work that has not started when the supply changes. An in-flight
        // action is conditional and serialized by the backend's hardware lock.
        if(engine.needsSupplyEvaluation()||(engine.lastAc&&s["ac"].isBool()&&*engine.lastAc!=s["ac"].toBool()))queue.clear();
        auto before=engine.config;auto oldSupply=engine.lastAc;auto actions=engine.observe(s);if(before!=engine.config&&!persist()){render();return;}
        if(engine.config["enabled"].toBool()&&engine.lastAc!=oldSupply){int skipped=0;for(auto state:engine.lastEvaluation)if(state.toObject()["outcome"].toString()=="skipped")skipped++;detail=QString("%1：待执行 %2 项，跳过 %3 项。").arg(engine.config[s["ac"].toBool()?"ac":"dc"].toObject()["name"].toString()).arg(actions.size()).arg(skipped);Journal::append({{"kind","profileEvaluation"},{"ac",s["ac"]},{"steps",engine.lastEvaluation}});}
        enqueue(actions);render();
    }
    void loadEditors(){
        const QStringList keys{"ac","dc"};for(const auto &key:keys){auto p=engine.config[key].toObject();names[key]->setText(p["name"].toString());auto a=p["actions"].toObject();for(const auto &id:Automation::profileIds()){auto c=choices[key+"/"+id];int ix=a.contains(id)?c->findData(a[id].toInt()):0;c->setCurrentIndex(qMax(0,ix));}}
        QSignalBlocker b(enabled);enabled->setChecked(engine.config["enabled"].toBool());
    }
    void saveEditors(){
        if(worker||window->isBusy()){detail="请等待当前操作结束后保存场景。";render();return;}
        for(const auto &key:QStringList{"ac","dc"}){QJsonObject a;for(const auto &id:Automation::profileIds()){auto c=choices[key+"/"+id];if(c->currentIndex()>0)a[id]=c->currentData().toInt();}engine.config[key]=QJsonObject{{"name",names[key]->text().left(80)},{"actions",a}};}
        engine.config["enabled"]=enabled->isChecked();queue.clear();engine.reapply();if(persist()){detail="场景已保存。";observe(latest);}render();
    }
public:
    Controller(Window *w,bool onlyPreview):QObject(w),window(w),preview(onlyPreview){
        if(!preview){QString error;engine.config=Automation::load(path,&error);taskRecorded=engine.hasTask();if(!error.isEmpty()){detail=error+"；自动场景保持关闭，请保存有效配置后再启用。";storageHealthy=false;}}
        auto pages=w->findChild<QStackedWidget*>();auto nav=w->findChild<QListWidget*>("navigation");auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);auto page=new QWidget;page->setObjectName("page");auto layout=new QVBoxLayout(page);layout->setContentsMargins(2,8,10,12);layout->setSpacing(14);
        auto title=new QLabel("场景与充电");title->setObjectName("heading");layout->addWidget(title);auto intro=new QLabel("分别保存接电和电池动作，只接管选中的设置。自动化默认关闭；手动操作优先，下次供电转换才重新接管。刷新率仍使用显示页的预览确认。");intro->setWordWrap(true);layout->addWidget(intro);
        enabled=new QCheckBox("启用 AC/DC 自动场景（保存后生效）");layout->addWidget(enabled);
        for(const auto &key:QStringList{"ac","dc"}){
            auto box=new QGroupBox(key=="ac"?"连接电源":"使用电池");auto form=new QFormLayout(box);auto name=new QLineEdit;names[key]=name;name->setMaxLength(80);form->addRow("场景名称",name);
            for(const auto &id:Automation::profileIds()){
                auto c=new QComboBox;choices[key+"/"+id]=c;c->addItem("保持原设置",-1);
                if(id=="its"){c->addItem("自适应",0);c->addItem("节能",1);c->addItem("性能",3);if(key=="ac")c->addItem("极客性能",4);}
                else if(id=="power"){c->addItem("最佳能效",0);c->addItem("平衡",1);c->addItem("最佳性能",2);}
                else if(id=="backlight"){c->addItem("关闭",0);c->addItem("低亮度",1);c->addItem("高亮度",2);c->addItem("自动（设备支持时）",3);}
                else for(int n=0;n<=100;n++)c->addItem(QString::number(n)+"%",n);
                form->addRow(id=="its"?"联想性能模式":id=="power"?"Windows 电源模式":id=="backlight"?"键盘背光":"内屏亮度",c);
            }layout->addWidget(box);
        }
        auto buttons=new QHBoxLayout;auto save=new QPushButton("保存并应用当前场景");auto pause=new QPushButton("暂停自动场景");auto exportButton=new QPushButton("导出场景");auto importButton=new QPushButton("导入场景");buttons->addWidget(save);buttons->addWidget(pause);buttons->addWidget(exportButton);buttons->addWidget(importButton);buttons->addStretch();layout->addLayout(buttons);
        auto group=new QGroupBox("临时充满一次");auto gl=new QVBoxLayout(group);task=new QLabel;task->setWordWrap(true);gl->addWidget(task);auto tb=new QHBoxLayout;auto start=new QPushButton("临时充满至 100%");auto cancel=new QPushButton("取消 / 重试恢复原模式");tb->addWidget(start);tb->addWidget(cancel);tb->addStretch();gl->addLayout(tb);layout->addWidget(group);
        task->setTextFormat(Qt::PlainText);status=new QLabel;status->setTextFormat(Qt::PlainText);status->setWordWrap(true);layout->addWidget(status);layout->addStretch();scroll->setWidget(page);pages->addWidget(scroll);nav->addItem("场景与充电");loadEditors();render();
        connect(save,&QPushButton::clicked,this,[this]{saveEditors();});
        connect(pause,&QPushButton::clicked,this,[this]{engine.config["enabled"]=false;enabled->setChecked(false);for(qsizetype i=queue.size();i>0;--i)if(!queue[i-1].reason.startsWith("topUp"))queue.removeAt(i-1);persist();detail=worker?"已暂停；当前操作完成后不再启动普通场景动作。":"自动场景已暂停；当前充满任务独立保留。";render();});
        connect(start,&QPushButton::clicked,this,[this]{if(worker||window->isBusy())return;QString error;auto actions=engine.startTopUp(latest,&error);detail=error;if(!actions.isEmpty()&&persist())enqueue(actions);render();});
        connect(cancel,&QPushButton::clicked,this,[this]{cancelPending=true;auto t=engine.config["topUp"].toObject();if(!t.isEmpty()){t["cancelRequested"]=true;engine.config["topUp"]=t;persist();}detail="已收到取消请求，正在等待当前操作结束并核对后恢复。";render();pump();});
        connect(exportButton,&QPushButton::clicked,this,[this]{auto file=QFileDialog::getSaveFileName(window,"导出场景",QString(),"JSON (*.json)");if(file.isEmpty())return;auto c=engine.config;c.remove("topUp");QString error;detail=Automation::save(file,c,&error)?"场景已导出（不包含一次性任务）。":error;render();});
        connect(importButton,&QPushButton::clicked,this,[this]{if(worker||window->isBusy())return;auto file=QFileDialog::getOpenFileName(window,"导入场景",QString(),"JSON (*.json)");if(file.isEmpty()||worker||window->isBusy())return;QString error;auto c=Automation::load(file,&error);if(!error.isEmpty()){detail=error;render();return;}auto t=engine.config["topUp"];c.remove("topUp");if(t.isObject())c["topUp"]=t;c["enabled"]=false;engine.config=c;queue.clear();persist();loadEditors();detail="场景已导入并暂停，请检查后保存启用。";render();});
        if(preview){for(auto b:{save,pause,exportButton,importButton,start,cancel})b->setEnabled(false);return;}
        connect(w,&Window::snapshotChanged,this,[this](const QJsonObject &s){observe(s);});
        connect(w,&Window::supplyObserved,this,[this](bool ac){engine.supplyChanged(ac);});
        connect(w,&Window::manualSettingRequested,this,[this](const QString &id,int){engine.manual(id);if(id=="charge")manualChargePending=true;for(qsizetype i=queue.size();i>0;--i)if(queue[i-1].id==id)queue.removeAt(i-1);persist();render();});
        connect(w,&Window::manualSettingFinished,this,[this](const QString &id,bool ok){if(id!="charge")return;manualChargePending=false;if(ok)engine.config.remove("topUp");persist();render();if(quitting)QTimer::singleShot(0,window,&Window::requestQuit);});
        auto timer=new QTimer(this);timer->setInterval(30000);connect(timer,&QTimer::timeout,this,[this]{if((engine.config["enabled"].toBool()||engine.hasTask())&&!window->isBusy()&&!worker)window->refreshState();});timer->start();
        auto pumpTimer=new QTimer(this);pumpTimer->setInterval(300);connect(pumpTimer,&QTimer::timeout,this,[this]{pump();});pumpTimer->start();
        w->quitGuard=[this]{
            quitting=true;queue.clear();if(worker)return false;
            if(!engine.hasTask()||(!storageHealthy&&!taskRecorded)||engine.config["topUp"].toObject()["phase"].toString()=="attention")return true;
            auto actions=engine.cancelTopUp(latest);const bool saved=persist();if((!saved&&!taskRecorded)||actions.isEmpty())return true;awaitingFresh=false;enqueue(actions);return false;
        };
        w->handoffPreparation=[this]{
            handoffPreparing=true;queue.clear();if(worker||window->isBusy())return false;
            if(!engine.hasTask())return true;
            // A failed restoration must keep the original instance alive.
            if(engine.config["topUp"].toObject()["phase"].toString()=="attention")return false;
            auto actions=engine.cancelTopUp(latest);bool saved=persist();if(!saved&&!taskRecorded)return false;
            if(actions.isEmpty())return true;awaitingFresh=false;enqueue(actions);return false;
        };
        w->handoffCancelled=[this]{handoffPreparing=false;quitting=false;detail="管理员交接未完成，当前实例继续运行。";render();window->refreshState();};
    }
};
}
void setupAutomation(Window *window,bool previewOnly){new Controller(window,previewOnly);}
