#include "window.h"
#include "window_widgets.h"
#include "backend.h"
#include "mode_slider.h"
#include <QDesktopServices>
#include <QJsonDocument>

namespace {
QLabel *label(const QString &text,const char *name=nullptr){auto w=new QLabel(text);if(name)w->setObjectName(name);w->setWordWrap(true);return w;}
QVBoxLayout *body(QWidget *p){return qobject_cast<QVBoxLayout*>(p->layout());}
void insert(QWidget *p,QWidget *w){body(p)->insertWidget(body(p)->count()-1,w);}
void divider(QWidget *p){
    int n=p->property("rows").toInt();p->setProperty("rows",n+1);
    if(!n)return;
    auto line=new QFrame;line->setObjectName("divider");line->setFixedHeight(1);insert(p,line);
}
QIcon navigationIcon(int index){
    QPixmap image(48,48);image.fill(Qt::transparent);QPainter p(&image);p.setRenderHint(QPainter::Antialiasing);p.scale(2,2);p.setPen(QPen(QColor("#a4bcb8"),1.6,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
    if(index==0){p.drawRoundedRect(QRectF(3,6,17,12),3,3);p.drawLine(22,10,22,14);p.drawLine(11,8,9,12);p.drawLine(9,12,14,12);p.drawLine(14,12,12,16);}
    if(index==1){p.drawRoundedRect(QRectF(2,5,20,14),3,3);for(int y:{9,12})for(int x:{6,10,14,18})p.drawPoint(x,y);p.drawLine(7,16,17,16);}
    if(index==2){p.drawRoundedRect(QRectF(2,3,20,14),2,2);p.drawLine(12,17,12,21);p.drawLine(7,21,17,21);p.drawEllipse(QPointF(12,9),2.5,2.5);}
    if(index==3){p.drawLine(4,9,7,9);p.drawLine(7,9,12,5);p.drawLine(12,5,12,19);p.drawLine(12,19,7,15);p.drawLine(7,15,4,15);p.drawLine(4,15,4,9);p.drawArc(QRectF(12,7,8,10),-60*16,120*16);}
    if(index==4){p.drawEllipse(QRectF(3,3,18,18));p.drawPoint(12,8);p.drawLine(12,11,12,17);}
    return QIcon(image);
}
}

Window::Window(const QString &path,bool smokeTest):capturePath(path),smoke(smokeTest){
    setWindowTitle("ThinkVantage");resize(1140,820);setMinimumSize(960,660);
    auto root=new QWidget;setCentralWidget(root);auto outer=new QHBoxLayout(root);outer->setContentsMargins(0,0,0,0);outer->setSpacing(0);
    auto sidebar=new QFrame;sidebar->setObjectName("sidebar");sidebar->setFixedWidth(200);auto nav=new QVBoxLayout(sidebar);nav->setContentsMargins(16,28,16,22);nav->setSpacing(6);
    auto identity=new QHBoxLayout;auto logo=label("T","logo");logo->setFixedSize(34,34);logo->setAlignment(Qt::AlignCenter);identity->addWidget(logo);auto brand=new QVBoxLayout;brand->setSpacing(1);brand->addWidget(label("ThinkVantage","brand"));brand->addWidget(label("设备控制台","subbrand"));identity->addLayout(brand);identity->addStretch();nav->addLayout(identity);nav->addSpacing(32);
    auto list=new QListWidget;list->setObjectName("navigation");list->setSpacing(5);list->setIconSize(QSize(20,20));
    QStringList names{"电源与性能","键盘与触控板","显示与感知","声音","设备信息"};for(int i=0;i<names.size();++i)list->addItem(new QListWidgetItem(navigationIcon(i),names[i]));nav->addWidget(list,1);
    auto sideNote=label("ThinkVantage  0.6.0\nWindows 11","sideNote");nav->addWidget(sideNote);outer->addWidget(sidebar);
    auto content=new QWidget;auto column=new QVBoxLayout(content);column->setContentsMargins(30,20,26,14);column->setSpacing(14);
    auto header=new QHBoxLayout;header->setSpacing(8);device=label("正在连接设备…","device");header->addWidget(device,1);
    admin=new QPushButton("管理员模式");admin->setObjectName("admin");admin->setToolTip("以管理员身份重新打开，使用需要提升权限的设备设置");connect(admin,&QPushButton::clicked,this,[this]{if(Backend::relaunchElevated())requestQuit();else status->setText("未完成管理员启动。");});header->addWidget(admin);
    refresh=new QPushButton("刷新");refresh->setToolTip("重新读取设备状态");connect(refresh,&QPushButton::clicked,this,&Window::reload);header->addWidget(refresh);column->addLayout(header);
    pages=new QStackedWidget;column->addWidget(pages,1);status=label("正在读取设备状态…","status");column->addWidget(status);outer->addWidget(content,1);connect(list,&QListWidget::currentRowChanged,pages,&QStackedWidget::setCurrentIndex);

    auto power=page("电源与性能","查看电池状态，调整性能、充电与供电行为。");
    auto batteryCard=new QFrame;batteryCard->setObjectName("batteryCard");batteryCard->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);auto bc=new QVBoxLayout(batteryCard);bc->setContentsMargins(22,20,22,16);bc->setSpacing(9);
    auto bh=new QHBoxLayout;auto bt=new QVBoxLayout;bt->setSpacing(3);bt->addWidget(label("电池","eyebrow"));batterySummary=label("正在读取…","batteryTitle");bt->addWidget(batterySummary);batteryDetail=label("","batteryCaption");bt->addWidget(batteryDetail);bh->addLayout(bt,1);
    auto therm=new QVBoxLayout;therm->setSpacing(6);temperature=label("—","temperature");temperature->setAlignment(Qt::AlignRight);therm->addWidget(temperature);temperatureBar=new SmoothProgress;temperatureBar->setRange(0,60);temperatureBar->setTextVisible(false);temperatureBar->setFixedSize(110,5);therm->addWidget(temperatureBar,0,Qt::AlignRight);bh->addLayout(therm);bc->addLayout(bh);
    batteryChart=new BatteryChart;bc->addWidget(batteryChart);
    auto all=new QPushButton("电池详情  ▸");all->setObjectName("textButton");bc->addWidget(all,0,Qt::AlignLeft);
    batteryExtra=new QWidget;auto details=new QGridLayout(batteryExtra);details->setContentsMargins(0,6,0,6);details->setHorizontalSpacing(24);details->setVerticalSpacing(9);
    int cell=0;for(const auto &item:QList<QPair<QString,QString>>{{"charge","充电状态"},{"health","容量比"},{"cycles","充电循环"},{"chemistry","电池类型"},{"adapter","电源适配器"},{"voltage","电压"},{"current","电流"},{"manufacturer","制造商"},{"designVoltage","设计电压"}}){auto pair=new QWidget;auto pl=new QHBoxLayout(pair);pl->setContentsMargins(0,0,0,0);pl->addWidget(label(item.second,"description"));auto value=label("—");pl->addWidget(value,1,Qt::AlignRight);batteryRows[item.first]=value;details->addWidget(pair,cell/2,cell%2);++cell;}
    bc->addWidget(batteryExtra);batteryExtra->hide();connect(all,&QPushButton::clicked,this,[this,all]{bool shown=batteryExtra->isHidden();batteryExtra->setVisible(shown);all->setText(shown?"收起详情  ▾":"电池详情  ▸");});insert(power,batteryCard);
    auto performance=section(power,"性能与图形");
    row(performance,"its","整机性能","极客性能需要连接电源。");row(performance,"power","Windows 电源模式","");row(performance,"gpu","GPU 模式","仅核显模式会先检查独显占用。");
    row(performance,"oc","GPU 超频","开启时会将自适应性能切换为性能档。",false,"switch");
    auto oc=advanced(performance,"超频参数");ocDetails=label("正在读取频率范围…","description");insert(oc,ocDetails);row(oc,"ocCore","核心偏移","",true);row(oc,"ocMemory","显存偏移","",true);
    auto charging=section(power,"充电策略");row(charging,"charge","充电方式","长期接电时可选择电池养护。",false,"choices");chargeThreshold=label("","inlineNote");insert(charging,chargeThreshold);row(charging,"night","夜间充电","由固件管理夜间充电节奏。",false,"switch");
    auto behavior=section(power,"开机与 USB 供电");row(behavior,"flip","开盖开机","关机后打开屏幕盖即可开机。",false,"switch");row(behavior,"usbAc","接电时 USB 常供电","",false,"switch");row(behavior,"usbDc","电池供电时 USB 常供电","会额外消耗电池电量。",false,"switch");

    auto input=page("键盘与触控板","灯光、键位、屏幕提示和触控板集中设置。");
    auto lighting=section(input,"键盘背光");row(lighting,"backlight","亮度","Fn + Space",false,"choices");row(lighting,"backlightRemember","记住亮度选择","由设备固件保存。",false,"switch");
    auto keyboard=section(input,"键位与屏幕提示");row(keyboard,"fnlock","Fn Lock","Fn + Esc · 切换 F1–F12 的默认功能。",false,"switch");row(keyboard,"fnCtrlSwap","Fn 与 Ctrl 互换","",false,"switch");row(keyboard,"capsOsd","Caps Lock 提示","切换大小写锁定时显示图标。",false,"switch");row(keyboard,"numOsd","Num Lock 提示","切换数字锁定时显示图标。",false,"switch");
    auto touchpad=section(input,"触控板");row(touchpad,"touchpad","启用触控板","Fn + M",false,"switch");linkRow(touchpad,"手势与灵敏度","","ms-settings:devices-touchpad");
    linkRow(input,"键盘辅助功能","","ms-settings:easeofaccess-keyboard");

    auto display=page("显示与感知","调整屏幕，让设备在你靠近或离开时自动响应。");
    auto screen=section(display,"屏幕");row(screen,"brightness","亮度","",true);row(screen,"refresh","刷新率","切换后可预览并恢复。");linkRow(screen,"显示与 HDR","","ms-settings:display");linkRow(screen,"高级显示","","ms-settings:display-advanced");linkRow(screen,"屏幕与睡眠","","ms-settings:powersleep");
    auto presence=section(display,"人体感知");presenceInfo=label("正在读取感知设备…","inlineNote");insert(presence,presenceInfo);
    row(presence,"presenceLogin","靠近唤醒","配合 Windows Hello 可使用人脸登录。",false,"switch");
    auto login=advanced(presence,"唤醒设置");row(login,"loginAuto","距离调整","",false,"choices");row(login,"loginDistance","检测距离","",false,"choices");linkRow(login,"设置 Windows Hello","","ms-settings:signinoptions");
    row(presence,"presenceLock","离开锁定","离开时变暗、关闭屏幕或锁定设备。",false,"switch");
    auto lock=advanced(presence,"锁定设置");row(lock,"lockTimer","响应速度","实际时间由原厂服务决定。",false,"choices");row(lock,"lockAuto","距离调整","",false,"choices");row(lock,"lockDistance","检测距离","",false,"choices");row(lock,"cameraAssist","人脸识别辅助","使用本机相机，减少短暂移动造成的误锁。",false,"switch");linkRow(lock,"相机访问权限","","ms-settings:privacy-webcam");auto note=label("离开锁定可能优先于 Windows 屏幕与睡眠时间。","inlineNote");insert(lock,note);
    row(presence,"presenceVideo","视频自动暂停","离开暂停，返回继续；需播放器支持。",false,"switch");

    auto sound=page("声音","调整默认扬声器与麦克风。");auto audio=section(sound,"音频设备");row(audio,"volume","音量","",true);row(audio,"speakerMute","扬声器静音","",false,"switch");row(audio,"micMute","麦克风静音","",false,"switch");linkRow(audio,"设备与音效","","ms-settings:sound");
    auto about=page("设备信息","设备支持情况与诊断信息。");auto information=section(about,"关于 ThinkVantage");auto intro=label("独立开发的本地设备设置工具。联想功能依赖本机原厂驱动与服务。","about");insert(information,intro);auto version=label("版本 0.6.0  ·  Windows 11 x64","inlineNote");insert(information,version);
    auto diagnostic=section(about,"诊断");auto exportButton=new QPushButton("导出诊断 JSON");exportButton->setObjectName("primary");auto exportLine=new QWidget;auto el=new QHBoxLayout(exportLine);el->setContentsMargins(20,12,20,12);el->addWidget(label("导出设备能力与当前设置，便于排查问题。","description"),1);el->addWidget(exportButton);insert(diagnostic,exportLine);
    auto raw=advanced(diagnostic,"查看原始数据");diagnostics=new QPlainTextEdit;diagnostics->setReadOnly(true);diagnostics->setMinimumHeight(300);diagnostics->installEventFilter(this);insert(raw,diagnostics);
    connect(exportButton,&QPushButton::clicked,this,[this]{auto path=QFileDialog::getSaveFileName(this,"导出诊断信息","ThinkVantage-diagnostics.json","JSON (*.json)");if(path.isEmpty())return;QFile f(path);if(!f.open(QIODevice::WriteOnly)||f.write(QJsonDocument(snapshot).toJson())<0)QMessageBox::warning(this,"导出失败",f.errorString());else status->setText("诊断信息已导出。");});

    setStyleSheet(R"(
        QWidget { color:#26313d; font-family:'Microsoft YaHei UI'; font-size:13px; }
        QMainWindow, QWidget#page { background:#f4f6f8; }
        QFrame#sidebar { background:#192c31; }
        QLabel#logo { color:#ffffff; background:#2b8c78; border-radius:9px; font-size:23px; font-weight:700; }
        QLabel#brand { color:#f4f8f7; font-size:16px; font-weight:600; }
        QLabel#subbrand, QLabel#sideNote { color:#8fa6a5; font-size:11px; }
        QLabel#sideNote { line-height:1.6; }
        QListWidget#navigation { background:transparent; border:0; outline:0; }
        QListWidget#navigation::item { height:43px; border-radius:7px; padding-left:10px; color:#b9cac8; }
        QListWidget#navigation::item:selected { background:#29474b; color:#effaf6; font-weight:600; }
        QListWidget#navigation::item:hover:!selected { background:#213b40; }
        QLabel#device { color:#71808c; font-size:11px; }
        QLabel#heading { color:#202e39; font-size:25px; font-weight:600; }
        QLabel#pageDescription { color:#7a8691; font-size:12px; margin-bottom:6px; }
        QLabel#sectionTitle { color:#263e44; font-size:14px; font-weight:600; }
        QLabel#description { color:#7a8792; font-size:11px; }
        QLabel#rowTitle { font-size:13px; }
        QLabel#reason { color:#a46e42; font-size:11px; }
        QLabel#status { color:#7a8990; font-size:10px; padding-top:2px; }
        QFrame#settingsGroup { background:#ffffff; border:1px solid #e1e7eb; border-radius:11px; }
        QFrame#divider { background:#eef1f4; border:0; margin-left:20px; margin-right:20px; }
        QFrame#batteryCard { background:qlineargradient(x1:0,y1:0,x2:1,y2:0,stop:0 #e5f2ed,stop:1 #f1f7f4); border:1px solid #d6e7df; border-radius:11px; }
        QLabel#eyebrow { color:#598375; font-size:11px; }
        QLabel#batteryTitle { color:#245d4c; font-size:25px; font-weight:600; }
        QLabel#batteryCaption { color:#628476; font-size:11px; }
        QLabel#temperature { color:#36715e; font-size:16px; font-weight:600; }
        QProgressBar { background:#d7e5de; border:0; border-radius:2px; }
        QProgressBar::chunk { background:#79ae9a; border-radius:2px; }
        QLabel#inlineNote { color:#7a8c90; font-size:11px; padding:7px 20px; }
        QLabel#about { color:#60717b; font-size:12px; padding:8px 20px; }
        QPushButton { background:#ffffff; border:1px solid #dbe3e8; padding:7px 12px; border-radius:6px; font-size:11px; }
        QPushButton:hover { border-color:#78ac9d; background:#f2f8f5; }
        QPushButton:focus { border-color:#278773; }
        QPushButton:disabled { color:#a3adb6; background:#f5f7f8; }
        QPushButton#admin { background:#e8f1ed; border-color:#d5e6df; color:#367460; }
        QPushButton#primary { background:#278773; border-color:#278773; color:white; }
        QPushButton#primary:hover { background:#207661; }
        QPushButton#textButton, QPushButton#advanced { text-align:left; color:#408674; background:transparent; border:0; padding:6px 0; font-size:11px; }
        QPushButton#textButton:hover, QPushButton#advanced:hover { color:#1c6855; }
        QRadioButton { background:#f4f6f8; border:1px solid transparent; border-radius:5px; padding:7px 11px; font-size:11px; spacing:0; }
        QRadioButton:hover { background:#eaf2ef; }
        QRadioButton:checked { background:#e0f0e9; border-color:#bedbcf; color:#256e56; }
        QRadioButton:focus { border-color:#278773; }
        QRadioButton:disabled { color:#a1acb6; background:#f4f6f8; border-color:transparent; }
        QRadioButton:checked:disabled { background:#e9efec; color:#a0b0a7; }
        QRadioButton::indicator { width:0; height:0; }
        QComboBox { background:#f7f9fa; border:1px solid #e1e6eb; border-radius:6px; padding:7px 9px; font-size:11px; }
        QComboBox:focus { border-color:#278773; }
        QComboBox:disabled { color:#a1acb6; background:#f4f6f8; }
        QComboBox::drop-down { width:22px; border:0; }
        QComboBox::down-arrow { image:none; }
        QComboBox QAbstractItemView { background:white; selection-background-color:#e0f0e9; selection-color:#256e56; padding:5px; border:1px solid #dce5df; }
        QSlider::groove:horizontal { background:#e1e7eb; height:4px; border-radius:2px; }
        QSlider::sub-page:horizontal { background:#278773; border-radius:2px; }
        QSlider::handle:horizontal { background:#ffffff; border:2px solid #278773; width:12px; margin:-5px 0; border-radius:7px; }
        QSlider::handle:horizontal:disabled { border-color:#bfcbd0; }
        QSlider::sub-page:horizontal:disabled { background:#bfcbd0; }
        QScrollArea { border:0; background:transparent; }
        QScrollBar:vertical { width:6px; background:transparent; margin:0; }
        QScrollBar::handle:vertical { background:#cdd6dc; border-radius:3px; min-height:28px; }
        QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
        QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background:transparent; }
        QPlainTextEdit { background:#f7f9fa; border:1px solid #e2e8eb; border-radius:6px; padding:9px; font-family:Consolas; font-size:11px; }
    )");
    list->setCurrentRow(0);auto timer=new QTimer(this);timer->setInterval(15000);connect(timer,&QTimer::timeout,this,[this]{if(isVisible()&&!isMinimized()&&!isBusy()&&QApplication::mouseButtons()==Qt::NoButton)reload();});timer->start();QTimer::singleShot(0,this,&Window::reload);
}

QWidget *Window::page(const QString &title,const QString &description){
    auto scroll=new QScrollArea;scroll->setWidgetResizable(true);scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);auto p=new QWidget;p->setObjectName("page");auto layout=new QVBoxLayout(p);layout->setContentsMargins(0,7,10,8);layout->setSpacing(14);layout->addWidget(label(title,"heading"));layout->addWidget(label(description,"pageDescription"));layout->addStretch();scroll->setWidget(p);pages->addWidget(scroll);return p;
}
QWidget *Window::section(QWidget *p,const QString &title,const QString &description){
    auto group=new QFrame;group->setObjectName("settingsGroup");group->setSizePolicy(QSizePolicy::Preferred,QSizePolicy::Maximum);auto layout=new QVBoxLayout(group);layout->setContentsMargins(0,0,0,4);layout->setSpacing(0);auto header=new QWidget;auto h=new QVBoxLayout(header);h->setContentsMargins(20,17,20,7);h->setSpacing(4);h->addWidget(label(title,"sectionTitle"));if(!description.isEmpty())h->addWidget(label(description,"description"));layout->addWidget(header);layout->addStretch();insert(p,group);return group;
}
QWidget *Window::advanced(QWidget *p,const QString &title){
    auto button=new QPushButton(title+"  ▸");button->setObjectName("advanced");button->setCheckable(true);button->setSizePolicy(QSizePolicy::Maximum,QSizePolicy::Fixed);
    auto line=new QWidget;auto l=new QHBoxLayout(line);l->setContentsMargins(20,0,20,4);l->addWidget(button);l->addStretch();insert(p,line);
    auto panel=new QWidget;panel->setObjectName("advancedPanel");auto layout=new QVBoxLayout(panel);layout->setContentsMargins(12,0,12,10);layout->setSpacing(0);layout->addStretch();insert(p,panel);panel->hide();button->setProperty("panel",QVariant::fromValue<QObject*>(panel));
    connect(button,&QPushButton::toggled,this,[button,panel,title](bool shown){panel->setVisible(shown);button->setText(title+(shown?"  ▾":"  ▸"));});return panel;
}
void Window::row(QWidget *p,const QString &id,const QString &title,const QString &description,bool slider,const QString &kind){
    divider(p);auto line=new QWidget;line->setObjectName("settingRow");line->setProperty("settingId",id);auto layout=new QHBoxLayout(line);layout->setContentsMargins(20,12,20,12);layout->setSpacing(18);
    auto texts=new QVBoxLayout;texts->setSpacing(3);texts->addWidget(label(title,"rowTitle"));if(!description.isEmpty())texts->addWidget(label(description,"description"));auto hint=label("正在检测…","reason");texts->addWidget(hint);hints[id]=hint;layout->addLayout(texts,1);
    if(id=="its"){
        auto mode=new ModeSlider;mode->setFixedWidth(340);mode->setEnabled(false);controls[id]=mode;layout->addWidget(mode);connect(mode,&ModeSlider::modeRequested,this,[this](int value){setValue("its",value);});
    }else if(kind=="switch"){
        auto toggle=new Toggle;toggle->setAccessibleName(title);toggle->setEnabled(false);controls[id]=toggle;layout->addWidget(toggle);connect(toggle,&QCheckBox::clicked,this,[this,id](bool value){setValue(id,value?1:0);});
    }else if(kind=="choices"){
        auto choices=new QWidget;choices->setProperty("choices",true);choices->setEnabled(false);choices->setAccessibleName(title);controls[id]=choices;auto grid=new QGridLayout(choices);grid->setContentsMargins(0,0,0,0);grid->setSpacing(4);
        auto choiceColumn=new QVBoxLayout;choiceColumn->setSpacing(5);choiceColumn->addWidget(choices);layout->addLayout(choiceColumn);
        if(id=="loginDistance"||id=="lockDistance"){
            auto unit=new ChoiceCombo;unit->addItems({"米","英尺"});unit->setFixedWidth(70);unit->setAccessibleName(title+"单位");unit->setCurrentIndex(QSettings().value("presence/"+id+"/unit",0).toInt());choiceColumn->addWidget(unit,0,Qt::AlignRight);unitControls[id]=unit;
            connect(unit,&QComboBox::activated,this,[this,id](int index){QSettings().setValue("presence/"+id+"/unit",index);updateState(snapshot);});
        }
    }else if(slider){
        auto box=new QWidget;auto sl=new QHBoxLayout(box);sl->setContentsMargins(0,0,0,0);sl->setSpacing(10);auto v=new SmoothSlider(Qt::Horizontal);v->setAccessibleName(title);v->setRange(0,100);v->setFixedWidth(170);auto value=new QLabel("—");value->setAlignment(Qt::AlignRight|Qt::AlignVCenter);value->setFixedWidth(id.startsWith("oc")?76:38);sl->addWidget(v);sl->addWidget(value);layout->addWidget(box);controls[id]=v;v->setEnabled(false);v->setProperty("valueLabel",QVariant::fromValue<QObject*>(value));
        auto debounce=new QTimer(v);debounce->setSingleShot(true);debounce->setInterval(450);v->setProperty("debounce",QVariant::fromValue<QObject*>(debounce));connect(v,&QSlider::valueChanged,this,[this,id,value,debounce](int n){value->setText(QString::number(n)+(id.startsWith("oc")?" MHz":"%"));if(!loading)debounce->start();});connect(debounce,&QTimer::timeout,this,[this,id,v]{if(!loading&&!v->isSliderDown())setValue(id,v->value());});connect(v,&QSlider::sliderReleased,this,[this,id,v,debounce]{debounce->stop();if(!loading)setValue(id,v->value());});
    }else{
        auto combo=new ChoiceCombo;combo->setAccessibleName(title);combo->setFixedWidth(190);combo->addItem("正在检测");combo->setEnabled(false);controls[id]=combo;layout->addWidget(combo);connect(combo,&QComboBox::activated,this,[this,id,combo](int index){setValue(id,combo->itemData(index).toInt());});
    }
    insert(p,line);
}
void Window::linkRow(QWidget *p,const QString &title,const QString &description,const QString &uri){
    auto previous=body(p)->itemAt(body(p)->count()-2)->widget();QHBoxLayout *layout=nullptr;
    if(previous&&previous->objectName()=="linkStrip")layout=qobject_cast<QHBoxLayout*>(previous->layout());
    if(!layout){auto line=new QWidget;line->setObjectName("linkStrip");layout=new QHBoxLayout(line);layout->setContentsMargins(20,6,20,8);layout->setSpacing(20);layout->addStretch();insert(p,line);}
    auto button=new QPushButton(title+"  ↗");button->setObjectName("textButton");button->setToolTip(description.isEmpty()?"打开 Windows 设置":description);layout->insertWidget(layout->count()-1,button);connect(button,&QPushButton::clicked,this,[this,uri]{if(!QDesktopServices::openUrl(QUrl(uri)))status->setText("无法打开 Windows 设置。");});
}
