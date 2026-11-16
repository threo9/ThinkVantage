#pragma once
#include <QtWidgets>
#include <QJsonObject>
class Window;

class QuickPanel : public QFrame {
    Q_OBJECT
public:
    explicit QuickPanel();
    void updateState(const QJsonObject &state);
    void setBusy(bool value);
    void showAt(const QRect &anchor);
    void feedback(bool ok,const QString &message);
    int controlCount() const{return controls.size();}
    bool matches(const QJsonObject &state) const;
    bool idleControlsEnabled(const QJsonObject &state)const;
protected:
    void paintEvent(QPaintEvent *event)override;
signals:
    void settingRequested(const QString &id,int value);
    void pageRequested(int index);
    void refreshRequested();
private:
    QLabel *battery,*supply,*detail,*message;
    QProgressBar *batteryBar;
    QMap<QString,QWidget*> controls;
    QMap<QString,QLabel*> values;
    QMap<QString,QTimer*> debounce;
    QJsonObject snapshot;
    bool updating=false,busy=false;
    qint64 feedbackUntil=0;
    void enableControls();
};

class TrayController : public QObject {
    Q_OBJECT
public:
    explicit TrayController(Window *window,bool enabled=true);
    ~TrayController();
    bool available() const{return active;}
    void showPanel();
    QJsonObject verify(const QString &capturePath);
    static QIcon appIcon();
private:
    Window *window;
    QuickPanel *panel;
    QSystemTrayIcon *icon;
    QMenu *menu;
    bool active=false,notified=false;
    void updateTooltip();
};
