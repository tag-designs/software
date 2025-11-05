#ifndef MAGPLOT_H
#define MAGPLOT_H

#include <QFrame>
#include <QPaintEvent>
#include <QVector3D>
#include <QList>
#include <QQuaternion>

class magPlot : public QFrame
{
    Q_OBJECT
public:
    explicit magPlot(QFrame *parent = nullptr);
    void setField(float f);
    void setZoom(float z);
    void setFocusQuaternion(QQuaternion);
    void setPoints(QList<QVector3D>);
    void addPoint(QVector3D);

signals:

protected:

    void paintEvent(QPaintEvent *event) override;

    // override mouse events

    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
        // magnetic field
    float field = 60.0;
        // zoom factor set by wheel
    float zoom = 1.0;

    // radius of center circle
    const float centerRadius = 2.0;

        // calibration data
    QList<QVector3D> points;
        // all items rotated by focusQ
    QQuaternion focusQ = QQuaternion(1.0,0.0,0.0,0.0);
        // rotation without rotationQ [ focusQ = rotationQ*savedQ ]
    QQuaternion savedQ = QQuaternion(1.0,0.0,0.0,0.0);
        // external rotation through mouse events
    QQuaternion rotationQ = QQuaternion(1.0,0.0,0.0,0.0);
        // state for mouse tracking
    QQuaternion old_rotationQ;
    bool m_isDragging = false;
    QPointF m_lastPos;

    // drawing helpers

    void drawAxis(QPainter *e, QVector3D pt, QColor color, QString& text);

    // if resize is true, points with -z are resized
    void drawPoint(QPainter *p, QVector3D pt, QColor color, float size);

};

#endif // MAGPLOT_H
