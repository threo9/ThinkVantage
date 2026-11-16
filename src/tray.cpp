#include "tray.h"
#include "window.h"
#include "window_widgets.h"
#include "mode_slider.h"
#include "ui_update.h"
#include <QJsonArray>
#include <QJsonDocument>
#include <algorithm>
#include <windows.h>

namespace {
QLabel *text(const QString &value,const char *name=nullptr){auto label=new QLabel(value);label->setTextFormat(Qt::PlainText);label->setWordWrap(true);if(name)label->setObjectName(name);return label;}
QFrame *line(){auto l=new QFrame;l->setFixedHeight(1);l->setObjectName("separator");return l;}
}

QuickPanel::QuickPanel():QFrame(nullptr,Qt::Popup|Qt::FramelessWindowHint|Qt::NoDropShadowWindowHint){
    setObjectName("quickPanel");setWindowTitle("ThinkVantage · 快捷面板");setAttribute(Qt::WA_TranslucentBackground);setFixedWidth(408);
    auto outer=new QVBoxLayout(this);outer->setContentsMargins(8,8,8,8);auto card=new QFrame;card->setObjectName("surface");outer->addWidget(card);
    auto root=new QVBoxLayout(card);root->setContentsMargins(18,17,18,14);root->setSpacing(11);
    auto heading=new QHBoxLayout;auto logo=text("T","trayLogo");logo->setAlignment(Qt::AlignCenter);logo->setFixedSize(30,30);heading->addWidget(logo);auto brand=new QVBoxLayout;brand->setSpacing(1);brand->addWidget(text("ThinkVantage","title"));brand->addWidget(text("设备速览与快捷控制","muted"));heading->addLayout(brand);heading->addStretch();auto refresh=new QPushButton("刷新");refresh->setObjectName("flat");heading->addWidget(refresh);connect(refresh,&QPushButton::clicked,this,&QuickPanel::refreshRequested);root->addLayout(heading);
    auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);scroll->setFrameShape(QFrame::NoFrame);auto content=new QWidget;auto layout=new QVBoxLayout(content);layout->setContentsMargins(0,0,0,0);layout->setSpacing(10);scroll->setWidget(content);root->addWidget(scroll,1);
    auto hero=new QFrame;hero->setObjectName("batteryHero");auto h=new QVBoxLayout(hero);h->setContentsMargins(14,12,14,12);h->setSpacing(5);auto bh=new QHBoxLayout;battery=text("—","battery");bh->addWidget(battery);bh->addStretch();supply=text("正在读取…","supply");supply->setAlignment(Qt::AlignRight|Qt::AlignVCenter);bh->addWidget(supply);h->addLayout(bh);batteryBar=new SmoothProgress;batteryBar->setRange(0,100);batteryBar->setTextVisible(false);batteryBar->setFixedHeight(5);h->addWidget(batteryBar);detail=text("正在连接设备…","heroDetail");h->addWidget(detail);layout->addWidget(hero);
    auto mode=new ModeSlider;controls["its"]=mode;layout->addWidget(mode);connect(mode,&ModeSlider::modeRequested,this,[this](int value){if(!updating)emit settingRequested("its",value);});
    auto addChoice=[&](const QString &id,const QString &title,bool combo=false){
        auto row=new QHBoxLayout;row->setSpacing(10);row->addWidget(text(title,"rowTitle"),1);
        if(combo){auto c=new ChoiceCombo;c->setFixedWidth(184);c->setAccessibleName(title);c->addItem("正在读取");controls[id]=c;row->addWidget(c);connect(c,&QComboBox::activated,this,[this,id,c](int index){if(!updating)emit settingRequested(id,c->itemData(index).toInt());});}
        else {auto box=new QWidget;box->setProperty("choices",true);auto grid=new QHBoxLayout(box);grid->setContentsMargins(0,0,0,0);grid->setSpacing(3);controls[id]=box;row->addWidget(box);}
        layout->addLayout(row);
    };
    addChoice("charge","充电策略");addChoice("backlight","键盘背光");layout->addWidget(line());
    auto addSlider=[&](const QString &id,const QString &title){
        auto row=new QHBoxLayout;row->setSpacing(10);auto label=text(title,"rowTitle");label->setFixedWidth(32);row->addWidget(label);auto slider=new SmoothSlider(Qt::Horizontal);slider->setRange(0,100);slider->setAccessibleName(title);controls[id]=slider;row->addWidget(slider,1);auto value=text("—");value->setFixedWidth(35);value->setAlignment(Qt::AlignRight|Qt::AlignVCenter);values[id]=value;row->addWidget(value);layout->addLayout(row);
        auto timer=new QTimer(this);timer->setSingleShot(true);timer->setInterval(450);debounce[id]=timer;slider->setProperty("debounce",QVariant::fromValue<QObject*>(timer));
        connect(slider,&QSlider::valueChanged,this,[this,id,value,timer](int n){Ui::text(value,QString::number(n)+"%");if(!updating)timer->start();});
        connect(timer,&QTimer::timeout,this,[this,id,slider]{if(!updating&&!slider->isSliderDown())emit settingRequested(id,slider->value());});
        connect(slider,&QSlider::sliderReleased,this,[this,id,slider,timer]{timer->stop();if(!updating)emit settingRequested(id,slider->value());});
    };
    addSlider("brightness","亮度");addSlider("volume","音量");layout->addWidget(line());
    auto toggles=new QGridLayout;toggles->setHorizontalSpacing(13);toggles->setVerticalSpacing(7);int n=0;
    for(const auto &item:QList<QPair<QString,QString>>{{"speakerMute","扬声器静音"},{"micMute","麦克风静音"},{"fnlock","Fn Lock"},{"touchpad","触控板"}}){auto row=new QHBoxLayout;row->setSpacing(5);row->addWidget(text(item.second,"toggleLabel"),1);auto toggle=new Toggle;toggle->setAccessibleName(item.second);controls[item.first]=toggle;row->addWidget(toggle);QString id=item.first;connect(toggle,&QCheckBox::clicked,this,[this,id](bool value){if(!updating)emit settingRequested(id,value?1:0);});toggles->addLayout(row,n/2,n%2);++n;}
    layout->addLayout(toggles);layout->addStretch();
    message=text("正在读取设备状态…","message");message->setMinimumHeight(28);root->addWidget(message);
    auto links=new QHBoxLayout;links->setSpacing(12);for(const auto &entry:QList<QPair<QString,int>>{{"电源与性能",0},{"键盘与触控板",1},{"显示与感知",2}}){auto button=new QPushButton(entry.first);button->setObjectName("flat");links->addWidget(button);connect(button,&QPushButton::clicked,this,[this,entry]{hide();emit pageRequested(entry.second);});}root->addLayout(links);
    auto open=new QPushButton("打开 ThinkVantage  ↗");open->setObjectName("primary");connect(open,&QPushButton::clicked,this,[this]{hide();emit pageRequested(0);});root->addWidget(open);
    setStyleSheet(R"(
        QWidget { color:#283a42; font-family:'Microsoft YaHei UI'; font-size:12px; }
        QFrame#surface { background:#ffffff; border:1px solid #dce6e2; border-radius:12px; }
        QLabel#trayLogo { background:#278773; color:white; border-radius:8px; font-size:22px; font-weight:700; }
        QLabel#title { font-size:15px; font-weight:600; }
        QLabel#muted { color:#8b989e; font-size:10px; }
        QFrame#batteryHero { background:#e8f4ee; border:0; border-radius:9px; }
        QLabel#battery { color:#286b55; font-size:27px; font-weight:600; }
        QLabel#supply { color:#467965; font-size:11px; }
        QLabel#heroDetail { color:#719184; font-size:10px; }
        QProgressBar { background:#d2e5db; border:0; border-radius:2px; }
        QProgressBar::chunk { background:#39967b; border-radius:2px; }
        QLabel#rowTitle { font-size:12px; }
        QLabel#toggleLabel { font-size:11px; }
        QFrame#separator { background:#edf1ef; border:0; }
        QLabel#message { color:#82938c; font-size:10px; }
        QLabel#message[error=true] { color:#ae683e; }
        QPushButton { background:#f3f7f5; border:1px solid #dde8e2; border-radius:6px; padding:7px 10px; }
        QPushButton:hover { background:#e9f3ed; border-color:#95bfad; }
        QPushButton:focus { border-color:#278773; }
        QPushButton#flat { background:transparent; border:0; padding:2px 0; color:#488b73; font-size:10px; }
        QPushButton#primary { background:#278773; border-color:#278773; color:white; padding:10px; font-size:12px; }
        QPushButton#primary:hover { background:#20765e; }
        QRadioButton { background:#f3f6f4; border:1px solid transparent; border-radius:5px; padding:7px 9px; spacing:0; font-size:11px; }
        QRadioButton:checked { background:#e0f0e8; color:#26694f; border-color:#bfdccc; }
        QRadioButton:hover { border-color:#95bfad; }
        QRadioButton:focus { border-color:#278773; }
        QRadioButton::indicator { width:0; height:0; }
        QRadioButton:disabled, QComboBox:disabled { color:#a6b2ac; background:#f5f7f6; }
        QComboBox { background:#f6f8f7; border:1px solid #dfe7e3; border-radius:5px; padding:7px 9px; font-size:11px; }
        QComboBox::drop-down { width:22px; border:0; }
        QComboBox::down-arrow { image:none; }
        QComboBox QAbstractItemView { background:white; color:#263c32; selection-background-color:#dfefe6; selection-color:#26694f; border:1px solid #dbe6de; padding:4px; }
        QSlider::groove:horizontal { height:4px; background:#e1e8e4; border-radius:2px; }
        QSlider::sub-page:horizontal { background:#278773; border-radius:2px; }
        QSlider::handle:horizontal { width:11px; background:white; border:2px solid #278773; border-radius:6px; margin:-5px 0; }
        QSlider::handle:horizontal:disabled { border-color:#c5d0c9; }
        QScrollArea, QScrollArea>QWidget>QWidget { background:transparent; border:0; }
        QScrollBar:vertical { width:4px; background:transparent; }
        QScrollBar::handle:vertical { background:#d0dbd4; border-radius:2px; min-height:20px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
    )");
    auto timer=new QTimer(this);timer->setInterval(10000);connect(timer,&QTimer::timeout,this,[this]{bool editing=false;for(auto slider:findChildren<QSlider*>())editing=editing||slider->isSliderDown();for(auto t:debounce)editing=editing||t->isActive();if(isVisible()&&!busy&&!editing)emit refreshRequested();});timer->start();enableControls();
}

void QuickPanel::paintEvent(QPaintEvent *event){QFrame::paintEvent(event);QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);for(int i=6;i>=1;--i){p.setBrush(QColor(15,40,40,3));p.drawRoundedRect(QRectF(8-i,8-i/2.,width()-16+2*i,height()-16+1.5*i),12+i,12+i);}}
void QuickPanel::enableControls(){auto pending=snapshot["_pending"].toObject();for(auto i=controls.begin();i!=controls.end();++i)Ui::enabled(i.value(),!(busy&&pending.isEmpty())&&!pending.contains(i.key())&&Ui::writable(snapshot,i.key()));}
void QuickPanel::setBusy(bool value){if(busy==value)return;busy=value;enableControls();if(snapshot["stale"].toBool()){Ui::text(message,"状态已过期，请刷新后重试");return;}if(QDateTime::currentMSecsSinceEpoch()>=feedbackUntil)Ui::text(message,value?"正在应用设置…":"更新于 "+QTime::currentTime().toString("HH:mm:ss"));}
void QuickPanel::feedback(bool ok,const QString &value){feedbackUntil=QDateTime::currentMSecsSinceEpoch()+6000;if(message->property("error").toBool()!=!ok){message->setProperty("error",!ok);message->style()->unpolish(message);message->style()->polish(message);}Ui::text(message,value);if(message->toolTip()!=value)message->setToolTip(value);}
void QuickPanel::updateState(const QJsonObject &state){
    updating=true;snapshot=state;auto features=state["features"].toObject();auto pending=state["_pending"].toObject();auto b=state["battery"].toObject();auto percent=state["batteryPercent"];Ui::text(battery,percent.isDouble()?QString::number(percent.toInt())+"%":"—");Ui::visible(batteryBar,percent.isDouble());Ui::progress(batteryBar,percent.toInt());Ui::text(supply,!state["ac"].isBool()?"供电状态未知":state["ac"].toBool()?"已连接电源":"电池供电");
    QStringList facts;if(b["temperatureC"].isDouble())facts<<QString("电池 %1°C").arg(b["temperatureC"].toDouble(),0,'f',0);auto gpu=features["gpu"].toObject();if(gpu["available"].toBool())for(auto opt:gpu["options"].toArray()){auto o=opt.toObject();if(o["value"]==gpu["value"])facts<<"GPU "+o["label"].toString();}detail->setText(facts.join("   ·   "));battery->setToolTip(b["healthExact"].isDouble()?QString("容量比 %1% · 满充 %2 Wh").arg(b["healthExact"].toDouble(),0,'f',1).arg(b["fullWh"].toDouble(),0,'f',1):QString());
    for(auto i=controls.begin();i!=controls.end();++i){auto f=features[i.key()].toObject();bool available=f["available"].toBool();int display=pending.contains(i.key())?pending[i.key()].toInt():f["value"].toInt(-1);auto tip=state["stale"].toBool()?"状态已过期："+state["readError"].toString():available?QString():f["error"].toString("暂不可用");if(i.value()->toolTip()!=tip)i.value()->setToolTip(tip);
        if(auto mode=qobject_cast<ModeSlider*>(i.value()))mode->setState(f,state["ac"].toBool(),pending.contains(i.key())?std::optional<int>(display):std::nullopt);
        else if(auto combo=qobject_cast<QComboBox*>(i.value()))Ui::combo(combo,f,display);
        else if(auto toggle=qobject_cast<QCheckBox*>(i.value()))Ui::checked(toggle,available&&display==1);
        else if(auto slider=qobject_cast<QSlider*>(i.value())){if(!Ui::editing(slider)){if(available)Ui::slider(slider,f,display);Ui::text(values[i.key()],available?QString::number(display)+"%":"—");}}
        else if(i.value()->property("choices").toBool()){
            auto options=f["options"].toArray();auto signature=QString::fromUtf8(QJsonDocument(options).toJson(QJsonDocument::Compact));
            if(i.value()->property("signature").toString()!=signature){auto l=qobject_cast<QHBoxLayout*>(i.value()->layout());while(auto item=l->takeAt(0)){delete item->widget();delete item;}for(auto option:options){auto o=option.toObject();int value=o["value"].toInt();auto label=i.key()=="charge"?QStringList{"标准","养护","快充"}.value(value):QStringList{"关","低","高","自动"}.value(value);auto button=new QRadioButton(label);button->setProperty("value",value);l->addWidget(button);QString id=i.key();connect(button,&QRadioButton::clicked,this,[this,id,value]{if(!updating)emit settingRequested(id,value);});}i.value()->setProperty("signature",signature);}
            for(auto button:i.value()->findChildren<QRadioButton*>())Ui::checked(button,available&&button->property("value").toInt()==display);
        }
    }
    if(state["stale"].toBool()){
        Ui::text(supply,"状态已过期");Ui::text(message,"状态已过期，请刷新后重试");message->setToolTip(state["readError"].toString());
    }else if(QDateTime::currentMSecsSinceEpoch()>=feedbackUntil){if(message->property("error").toBool()){message->setProperty("error",false);message->style()->unpolish(message);message->style()->polish(message);}Ui::text(message,"更新于 "+QTime::currentTime().toString("HH:mm:ss"));if(!message->toolTip().isEmpty())message->setToolTip(QString());}updating=false;enableControls();
}
void QuickPanel::showAt(const QRect &anchor){
    auto screen=QGuiApplication::screenAt(anchor.center());if(!screen)screen=QGuiApplication::primaryScreen();auto work=screen->availableGeometry();setFixedWidth(qMin(408,work.width()-16));setFixedHeight(qMin(680,work.height()-16));
    int x=qBound(work.left()+4,anchor.right()-width(),work.right()-width()-4);int y=anchor.top()-height()-4;
    if(y<work.top())y=anchor.bottom()+4;y=qBound(work.top()+4,y,work.bottom()-height()-4);move(x,y);show();raise();activateWindow();
}
bool QuickPanel::matches(const QJsonObject &state)const{
    auto f=state["features"].toObject();for(auto i=controls.begin();i!=controls.end();++i){auto s=f[i.key()].toObject();if(i.value()->isEnabled()!=(!busy&&Ui::writable(state,i.key())))return false;if(!s["available"].toBool())continue;
        if(auto m=qobject_cast<ModeSlider*>(i.value())){if(m->mode()!=s["value"].toInt())return false;}
        else if(auto c=qobject_cast<QComboBox*>(i.value())){if(c->currentData().toInt()!=s["value"].toInt())return false;}
        else if(auto t=qobject_cast<QCheckBox*>(i.value())){if(t->isChecked()!=(s["value"].toInt()==1))return false;}
        else if(auto v=qobject_cast<QSlider*>(i.value())){if(v->value()!=s["value"].toInt())return false;}
        else {int checked=-1;for(auto button:i.value()->findChildren<QRadioButton*>())if(button->isChecked())checked=button->property("value").toInt();if(checked!=s["value"].toInt())return false;}
    }return true;
}
bool QuickPanel::idleControlsEnabled(const QJsonObject &state)const{for(auto i=controls.begin();i!=controls.end();++i)if(i.value()->isEnabled()!=Ui::writable(state,i.key()))return false;return true;}

QIcon TrayController::appIcon(){QIcon icon;for(int size:{16,20,24,32,48,64,128,256}){QPixmap image(size,size);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.scale(size/32.,size/32.);p.setPen(Qt::NoPen);p.setBrush(QColor("#278773"));p.drawRoundedRect(QRectF(1,1,30,30),8,8);p.setBrush(Qt::white);p.drawRoundedRect(QRectF(8,8,16,4),1,1);p.drawRoundedRect(QRectF(14,9,4,16),1,1);icon.addPixmap(image);}return icon;}
TrayController::TrayController(Window *w,bool enabled):QObject(w),window(w),panel(new QuickPanel),icon(new QSystemTrayIcon(this)),menu(new QMenu){
    qApp->setWindowIcon(appIcon());window->setWindowIcon(appIcon());panel->setWindowIcon(appIcon());icon->setIcon(appIcon());
    auto open=menu->addAction("打开 ThinkVantage");connect(open,&QAction::triggered,this,[this]{panel->hide();window->showPage();});auto quick=menu->addAction("快捷面板");connect(quick,&QAction::triggered,this,&TrayController::showPanel);menu->addSeparator();auto refresh=menu->addAction("刷新状态");connect(refresh,&QAction::triggered,window,&Window::refreshState);auto admin=menu->addAction("管理员模式");connect(admin,&QAction::triggered,this,[this]{panel->hide();window->showPage();auto button=window->findChild<QPushButton*>("admin");if(button&&button->isVisible())button->click();});menu->addSeparator();auto quit=menu->addAction("退出 ThinkVantage");connect(quit,&QAction::triggered,window,&Window::requestQuit);icon->setContextMenu(menu);
    connect(icon,&QSystemTrayIcon::activated,this,[this](QSystemTrayIcon::ActivationReason reason){if(reason==QSystemTrayIcon::Trigger)showPanel();else if(reason==QSystemTrayIcon::DoubleClick){panel->hide();window->showPage();}});
    connect(panel,&QuickPanel::pageRequested,window,&Window::showPage);connect(panel,&QuickPanel::refreshRequested,window,&Window::refreshState);connect(panel,&QuickPanel::settingRequested,window,&Window::applySetting);
    connect(window,&Window::snapshotChanged,this,[this,admin](const QJsonObject &state){panel->updateState(state);admin->setVisible(!state["administrator"].toBool());updateTooltip();});connect(window,&Window::busyChanged,panel,&QuickPanel::setBusy);connect(window,&Window::operationFinished,panel,&QuickPanel::feedback);
    connect(window,&Window::hiddenToTray,this,[this]{if(!notified){notified=true;icon->showMessage("ThinkVantage 已在托盘运行","点击图标打开快捷面板；右键菜单可完全退出。",QSystemTrayIcon::Information,3500);}});
    active=enabled&&QSystemTrayIcon::isSystemTrayAvailable();if(active){icon->show();window->setTrayActive(true);qApp->setQuitOnLastWindowClosed(false);}updateTooltip();auto timer=new QTimer(this);timer->setInterval(30000);connect(timer,&QTimer::timeout,this,&TrayController::updateTooltip);timer->start();
}
TrayController::~TrayController(){icon->hide();delete panel;delete menu;}
void TrayController::updateTooltip(){SYSTEM_POWER_STATUS s{};QString tip="ThinkVantage";if(GetSystemPowerStatus(&s)){if(s.BatteryLifePercent<=100)tip+=QString(" · %1%").arg(s.BatteryLifePercent);tip+=s.ACLineStatus==255?" · 供电状态未知":s.ACLineStatus==1?" · 接电":" · 电池供电";}tip+="\n单击：快捷面板 · 双击：主窗口";if(icon->toolTip()!=tip)icon->setToolTip(tip);}
void TrayController::showPanel(){if(!active){window->showPage();return;}auto anchor=icon->geometry();if(anchor.isEmpty())anchor=QRect(QCursor::pos(),QSize(1,1));panel->updateState(window->displayedState());panel->setBusy(window->isApplying());panel->showAt(anchor);window->refreshState();}
QJsonObject TrayController::verify(const QString &capturePath){
    notified=true;auto state=window->currentState();panel->updateState(state);panel->setBusy(false);QJsonObject checks{{"initialValues",panel->matches(state)}};bool ok=active&&icon->isVisible()&&checks["initialValues"].toBool();panel->setBusy(true);checks["busyValues"]=panel->matches(state);ok=checks["busyValues"].toBool()&&ok;panel->setBusy(false);
    if(!capturePath.isEmpty()){
        auto screen=QGuiApplication::primaryScreen();auto work=screen->availableGeometry();panel->showAt(QRect(work.right()-28,work.bottom()-24,1,1));
        QEventLoop settle;QTimer::singleShot(150,&settle,&QEventLoop::quit);settle.exec();
        QFileInfo file(capturePath);QDir().mkpath(file.absolutePath());checks["capture"]=panel->grab().save(capturePath);checks["geometry"]=work.contains(panel->geometry());
        // A widget grab excludes Windows' popup shadow. Inspect its native style
        // and capture the composited screen, including the outside edges.
        auto hwnd=reinterpret_cast<HWND>(panel->winId());
        auto classStyle=GetClassLongPtrW(hwnd,GCL_STYLE);auto style=GetWindowLongPtrW(hwnd,GWL_STYLE);auto extended=GetWindowLongPtrW(hwnd,GWL_EXSTYLE);
        checks["noSystemShadow"]=panel->windowFlags().testFlag(Qt::NoDropShadowWindowHint)&&!(classStyle&CS_DROPSHADOW);
        checks["noNativeFrame"]=!(style&(WS_BORDER|WS_DLGFRAME|WS_THICKFRAME))&&!(extended&(WS_EX_WINDOWEDGE|WS_EX_CLIENTEDGE|WS_EX_DLGMODALFRAME));
        const auto bounds=panel->frameGeometry().adjusted(-12,-12,12,12).intersected(screen->geometry());
        auto desktop=screen->grabWindow(0,bounds.x(),bounds.y(),bounds.width(),bounds.height());
        checks["desktopCapture"]=!desktop.isNull()&&desktop.save(file.dir().filePath(file.completeBaseName()+"-desktop.png"));
        for(const auto &check:checks)ok=check.toBool()&&ok;
    }
    bool quietRead=true;window->refreshState();QElapsedTimer readTimer;readTimer.start();while(window->isBusy()&&readTimer.elapsed()<25000){quietRead=quietRead&&panel->idleControlsEnabled(state);QApplication::processEvents(QEventLoop::AllEvents,25);QThread::msleep(10);}quietRead=quietRead&&!window->isBusy();ok=quietRead&&ok;state=window->currentState();
    QJsonObject adjustment;
    if(QCoreApplication::arguments().contains("--verify-adjustment")){
        auto volume=state["features"].toObject()["volume"].toObject();
        if(volume["available"].toBool()){
            int baseline=volume["value"].toInt(),target=baseline==100?99:baseline+1;
            QString lastOperation;bool operationOk=false;int requests=0;QJsonArray requested,snapshots;auto op=connect(window,&Window::operationFinished,this,[&](bool result,const QString &message){operationOk=result;lastOperation=QString::number(result)+": "+message;});auto req=connect(panel,&QuickPanel::settingRequested,this,[&](const QString &id,int value){if(id=="volume"){++requests;requested.append(value);}});auto snap=connect(window,&Window::snapshotChanged,this,[&](const QJsonObject &s){snapshots.append(s["features"].toObject()["volume"].toObject()["value"]);});
            auto wait=[this,baseline,target](int value,bool change=false){QElapsedTimer timer;timer.start();do{QApplication::processEvents(QEventLoop::AllEvents,25);int actual=window->currentState()["features"].toObject()["volume"].toObject()["value"].toInt(-1);bool match=change?(actual>=0&&(target>baseline?actual>baseline:actual<baseline)):actual==value;if(!window->isBusy()&&match)return true;QThread::msleep(10);}while(timer.elapsed()<10000);return false;};
            window->refreshState();bool reading=window->isBusy()&&!window->isApplying();auto sliders=panel->findChildren<QSlider*>();auto slider=*std::find_if(sliders.begin(),sliders.end(),[](QSlider *s){return s->accessibleName()=="音量";});slider->setSliderDown(true);slider->setValue(target);slider->setSliderDown(false);bool queued=reading&&window->displayedState()["_pending"].toObject().contains("volume");bool changed=wait(target,true)&&operationOk;
            int actual=window->currentState()["features"].toObject()["volume"].toObject()["value"].toInt(-1);
            // Always restore the original value, including a failed adjustment.
            QElapsedTimer idle;idle.start();while(window->isBusy()&&idle.elapsed()<25000){QApplication::processEvents(QEventLoop::AllEvents,25);QThread::msleep(10);}
            window->applySetting("volume",baseline);bool restored=wait(baseline);
            adjustment={{"changed",changed},{"restored",restored},{"queuedDuringRead",queued},{"before",baseline},{"target",target},{"actual",actual},{"requests",requests},{"requestedValues",requested},{"snapshots",snapshots},{"lastOperation",lastOperation},{"after",window->currentState()["features"].toObject()["volume"].toObject()["value"]}};disconnect(op);disconnect(req);disconnect(snap);ok=changed&&restored&&queued&&panel->matches(window->currentState())&&ok;
        }
    }
    panel->hide();window->showPage(1);QApplication::processEvents();ok=window->isVisible()&&window->findChild<QListWidget*>()->currentRow()==1&&ok;window->close();ok=!window->isVisible()&&icon->isVisible()&&ok;
    auto nativeRect=icon->geometry();return {{"ok",ok},{"checks",checks},{"trayRegistered",active&&icon->isVisible()},{"nativeIconRect",QJsonArray{nativeRect.x(),nativeRect.y(),nativeRect.width(),nativeRect.height()}},{"controls",panel->controlCount()},{"quietRead",quietRead},{"closeToTray",!window->isVisible()},{"adjustment",adjustment},{"snapshot",state}};
}
