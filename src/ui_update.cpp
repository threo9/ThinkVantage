#include "ui_update.h"
#include "window_widgets.h"
#include <windows.h>

namespace Ui {
bool motionEnabled(){static bool enabled=[](){BOOL value=TRUE;return !SystemParametersInfoW(SPI_GETCLIENTAREAANIMATION,0,&value,0)||value!=FALSE;}();return enabled;}
void text(QLabel *widget,const QString &value){if(widget->text()!=value)widget->setText(value);}
void visible(QWidget *widget,bool value){if(widget->isHidden()==value)widget->setVisible(value);}
void enabled(QWidget *widget,bool value){if(widget->testAttribute(Qt::WA_ForceDisabled)==value)widget->setEnabled(value);}
void checked(QAbstractButton *widget,bool value){if(widget->isChecked()!=value)widget->setChecked(value);}
void combo(QComboBox *widget,const QJsonObject &state,int value){
    auto options=state["options"].toArray();bool available=state["available"].toBool();
    if(widget->view()->isVisible())return; // Keep the user's open menu and highlight intact.
    bool changed=widget->count()!=(available?options.size():1);
    if(!changed&&available)for(int i=0;i<options.size();++i){auto o=options[i].toObject();if(widget->itemData(i).toInt()!=o["value"].toInt()||widget->itemText(i)!=o["label"].toString()){changed=true;break;}}
    if(!available&&widget->itemText(0)!="不可用")changed=true;
    QSignalBlocker block(widget);
    if(changed){widget->clear();if(available){for(auto option:options){auto o=option.toObject();widget->addItem(o["label"].toString(),o["value"].toInt());}}else widget->addItem("不可用");}
    int index=available?widget->findData(value):0;if(widget->currentIndex()!=index)widget->setCurrentIndex(index);
}
bool editing(QWidget *widget){if(auto slider=qobject_cast<QAbstractSlider*>(widget))if(slider->isSliderDown())return true;auto timer=widget->property("debounce").value<QObject*>();return timer&&qobject_cast<QTimer*>(timer)&&qobject_cast<QTimer*>(timer)->isActive();}
void slider(QSlider *widget,const QJsonObject &state,int value){
    if(editing(widget))return;QSignalBlocker block(widget);int low=state["min"].toInt(0),high=state["max"].toInt(100),step=state["step"].toInt(1);
    if(widget->minimum()!=low||widget->maximum()!=high)widget->setRange(low,high);if(widget->singleStep()!=step)widget->setSingleStep(step);
    if(auto smooth=dynamic_cast<SmoothSlider*>(widget))smooth->syncValue(value);else if(widget->value()!=value)widget->setValue(value);
}
void progress(QProgressBar *widget,int value){if(auto smooth=dynamic_cast<SmoothProgress*>(widget))smooth->syncValue(value);else if(widget->value()!=value)widget->setValue(value);}
bool equalData(QJsonObject a,QJsonObject b){a.remove("timestamp");b.remove("timestamp");a.remove("_pending");b.remove("_pending");return a==b;}
}
