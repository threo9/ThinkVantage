#pragma once
#include <QtWidgets>
namespace Ui {bool motionEnabled();}

class SmoothSlider : public QSlider {
    qreal visual=0;
    bool initialized=false;
    QVariantAnimation animation;
public:
    explicit SmoothSlider(Qt::Orientation orientation):QSlider(orientation){
        animation.setDuration(190);animation.setEasingCurve(QEasingCurve::OutCubic);
        connect(&animation,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){visual=v.toReal();update();});
        connect(this,&QSlider::valueChanged,this,[this](int n){animation.stop();visual=n;initialized=true;});
    }
    void syncValue(int target){
        if(value()==target&&initialized)return;
        QSlider::setValue(target);animation.stop();
        if(initialized&&isVisible()&&!isSliderDown()&&Ui::motionEnabled()){animation.setStartValue(visual);animation.setEndValue(target);animation.start();}
        else {visual=target;update();}initialized=true;
    }
protected:
    void paintEvent(QPaintEvent*)override{QStylePainter p(this);QStyleOptionSlider option;initStyleOption(&option);option.subControls=QStyle::SC_SliderGroove|QStyle::SC_SliderHandle;if(tickPosition()!=NoTicks)option.subControls|=QStyle::SC_SliderTickmarks;if(isSliderDown())option.activeSubControls=QStyle::SC_SliderHandle;option.sliderPosition=qRound(visual);p.drawComplexControl(QStyle::CC_Slider,option);}
    void mousePressEvent(QMouseEvent *event)override{animation.stop();visual=value();QSlider::mousePressEvent(event);}
    void hideEvent(QHideEvent *event)override{animation.stop();visual=value();QSlider::hideEvent(event);}
};

class SmoothProgress : public QProgressBar {
    QVariantAnimation animation;
    int target=-1;
public:
    SmoothProgress(){animation.setDuration(240);animation.setEasingCurve(QEasingCurve::OutCubic);connect(&animation,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){QProgressBar::setValue(qRound(v.toReal()));});}
    void syncValue(int n){if(target==n)return;target=n;animation.stop();if(value()>=0&&isVisible()&&Ui::motionEnabled()){animation.setStartValue(value());animation.setEndValue(n);animation.start();}else setValue(n);}
};

class ChoiceCombo : public QComboBox {
public:
    void paintEvent(QPaintEvent *event)override{
        QComboBox::paintEvent(event);QPainter p(this);p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(isEnabled()?QColor("#73858e"):QColor("#b6c1c7"),1.3,Qt::SolidLine,Qt::RoundCap,Qt::RoundJoin));
        const qreal x=width()-17,y=height()/2.;p.drawLine(QPointF(x-3,y-1),QPointF(x,y+2));p.drawLine(QPointF(x,y+2),QPointF(x+3,y-1));
    }
};

class Toggle : public QCheckBox {
    qreal position=0;
    QVariantAnimation animation;
public:
    Toggle(){setFixedSize(44,30);setCursor(Qt::PointingHandCursor);animation.setDuration(145);animation.setEasingCurve(QEasingCurve::OutCubic);connect(&animation,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){position=v.toReal();update();});connect(this,&QCheckBox::toggled,this,[this](bool checked){animation.stop();if(isVisible()&&Ui::motionEnabled()){animation.setStartValue(position);animation.setEndValue(checked?1.:0.);animation.start();}else {position=checked?1:0;update();}});}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);p.setPen(Qt::NoPen);
        QColor off("#a4aeb8"),on("#278773");QColor color=QColor::fromRgbF(off.redF()+(on.redF()-off.redF())*position,off.greenF()+(on.greenF()-off.greenF())*position,off.blueF()+(on.blueF()-off.blueF())*position);
        p.setBrush(!isEnabled()?QColor("#dce1e6"):color);
        p.drawRoundedRect(QRectF(1,4,42,24),12,12);p.setBrush(Qt::white);p.drawEllipse(QRectF(5+18*position,8,16,16));
        if(hasFocus()){p.setBrush(Qt::NoBrush);p.setPen(QPen(QColor("#278773"),1,Qt::DashLine));p.drawRoundedRect(QRectF(0,1,43,28),5,5);}
    }
    bool hitButton(const QPoint &pos)const override{return rect().contains(pos);}
    void hideEvent(QHideEvent *event)override{animation.stop();position=isChecked()?1:0;QCheckBox::hideEvent(event);}
};

class BatteryChart : public QWidget {
    QVariantAnimation animation;
    double shown=-1;
public:
    double current=-1,full=-1,design=-1;
    BatteryChart(){setMinimumHeight(45);animation.setDuration(240);animation.setEasingCurve(QEasingCurve::OutCubic);connect(&animation,&QVariantAnimation::valueChanged,this,[this](const QVariant &v){shown=v.toDouble();update(QRect(0,0,width(),15));});}
    void setCapacities(double c,double f,double d){if(current==c&&full==f&&design==d)return;animation.stop();current=c;full=f;design=d;if(shown>=0&&c>=0&&isVisible()&&Ui::motionEnabled()){animation.setStartValue(shown);animation.setEndValue(c);animation.start();}else shown=c;update();}
    void paintEvent(QPaintEvent*)override{
        QPainter p(this);p.setRenderHint(QPainter::Antialiasing);QRectF bar(0,4,width(),8);
        p.setPen(Qt::NoPen);p.setBrush(QColor("#d6e5df"));p.drawRoundedRect(bar,4,4);
        auto draw=[&](double n,const QColor &color){if(design>0&&n>=0){p.setBrush(color);auto rect=bar;rect.setWidth(bar.width()*qBound(0.,n/design,1.));p.drawRoundedRect(rect,4,4);}};
        draw(full,QColor("#96c7b8"));draw(shown,QColor("#278773"));
        auto value=[](double n){return n>=0?QString::number(n,'f',1)+" Wh":QString("—");};
        p.setPen(QColor("#647a74"));p.drawText(QRectF(0,20,width(),22),Qt::AlignLeft,"当前 "+value(current)+"    满充 "+value(full)+"    设计 "+value(design));
    }
};
