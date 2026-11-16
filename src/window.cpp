#include "window.h"
#include "window_widgets.h"
#include "backend.h"
#include "mode_slider.h"
#include "ui_update.h"
#include <QProcess>
#include <QJsonDocument>
#include <QFile>
void Window::showPage(int index){findChild<QListWidget*>()->setCurrentRow(qBound(0,index,pages->count()-1));showNormal();raise();activateWindow();}
void Window::closeEvent(QCloseEvent *event){
    if(trayActive){hide();event->ignore();emit hiddenToTray();return;}
    // Use the same exit guard for title-bar close and the tray's Quit action.
    event->ignore();requestQuit();
}
void Window::requestQuit(){
    if(quitGuard&&!quitGuard())return;
    if(isBusy()){quitPending=true;status->setText("当前操作结束后退出…");return;}
    QCoreApplication::quit();
}
void Window::enableControls(){
    const auto f=snapshot["features"].toObject();
    for(auto i=controls.begin();i!=controls.end();++i)Ui::enabled(i.value(),!externalBusy&&!pendingSettings.contains(i.key())&&Ui::writable(snapshot,i.key()));
    for(auto i=unitControls.begin();i!=unitControls.end();++i){auto s=f[i.key()].toObject();Ui::enabled(i.value(),!externalBusy&&!snapshot["stale"].toBool()&&s["available"].toBool()&&s["meters"].isArray());}
    bool writing=isApplying();Ui::enabled(refresh,!writing);Ui::enabled(admin,!externalBusy&&pendingSettings.isEmpty());Ui::enabled(gpuRefresh,!isBusy()&&!snapshot["stale"].toBool());if(lastWriteBusy!=writing){lastWriteBusy=writing;emit busyChanged(writing);}
    if(quitPending&&!isBusy())QTimer::singleShot(0,this,&Window::requestQuit);
}
void Window::markStale(const QString &error){
    const auto discarded=pendingSettings.keys();
    pendingSettings.clear();pendingOrder.clear();
    for(const auto &id:discarded)emit manualSettingFinished(id,false);
    updateState(Ui::expiredSnapshot(snapshot,error,QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)));
    auto message="状态已过期："+error+"。请刷新后重试。";statusUntil=0;Ui::text(status,message);emit operationFinished(false,message);
}
void Window::invalidateState(const QString &reason){
    snapshot["lastInvalidationReason"]=reason;snapshot["invalidatedAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);diagnosticsDirty=true;
    eventRefreshPending=true;if(eventRefreshTimer)eventRefreshTimer->start();
}
void Window::run(const QStringList &args,std::function<void(QJsonObject)> done){
    if(process)return;
    process=new QProcess(this);auto proc=process;proc->setProgram(QCoreApplication::applicationFilePath());proc->setArguments(args);
    const bool refreshWorker=args.size()>=2&&args[0]=="--apply"&&args[1]=="refresh";
    const bool applyWorker=!args.isEmpty()&&args[0]=="--apply";
    auto timeout=new QTimer(proc);timeout->setSingleShot(true);timeout->setInterval(22000);
    auto output=std::make_shared<QByteArray>();auto completed=std::make_shared<bool>(false),timedOut=std::make_shared<bool>(false);
    connect(proc,&QProcess::readyReadStandardOutput,this,[this,proc,output,timeout,timedOut]{
        output->append(proc->readAllStandardOutput());
        int pos=output->indexOf('\n');if(pos<0)return;
        auto preview=QJsonDocument::fromJson(output->left(pos)).object();
        if(!preview["preview"].toBool())return;
        output->remove(0,pos+1);
        // The worker owns the rollback and the cross-process lock until it
        // emits its final result. Give confirmation/rollback its own budget.
        if(*timedOut){proc->closeWriteChannel();return;}
        timeout->start(20000);
        QMessageBox box(QMessageBox::Question,"保留此刷新率？","屏幕将在 15 秒后恢复原刷新率。",QMessageBox::Yes|QMessageBox::No,this);
        box.button(QMessageBox::Yes)->setText("保留");box.button(QMessageBox::No)->setText("恢复");box.setDefaultButton(QMessageBox::No);
        QTimer timer;timer.setSingleShot(true);connect(&timer,&QTimer::timeout,&box,&QDialog::reject);timer.start(12000);
        connect(proc,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),&box,&QDialog::reject);
        QPointer<QProcess> guarded(proc);bool keep=box.exec()==QMessageBox::Yes;
        if(guarded&&guarded->state()!=QProcess::NotRunning&&!*timedOut)guarded->write(keep?"keep\n":"revert\n");
    });
    auto finish=[this,proc,output,completed,timedOut,done](const QJsonObject &fallback){
        if(*completed)return;*completed=true;output->append(proc->readAllStandardOutput());
        auto doc=QJsonDocument::fromJson(output->trimmed());auto result=doc.isObject()?doc.object():fallback;
        if(fallback["crashed"].toBool()&&fallback["unknownOutcome"].toBool())result["unknownOutcome"]=true;
        if(*timedOut&&!doc.isObject()){result["unknownOutcome"]=true;result["error"]="设备进程超时，实际结果需要重新读取确认";}
        if(result["unknownOutcome"].toBool()){result["ok"]=false;if(!result.contains("error"))result["error"]="设备进程异常退出，实际结果需要重新读取确认";}
        process=nullptr;proc->deleteLater();done(result);enableControls();if(!pendingSettings.isEmpty())QTimer::singleShot(0,this,&Window::drainPending);
        else if(eventRefreshPending&&eventRefreshTimer)eventRefreshTimer->start();
    };
    connect(proc,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[finish,applyWorker](int,QProcess::ExitStatus exit){finish({{"ok",false},{"unknownOutcome",applyWorker},{"crashed",exit==QProcess::CrashExit},{"error","设备进程未返回有效结果"}});});
    connect(proc,&QProcess::errorOccurred,this,[finish](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish({{"ok",false},{"error","无法启动设备进程"}});});
    connect(timeout,&QTimer::timeout,this,[this,proc,refreshWorker,timedOut]{
        *timedOut=true;
        if(refreshWorker){proc->closeWriteChannel();Ui::text(status,"刷新率操作超时，正在等待设备进程恢复或核对状态…");}
        else proc->kill();
    });
    enableControls();proc->start();timeout->start();
}
void Window::reload(){
    if(externalBusy||process||!pendingSettings.isEmpty()||refreshPending)return;
    eventRefreshPending=false;
    run({"--probe"},[this](QJsonObject json){
        if(!json["features"].isObject()||(json.contains("ok")&&!json["ok"].toBool())){markStale(json["error"].toString("设备进程未返回完整快照"));return;}
        json["stale"]=false;json["lastSuccessfulRead"]=json["timestamp"];json["lastAttemptAt"]=QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs);
        if(!json.contains("gpuOccupancy")&&snapshot.contains("gpuOccupancy"))json["gpuOccupancy"]=snapshot["gpuOccupancy"];
        updateState(json);
        if(!capturePath.isEmpty()||smoke){
            QTimer::singleShot(100,this,[this]{
                bool ok=true;auto features=snapshot["features"].toObject();
                for(auto i=controls.begin();i!=controls.end();++i){auto f=features[i.key()].toObject();if(i.value()->isEnabled()!=(f["available"].toBool()&&f["enabled"].toBool(true)))ok=false;}
                for(auto button:findChildren<QPushButton*>("advanced")){auto panel=qobject_cast<QWidget*>(button->property("panel").value<QObject*>());bool original=button->isChecked();button->click();ok=(panel->isHidden()==original)&&ok;button->click();ok=(panel->isHidden()!=original)&&ok;}
                for(auto i=unitControls.begin();i!=unitControls.end();++i){auto unit=qobject_cast<QComboBox*>(i.value());int original=unit->currentIndex();auto meters=features[i.key()].toObject()["meters"].toArray();if(meters.isEmpty())continue;unit->setCurrentIndex(1);updateState(snapshot);auto buttons=controls[i.key()]->findChildren<QRadioButton*>();ok=buttons.size()==3&&buttons[0]->text().contains("英尺")&&ok;unit->setCurrentIndex(0);updateState(snapshot);buttons=controls[i.key()]->findChildren<QRadioButton*>();ok=buttons.size()==3&&buttons[0]->text().contains("米")&&ok;unit->setCurrentIndex(original);updateState(snapshot);}
                if(!capturePath.isEmpty()){
                    QFileInfo fi(capturePath);QDir().mkpath(fi.absolutePath());
                    for(int i=0;i<pages->count();i++){
                        findChild<QListWidget*>()->setCurrentRow(i);QApplication::processEvents();auto name=i==0?capturePath:fi.dir().filePath(fi.completeBaseName()+QString("-%1.png").arg(i));ok=grab().save(name)&&ok;
                        auto scroll=qobject_cast<QScrollArea*>(pages->widget(i));auto buttons=scroll->findChildren<QPushButton*>("advanced");for(auto button:buttons)button->setChecked(true);QApplication::processEvents();
                        ok=scroll->widget()->grab().save(fi.dir().filePath(fi.completeBaseName()+QString("-%1-full.png").arg(i)))&&ok;
                        for(auto button:buttons)button->setChecked(false);
                    }
                    resize(minimumSize());
                    for(int i=0;i<pages->count();i++){
                        findChild<QListWidget*>()->setCurrentRow(i);QApplication::processEvents();
                        ok=grab().save(fi.dir().filePath(fi.completeBaseName()+QString("-%1-compact.png").arg(i)))&&ok;
                    }
                }
                if(smoke){QJsonObject report{{"ok",ok},{"controls",controls.size()},{"pages",pages->count()},{"snapshot",snapshot}};QFile f(QCoreApplication::applicationDirPath()+"/ui-smoke.json");if(f.open(QIODevice::WriteOnly))f.write(QJsonDocument(report).toJson());}
                QCoreApplication::exit(ok?0:2);
            });
        }
    });
}
void Window::updateState(const QJsonObject &json){
    loading=true;diagnosticsDirty=diagnosticsDirty||!Ui::equalData(snapshot,json);snapshot=json;snapshot.remove("_pending");
    QString product=json["product"].toString(),model=json["model"].toString();device->setText(product.isEmpty()?QString("Lenovo / ")+model:product+"  ·  "+model);
    Ui::visible(admin,!json["administrator"].toBool());
    auto features=json["features"].toObject();int count=0;
    for(auto i=controls.begin();i!=controls.end();++i){
        const auto f=features[i.key()].toObject();bool available=f["available"].toBool();if(available)count++;int display=pendingSettings.value(i.key(),f["value"].toInt(-1));
        bool stale=json["stale"].toBool()||f["stale"].toBool();
        auto reason=stale?"状态已过期："+json["readError"].toString(f["readError"].toString()):f["error"].toString("当前设备未提供此设置");auto readable=reason;
        if(!json["administrator"].toBool()&&(reason.contains("管理员")||reason.contains("2147749891")))readable="需要管理员模式";
        Ui::text(hints[i.key()],available&&!stale?QString():readable);if(hints[i.key()]->toolTip()!=reason)hints[i.key()]->setToolTip(reason);Ui::visible(hints[i.key()],!available||stale);
        if(auto mode=qobject_cast<ModeSlider*>(i.value())){
            mode->setState(f,json["ac"].toBool(),pendingSettings.contains(i.key())?std::optional<int>(display):std::nullopt);
        }else if(i.value()->property("choices").toBool()){
            auto grid=qobject_cast<QGridLayout*>(i.value()->layout());auto options=f["options"].toArray();auto meters=f["meters"].toArray();bool imperial=unitControls.contains(i.key())&&qobject_cast<QComboBox*>(unitControls[i.key()])->currentIndex()==1;
            QString signature=QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact))+QString::fromUtf8(QJsonDocument(meters).toJson(QJsonDocument::Compact))+QString::number(imperial);
            if(i.value()->property("signature").toString()!=signature){
                while(auto item=grid->takeAt(0)){delete item->widget();delete item;}int n=0;
                for(auto opt:options){auto o=opt.toObject();int value=o["value"].toInt();QString text=o["label"].toString();
                    if(i.key()=="loginAuto"||i.key()=="lockAuto")text=value==1?"自动":"手动";
                    if(i.key()=="charge")text=QStringList{"标准","养护","快充"}.value(value,text);
                    if(i.key()=="backlight")text=QStringList{"关闭","低","高","自动"}.value(value,text);
                    if(value<meters.size()){double distance=meters[value].toDouble();text+=imperial?QString(" · %1 英尺").arg(distance*3.28084,0,'f',1):QString(" · %1 米").arg(distance,0,'g',3);}
                    auto button=new QRadioButton(text);button->setProperty("optionValue",value);grid->addWidget(button,0,n++);QString id=i.key();connect(button,&QRadioButton::clicked,this,[this,id,value]{setValue(id,value);});
                }
                i.value()->setProperty("signature",signature);
            }
            for(auto button:i.value()->findChildren<QRadioButton*>())Ui::checked(button,available&&button->property("optionValue").toInt()==display);
        }else if(auto toggle=qobject_cast<QCheckBox*>(i.value()))Ui::checked(toggle,available&&display==1);
        else if(auto c=qobject_cast<QComboBox*>(i.value()))Ui::combo(c,f,display);
        else if(auto s=qobject_cast<QSlider*>(i.value())){if(!Ui::editing(s)){if(available)Ui::slider(s,f,display);auto label=qobject_cast<QLabel*>(s->property("valueLabel").value<QObject*>());if(label)Ui::text(label,available?QString::number(display)+(i.key().startsWith("oc")?" MHz":"%"):"—");}}
    }
    auto percent=json["batteryPercent"];QString supply=!json["ac"].isBool()?"供电状态未知":json["ac"].toBool()?"已连接电源":"正在使用电池";batterySummary->setText((percent.isDouble()?QString("%1%  ").arg(percent.toInt()):QString())+supply+(json["stale"].toBool()?" · 已过期":""));
    auto hpd=features["presenceLogin"].toObject();presenceInfo->setText(hpd["available"].toBool()?(hpd["faceRegistered"].isBool()?(hpd["faceRegistered"].toBool()?"传感器已连接  ·  Windows Hello 已配置":"传感器已连接  ·  人脸登录尚未配置"):"传感器已连接"):"感知服务暂不可用，请查看下方提示。");
    auto b=json["battery"].toObject();
    QStringList caption;if(b.contains("healthExact"))caption<<QString("容量比 %1%").arg(b["healthExact"].toDouble(),0,'f',1);if(b.contains("cycles"))caption<<QString("循环 %1 次").arg(b["cycles"].toInt());batteryDetail->setText(caption.isEmpty()?"电池详细信息暂不可用":caption.join("   ·   "));
    auto chart=static_cast<BatteryChart*>(batteryChart);chart->setCapacities(b["currentWh"].toDouble(-1),b["fullWh"].toDouble(-1),b["designWh"].toDouble(-1));
    bool temp=b["temperatureC"].isDouble();double c=b["temperatureC"].toDouble();Ui::text(temperature,temp?QString("%1°C").arg(c,0,'f',0):"—");temperature->setToolTip(temp?QString("电池温度 · %1°F").arg(c*1.8+32,0,'f',1):"电池温度暂不可用");Ui::visible(temperatureBar,temp);Ui::progress(temperatureBar,qRound(c));
    int charge=b["chargeStatus"].toInt(-99);QString chargeState=charge==0?"无充放电活动":charge==1?"正在充电":charge==2?"正在放电":charge==3?"接电放电":"状态未提供";batteryRows["charge"]->setText(chargeState+(percent.isDouble()?QString("（%1%）").arg(percent.toInt()):QString()));
    batteryRows["health"]->setText(b.contains("healthExact")?QString("%1%").arg(b["healthExact"].toDouble(),0,'f',1):"—");batteryRows["cycles"]->setText(b.contains("cycles")?QString::number(b["cycles"].toInt()):"—");
    QString chemistry=b["chemistry"].toString();if(chemistry=="LiPM")chemistry="可充电锂离子聚合物电池";else if(chemistry=="LION"||chemistry=="Li-I")chemistry="可充电锂离子电池";batteryRows["chemistry"]->setText(chemistry.isEmpty()?"—":chemistry);
    batteryRows["adapter"]->setText(!json["ac"].isBool()?"供电状态未知":json["ac"].toBool()?"已连接电源":"未连接电源");batteryRows["voltage"]->setText(b.contains("voltageV")?QString::number(b["voltageV"].toDouble(),'f',2)+" V":"—");batteryRows["current"]->setText(b.contains("currentMa")?QString::number(b["currentMa"].toInt())+" mA":"—");batteryRows["manufacturer"]->setText(b["manufacturer"].toString("—"));batteryRows["designVoltage"]->setText(b.contains("designVoltageV")?QString::number(b["designVoltageV"].toDouble(),'f',2)+" V":"—");
    QString direction=b["powerDirection"].toString();QString powerLabel=direction=="charging"?"充电":direction=="discharging"?"放电":direction=="idle"?"空闲":"方向未知";
    batteryRows["power"]->setText(b["powerW"].isDouble()?QString("%1 W（%2）").arg(b["powerW"].toDouble(),0,'f',2).arg(powerLabel):"—");
    batteryRows["adapterStatus"]->setText(b["adapterStatus"].toVariant().toString().isEmpty()?"—":b["adapterStatus"].toVariant().toString());
    batteryRows["adapterRaw"]->setText(b.contains("adapterWattageRaw")?b["adapterWattageRaw"].toVariant().toString()+"（原始值）":"—");
    batteryRows["manufactureDate"]->setText(b["manufactureDate"].toString("—"));
    batteryRows["manufactureDate"]->setToolTip(b["manufactureDateRaw"].toVariant().toString());
    batteryRows["vendorHealth"]->setText(b.contains("vendorHealthCode")?b["vendorHealthCode"].toVariant().toString():"—");
    auto occupancy=json["gpuOccupancy"].toObject();QStringList processes;
    if(occupancy["available"].toBool()){
        for(auto entry:occupancy["processes"].toArray()){auto p=entry.toObject();QString name=p["name"].toString();if(p["pid"].isDouble())name+=QString(" · PID %1").arg(p["pid"].toInt());if(p["ignored"].toBool())name+="（系统/控制台，不阻止切换）";processes<<name;}
        QString observed=occupancy["observedAt"].toString();QString timing=observed.isEmpty()?QString():"\n上次读取："+QDateTime::fromString(observed,Qt::ISODateWithMs).toLocalTime().toString("yyyy-MM-dd HH:mm:ss");
        Ui::text(gpuOccupancy,(processes.isEmpty()?"原厂组件在上次读取时未报告独显占用程序。":processes.join("\n"))+timing);
    }else Ui::text(gpuOccupancy,occupancy["error"].toString("尚未读取独显占用；切换仅核显前仍会独立检查。"));
    chargeThreshold->setText(b["storage80"].toBool()?"养护阈值：低于 75% 开始充电，达到 80% 停止。":"养护充电阈值由设备固件决定。");
    auto oc=features["oc"].toObject();auto od=oc["details"].toObject();ocDetails->setText(oc["available"].toBool()?QString("基础频率：GPU %1 MHz / 显存 %2 MHz；偏移范围：GPU %3–%4 MHz / 显存 %5–%6 MHz").arg(od["coreBase"].toInt()).arg(od["memoryBase"].toInt()).arg(od["coreMin"].toInt()).arg(od["coreMax"].toInt()).arg(od["memoryMin"].toInt()).arg(od["memoryMax"].toInt()):"GPU 超频信息需要原厂组件与管理员权限。");
    if(json["stale"].toBool())Ui::text(status,"状态已过期 · "+json["readError"].toString()+" · 请刷新后重试");
    else if(QDateTime::currentMSecsSinceEpoch()>=statusUntil)Ui::text(status,QString("已读取 %1 项设置  ·  %2").arg(count).arg(QTime::currentTime().toString("HH:mm:ss")));
    updateDiagnostics();loading=false;enableControls();emit snapshotChanged(presentationState());
}
void Window::setValue(const QString &id,int value){
    if(loading||quitPending||externalBusy||!Ui::writable(snapshot,id))return;
    if(!pendingSettings.contains(id)&&snapshot["features"].toObject()[id].toObject()["value"].toInt(-1)==value)return;
    emit manualSettingRequested(id,value);
    if(!pendingSettings.contains(id))pendingOrder<<id;pendingSettings[id]=value;updateState(snapshot);drainPending();
}
QJsonObject Window::presentationState()const{auto state=snapshot;QJsonObject pending;for(auto i=pendingSettings.begin();i!=pendingSettings.end();++i)pending[i.key()]=i.value();state["_pending"]=pending;return state;}
void Window::drainPending(){
    if(externalBusy||process||!applyingId.isEmpty()||pendingSettings.isEmpty())return;
    while(!pendingOrder.isEmpty()&&!pendingSettings.contains(pendingOrder.first()))pendingOrder.removeFirst();if(pendingOrder.isEmpty())return;
    QString id=pendingOrder.first();int value=pendingSettings[id];applyingId=id;
    statusUntil=0;Ui::text(status,"正在应用并确认设置…");
    run({"--apply",id,QString::number(value)},[this,id,value](QJsonObject result){
        auto fresh=snapshot;if(result["ok"].toBool()&&result["state"].isObject()){auto features=fresh["features"].toObject();features[id]=result["state"];fresh["features"]=features;}
        if(pendingSettings.value(id)==value){pendingSettings.remove(id);pendingOrder.removeAll(id);}applyingId.clear();
        if(pendingSettings.isEmpty())refreshPending=true;
        if(result["unknownOutcome"].toBool()){auto discarded=pendingSettings.keys();pendingSettings.clear();pendingOrder.clear();for(const auto &pending:discarded)emit manualSettingFinished(pending,false);fresh=Ui::expiredSnapshot(fresh,result["error"].toString("设置结果未知"),QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs));}
        updateState(fresh);
        QString message=result["ok"].toBool()?result["message"].toString("设置已由设备回读确认。"):"设置未完成："+result["error"].toString();
        statusUntil=QDateTime::currentMSecsSinceEpoch()+6000;Ui::text(status,message);emit operationFinished(result["ok"].toBool(),message);
        emit manualSettingFinished(id,result["ok"].toBool()&&!result["unknownOutcome"].toBool());
        if(pendingSettings.isEmpty())QTimer::singleShot(200,this,[this]{refreshPending=false;if(quitPending&&!isBusy())requestQuit();else if(pendingSettings.isEmpty())reload();else drainPending();});
    });
}
void Window::updateDiagnostics(){
    if(!diagnosticsDirty||!diagnostics->isVisible())return;
    auto text=QString::fromUtf8(QJsonDocument(snapshot).toJson());if(diagnostics->toPlainText()!=text){auto bar=diagnostics->verticalScrollBar();int scroll=bar->value();auto cursor=diagnostics->textCursor();int position=cursor.position(),anchor=cursor.anchor();diagnostics->setPlainText(text);cursor=diagnostics->textCursor();cursor.setPosition(qMin(anchor,int(text.size())));cursor.setPosition(qMin(position,int(text.size())),QTextCursor::KeepAnchor);diagnostics->setTextCursor(cursor);bar->setValue(scroll);}diagnosticsDirty=false;
}
bool Window::eventFilter(QObject *object,QEvent *event){if(object==diagnostics&&event->type()==QEvent::Show)QTimer::singleShot(0,this,&Window::updateDiagnostics);return QMainWindow::eventFilter(object,event);}
