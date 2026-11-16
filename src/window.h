#pragma once
#include <QMainWindow>
#include <QJsonObject>
#include <QMap>
#include <functional>
class QStackedWidget;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QCloseEvent;
class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(const QString &capturePath={},bool smoke=false);
    QJsonObject currentState() const{return snapshot;}
    QJsonObject displayedState() const{return presentationState();}
    bool isApplying() const{return !applyingId.isEmpty();}
    bool isBusy() const{return process||refreshPending||!pendingSettings.isEmpty();}
    void setTrayActive(bool active){trayActive=active;}
    void showPage(int index=0);
    void refreshState(){reload();}
    void applySetting(const QString &id,int value){setValue(id,value);}
    void requestQuit();
signals:
    void snapshotChanged(const QJsonObject &state);
    void busyChanged(bool busy);
    void operationFinished(bool ok,const QString &message);
    void hiddenToTray();
protected:
    void closeEvent(QCloseEvent *event)override;
    bool eventFilter(QObject *object,QEvent *event)override;
private:
    QStackedWidget *pages;
    QLabel *device,*status,*batterySummary,*batteryDetail;
    QLabel *temperature,*chargeThreshold,*ocDetails;
    QWidget *batteryChart,*batteryExtra;
    QProgressBar *temperatureBar;
    QMap<QString,QLabel*> batteryRows;
    QPushButton *refresh,*admin;
    QPlainTextEdit *diagnostics;
    QMap<QString,QWidget*> controls;
    QMap<QString,QLabel*> hints;
    QMap<QString,QWidget*> unitControls;
    QLabel *presenceInfo;
    QJsonObject snapshot;
    QProcess *process=nullptr;
    QString capturePath;
    bool loading=false,smoke=false;
    bool trayActive=false,refreshPending=false,quitPending=false;
    bool lastWriteBusy=false,diagnosticsDirty=true;
    QString applyingId;
    QMap<QString,int> pendingSettings;
    QStringList pendingOrder;
    qint64 statusUntil=0;
    QJsonObject presentationState() const;
    void drainPending();
    void updateDiagnostics();
    QWidget *page(const QString &title,const QString &description);
    QWidget *section(QWidget *page,const QString &title,const QString &description={});
    void row(QWidget *page,const QString &id,const QString &title,const QString &description,bool slider=false,const QString &kind={});
    QWidget *advanced(QWidget *page,const QString &title);
    void linkRow(QWidget *page,const QString &title,const QString &description,const QString &uri);
    void reload();
    void updateState(const QJsonObject &json);
    void setValue(const QString &id,int value);
    void run(const QStringList &args,std::function<void(QJsonObject)> done);
    void enableControls();
};
