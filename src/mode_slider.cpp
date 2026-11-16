#include "mode_slider.h"
#include "ui_update.h"
#include <QJsonArray>
#include <cmath>

namespace {
QString titleFor(int mode){switch(mode){case 1:return "节能";case 0:return "自适应";case 3:return "性能";case 4:return "Ultra";default:return "正在读取";}}
QString captionFor(int mode){switch(mode){case 1:return "安静运行 · 降低功耗";case 0:return "自动平衡性能与能耗";case 3:return "优先释放整机性能";case 4:return "极客性能 · 需要连接电源";default:return "性能模式";}}
QColor mix(const QColor &a,const QColor &b,qreal t){return QColor::fromRgbF(a.redF()+(b.redF()-a.redF())*t,a.greenF()+(b.greenF()-a.greenF())*t,a.blueF()+(b.blueF()-a.blueF())*t);}
struct Palette {QColor start,end,text;};
qreal warmthFor(int mode){switch(mode){case 0:return 1;case 3:return 2;case 4:return 3;default:return 0;}}
Palette paletteAt(qreal warmth){
    static const Palette stops[]={
        {QColor("#4789ee"),QColor("#2867d8"),QColor("#245bb8")},
        {QColor("#39bac5"),QColor("#168e9b"),QColor("#0d737e")},
        {QColor("#f5ce70"),QColor("#dda644"),QColor("#946520")},
        {QColor("#f28d32"),QColor("#dc2d2c"),QColor("#b52a26")}
    };
    warmth=qBound(0.,warmth,3.);int lower=qMin(int(warmth),2);qreal t=warmth-lower;
    return {mix(stops[lower].start,stops[lower+1].start,t),mix(stops[lower].end,stops[lower+1].end,t),mix(stops[lower].text,stops[lower+1].text,t)};
}
}
ModeSlider::ModeSlider(QWidget *parent):QAbstractSlider(parent){
    setOrientation(Qt::Horizontal);setFocusPolicy(Qt::StrongFocus);setMouseTracking(true);setCursor(Qt::PointingHandCursor);setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Fixed);setFixedHeight(100);setAccessibleName("整机性能模式");setTracking(false);clock.start();
    slide.setDuration(210);slide.setEasingCurve(QEasingCurve::OutCubic);connect(&slide,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){position=v.toReal();update(track().adjusted(-6,-8,6,8).toAlignedRect());});
    ultra.setDuration(230);ultra.setEasingCurve(QEasingCurve::OutCubic);connect(&ultra,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){ultraAmount=v.toReal();update();});
    warmth.setDuration(230);warmth.setEasingCurve(QEasingCurve::OutCubic);connect(&warmth,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){warmthAmount=v.toReal();update();});
    particles.setInterval(33);connect(&particles,&QTimer::timeout,this,[this]{if(window()->isMinimized()){particles.stop();return;}update(track().adjusted(0,-2,0,2).toAlignedRect());});
}
QRectF ModeSlider::track()const{return {12.,38.,qreal(width()-24),26.};}
qreal ModeSlider::stopPosition(int index)const{return modes.size()>1?qreal(index)/(modes.size()-1):0.;}
int ModeSlider::indexAt(qreal x)const{auto t=track();qreal start=t.left()+16,span=t.width()-32;return qBound(0,qRound((x-start)/qMax(1.,span)*(modes.size()-1)),modes.size()-1);}
void ModeSlider::setState(const QJsonObject &feature,bool ac,std::optional<int> requested){
    QList<int> next;for(int mode:{1,0,3,4})for(auto option:feature["options"].toArray())if(option.toObject()["value"].toInt(-1)==mode){next<<mode;break;}
    bool ready=feature["available"].toBool()&&next.contains(feature["value"].toInt(-1));int actual=ready?feature["value"].toInt():-1;bool changed=modes!=next||confirmed!=actual||known!=ready||onAc!=ac||pending!=requested.has_value();
    if(!changed&&(!requested||mode()==*requested))return;
    modes=next;confirmed=actual;known=ready;onAc=ac;pending=requested.has_value();
    {QSignalBlocker block(this);setRange(0,qMax(0,int(modes.size())-1));}
    if(!isSliderDown()&&!keyboardEditing){int index=modes.indexOf(requested.value_or(confirmed));if(index>=0)preview(index);else update();}
    setAccessibleDescription(ready?titleFor(mode())+"，"+captionFor(mode()):feature["error"].toString("当前不可用"));update();updateAnimation();
}
void ModeSlider::preview(int index,bool animate){
    if(index<0||index>=modes.size())return;
    bool changed=value()!=index||!known;{QSignalBlocker block(this);setValue(index);setSliderPosition(index);}
    qreal target=stopPosition(index),glow=modes[index]==4?1.:0.;
    if(std::abs(position-target)>.0001){slide.stop();++transitions;if(animate&&isVisible()&&Ui::motionEnabled()){slide.setStartValue(position);slide.setEndValue(target);slide.start();}else position=target;}
    if(std::abs(ultraAmount-glow)>.0001){ultra.stop();if(animate&&isVisible()&&Ui::motionEnabled()){ultra.setStartValue(ultraAmount);ultra.setEndValue(glow);ultra.start();}else ultraAmount=glow;}
    const auto targetWarmth=warmthFor(modes[index]);
    if(std::abs(warmthAmount-targetWarmth)>.0001){warmth.stop();if(animate&&isVisible()&&Ui::motionEnabled()){warmth.setStartValue(warmthAmount);warmth.setEndValue(targetWarmth);warmth.start();}else warmthAmount=targetWarmth;}
    if(changed||slide.state()!=QAbstractAnimation::Running)update();updateAnimation();
}
void ModeSlider::commit(){
    if(!known||!isEnabled()||pending)return;
    if(mode()==4&&!onAc){setToolTip("Ultra 极客性能需要连接电源");preview(modes.indexOf(confirmed));return;}
    if(mode()!=confirmed)emit modeRequested(mode());
}
void ModeSlider::mousePressEvent(QMouseEvent *e){if(e->button()!=Qt::LeftButton||!known||!isEnabled()||!track().adjusted(-2,-13,2,13).contains(e->position())){e->ignore();return;}setFocus(Qt::MouseFocusReason);setSliderDown(true);preview(indexAt(e->position().x()));e->accept();}
void ModeSlider::mouseMoveEvent(QMouseEvent *e){if(isSliderDown())preview(indexAt(e->position().x()));else if(known)setToolTip(titleFor(modes.value(indexAt(e->position().x()),-1))+" · "+captionFor(modes.value(indexAt(e->position().x()),-1)));}
void ModeSlider::mouseReleaseEvent(QMouseEvent *e){if(e->button()==Qt::LeftButton&&isSliderDown()){preview(indexAt(e->position().x()));setSliderDown(false);commit();e->accept();}else e->ignore();}
void ModeSlider::keyPressEvent(QKeyEvent *e){
    if(e->key()==Qt::Key_Escape&&(isSliderDown()||keyboardEditing)){setSliderDown(false);keyboardEditing=false;preview(modes.indexOf(confirmed));e->accept();return;}
    if(!known||!isEnabled()){e->ignore();return;}int index=value();
    switch(e->key()){case Qt::Key_Left:case Qt::Key_Down:index--;break;case Qt::Key_Right:case Qt::Key_Up:index++;break;case Qt::Key_Home:index=0;break;case Qt::Key_End:index=modes.size()-1;break;default:QAbstractSlider::keyPressEvent(e);return;}
    keyboardEditing=true;preview(qBound(0,index,int(modes.size())-1));e->accept();
}
void ModeSlider::keyReleaseEvent(QKeyEvent *e){if(keyboardEditing&&!e->isAutoRepeat()){keyboardEditing=false;commit();e->accept();}else QAbstractSlider::keyReleaseEvent(e);}
void ModeSlider::showEvent(QShowEvent *e){QAbstractSlider::showEvent(e);window()->installEventFilter(this);position=stopPosition(value());ultraAmount=mode()==4?1:0;warmthAmount=warmthFor(mode());updateAnimation();}
void ModeSlider::hideEvent(QHideEvent *e){particles.stop();slide.stop();ultra.stop();warmth.stop();QAbstractSlider::hideEvent(e);}
void ModeSlider::changeEvent(QEvent *e){QAbstractSlider::changeEvent(e);if(e->type()==QEvent::EnabledChange)updateAnimation();}
bool ModeSlider::eventFilter(QObject *object,QEvent *e){if(object==window()&&(e->type()==QEvent::WindowStateChange||e->type()==QEvent::Show))QTimer::singleShot(0,this,[this]{updateAnimation();});return QAbstractSlider::eventFilter(object,e);}
void ModeSlider::updateAnimation(){bool active=known&&mode()==4&&isVisible()&&!window()->isMinimized()&&(isEnabled()||pending)&&Ui::motionEnabled();if(active&&!particles.isActive())particles.start();else if(!active)particles.stop();}
void ModeSlider::paintEvent(QPaintEvent*){
    QPainter p(this);p.setRenderHint(QPainter::Antialiasing);bool muted=!isEnabled()&&!pending;auto palette=paletteAt(warmthAmount);
    if(muted)palette={QColor("#b7c0ca"),QColor("#a9b3c0"),QColor("#929eac")};
    auto accent=palette.text;
    QFont title=font();title.setPixelSize(16);title.setWeight(QFont::DemiBold);p.setFont(title);
    const auto currentTitle=known?titleFor(mode()):QString("不可用");
    if(ultraAmount>0){QColor glow=mix(Qt::white,palette.end,.12);glow.setAlphaF(0.65*ultraAmount);p.setPen(Qt::NoPen);p.setBrush(glow);p.drawRoundedRect(QRectF(6,0,p.fontMetrics().horizontalAdvance(currentTitle)+12,28),7,7);}
    p.setPen(accent);p.drawText(QRectF(12,0,90,28),Qt::AlignLeft|Qt::AlignVCenter,currentTitle);
    QFont sub=font();sub.setPixelSize(11);p.setFont(sub);p.setPen(QColor("#85858d"));
    const auto caption=pending?QString("正在应用…"):isSliderDown()||keyboardEditing?QString("松开以应用"):known?captionFor(mode()):QString("未读取到性能模式");
    p.drawText(QRectF(104,2,width()-116,24),Qt::AlignRight|Qt::AlignVCenter,p.fontMetrics().elidedText(caption,Qt::ElideRight,width()-116));
    auto t=track();p.setPen(QPen(mix(QColor("#dedee2"),palette.end,.12),0.8));p.setBrush(mix(QColor("#e9e9ec"),palette.end,.06));p.drawRoundedRect(t,14,14);
    qreal center=t.left()+16+qBound(0.,position,1.)*(t.width()-32);
    QPainterPath clip;clip.addRoundedRect(t,14,14);p.save();p.setClipPath(clip);QLinearGradient fill(t.topLeft(),QPointF(qMax(t.left()+1,center),t.top()));fill.setColorAt(0,palette.start);fill.setColorAt(1,palette.end);p.fillRect(QRectF(t.left(),t.top(),known?center-t.left():0,t.height()),fill);
    if(known&&ultraAmount>0.01){qreal time=clock.elapsed()/1000.;p.setPen(Qt::NoPen);for(int i=0;i<25;++i){qreal seed=std::fmod(i*0.6180339887,1.),x=std::fmod(seed+time*(.025+(i%5)*.005),1.),y=t.center().y()+std::sin(i*2.37+time*(.7+(i%3)*.2))*9;QPointF pos(t.left()+x*t.width(),y);qreal alpha=(.23+.43*(.5+.5*std::sin(i+time*1.2)))*ultraAmount;QColor color=mix(QColor("#fff9dc"),QColor("#ffd27a"),qreal(i%3)/2.);color.setAlphaF(alpha);p.setBrush(color);qreal radius=i%6==0?1.7:.8;p.drawEllipse(pos,radius,radius);}}
    p.restore();p.setPen(Qt::NoPen);for(int i=0;i<modes.size();++i){qreal x=t.left()+16+stopPosition(i)*(t.width()-32);p.setBrush(x<center?QColor(255,255,255,105):QColor("#b3b3b8"));p.drawEllipse(QPointF(x,t.center().y()),2.25,2.25);}
    p.setBrush(QColor(0,0,0,12));p.drawEllipse(QPointF(center,t.center().y()+1),18,18);p.setBrush(Qt::white);p.setPen(QPen(mix(Qt::white,palette.end,.55),1.3));p.drawEllipse(QPointF(center,t.center().y()),16,16);
    for(int i=0;i<modes.size();++i){const bool selected=known&&i==value();sub.setWeight(selected?QFont::DemiBold:QFont::Normal);p.setFont(sub);p.setPen(selected?accent:QColor("#85858d"));qreal x=t.left()+16+stopPosition(i)*(t.width()-32);p.drawText(QRectF(x-28,75,56,18),Qt::AlignCenter,titleFor(modes[i]));}
    if(hasFocus()){p.setBrush(Qt::NoBrush);p.setPen(QPen(mix(Qt::white,palette.end,.55),1));p.drawRoundedRect(t.adjusted(-4,-7,4,7),20,20);}
}
