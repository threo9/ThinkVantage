#pragma once
#include <QtWidgets>
#include <QJsonObject>
#include <optional>

// Visual stop indices are independent from the non-contiguous firmware values.
class ModeSlider : public QAbstractSlider {
    Q_OBJECT
public:
    explicit ModeSlider(QWidget *parent=nullptr);
    void setState(const QJsonObject &feature,bool onAc,std::optional<int> pending={});
    int mode() const{return modes.value(value(),-1);}
    int confirmedMode() const{return confirmed;}
    int indexForMode(int mode) const{return modes.indexOf(mode);}
    bool particleAnimationActive() const{return particles.isActive();}
    quint64 transitionCount() const{return transitions;}
    QSize sizeHint()const override{return {350,100};}
    QSize minimumSizeHint()const override{return {220,100};}
signals:
    void modeRequested(int value);
protected:
    void paintEvent(QPaintEvent*)override;
    void mousePressEvent(QMouseEvent*)override;
    void mouseMoveEvent(QMouseEvent*)override;
    void mouseReleaseEvent(QMouseEvent*)override;
    void keyPressEvent(QKeyEvent*)override;
    void keyReleaseEvent(QKeyEvent*)override;
    void wheelEvent(QWheelEvent *event)override{event->ignore();}
    void showEvent(QShowEvent*)override;
    void hideEvent(QHideEvent*)override;
    void changeEvent(QEvent*)override;
    bool eventFilter(QObject*,QEvent*)override;
private:
    QList<int> modes;
    QVariantAnimation slide,ultra,warmth;
    QTimer particles;
    QElapsedTimer clock;
    qreal position=0,ultraAmount=0,warmthAmount=0;
    int confirmed=-1;
    bool onAc=true,pending=false,known=false,keyboardEditing=false;
    quint64 transitions=0;
    QRectF track()const;
    qreal stopPosition(int index)const;
    int indexAt(qreal x)const;
    void preview(int index,bool animate=true);
    void commit();
    void updateAnimation();
};
