#pragma once
#include <QMainWindow>
#include <QJsonObject>
#include <QMap>
#include <functional>
#include "target_context.h"
class QStackedWidget;
class QLabel;
class QPushButton;
class QPlainTextEdit;
class QProcess;
class QProgressBar;
class QCloseEvent;
class QTimer;
class Window : public QMainWindow {
    Q_OBJECT
public:
    explicit Window(const QString &capturePath={},bool smoke=false);
    QJsonObject currentState() const{return snapshot;}
    QJsonObject displayedState() const{return presentationState();}
    bool isApplying() const{return externalBusy||!applyingId.isEmpty();}
    bool isBusy() const{return externalBusy||process||refreshPending||!pendingSettings.isEmpty();}
    void setAutomationBusy(bool busy){externalBusy=busy;enableControls();}
    void markUnknownOutcome(const QString &error){markStale(error);}
    void setTrayActive(bool active){trayActive=active;}
    void showPage(int index=0);
    void refreshState(){reload();}
    void applySetting(const QString &id,int value){setValue(id,value);}
    void requestQuit();
    std::function<bool()> quitGuard;
    std::function<bool()> handoffPreparation;
    std::function<void()> handoffCancelled;
signals:
    void snapshotChanged(const QJsonObject &state);
    void busyChanged(bool busy);
    void operationFinished(bool ok,const QString &message);
    void hiddenToTray();
    void manualSettingRequested(const QString &id,int value);
    void manualSettingFinished(const QString &id,bool ok);
    void supplyObserved(bool ac);
    void elevationRequested();
protected:
    void closeEvent(QCloseEvent *event)override;
    bool eventFilter(QObject *object,QEvent *event)override;
private:
    QStackedWidget *pages;
    QLabel *device,*status,*batterySummary,*batteryDetail;
    QLabel *temperature,*chargeThreshold,*ocDetails,*gpuOccupancy;
    QWidget *batteryChart,*batteryExtra;
    QProgressBar *temperatureBar;
    QMap<QString,QLabel*> batteryRows;
    QPushButton *refresh,*admin,*gpuRefresh;
    QPlainTextEdit *diagnostics;
    QMap<QString,QWidget*> controls;
    QMap<QString,QLabel*> hints;
    QMap<QString,QWidget*> unitControls;
    QLabel *presenceInfo;
    QJsonObject snapshot;
    QProcess *process=nullptr;
    QTimer *eventRefreshTimer=nullptr;
    QString capturePath;
    bool loading=false,smoke=false;
    bool trayActive=false,refreshPending=false,quitPending=false;
    bool lastWriteBusy=false,diagnosticsDirty=true;
    bool externalBusy=false;
    bool eventRefreshPending=false;
    QString applyingId;
    QMap<QString,int> pendingSettings;
    QMap<QString,QJsonObject> pendingContexts;
    TargetContext::GenerationFence generation;
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
    void invalidateState(const QString &reason);
    void markStale(const QString &error);
};
