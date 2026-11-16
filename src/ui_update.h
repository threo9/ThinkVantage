#pragma once
#include <QtWidgets>
#include <QJsonArray>
#include <QJsonObject>
namespace Ui {
bool motionEnabled();
void text(QLabel *label,const QString &value);
void visible(QWidget *widget,bool value);
void enabled(QWidget *widget,bool value);
void checked(QAbstractButton *widget,bool value);
void combo(QComboBox *widget,const QJsonObject &state,int value);
bool editing(QWidget *widget);
void slider(QSlider *widget,const QJsonObject &state,int value);
void progress(QProgressBar *widget,int value);
bool equalData(QJsonObject a,QJsonObject b);
}
