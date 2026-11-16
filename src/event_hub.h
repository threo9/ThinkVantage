#pragma once
#include <QObject>
#include <QAbstractNativeEventFilter>
#include <memory>

// Observes Windows/OEM state only. All writes still pass through the worker.
class EventHub final : public QObject, public QAbstractNativeEventFilter {
    Q_OBJECT
public:
    explicit EventHub(quintptr window, QObject *parent=nullptr);
    ~EventHub() override;
    bool nativeEventFilter(const QByteArray &eventType, void *message, qintptr *result) override;
signals:
    void invalidated(const QString &reason);
private slots:
    void audioChanged(bool topology);
private:
    bool closing=false;
    struct Impl;
    std::unique_ptr<Impl> impl;
};
