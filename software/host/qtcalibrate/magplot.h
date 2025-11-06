#ifndef MAGPLOT_H
#define MAGPLOT_H

#include <QFrame>
#include <QPaintEvent>
#include <QVector3D>
#include <QList>
#include <QQuaternion>

class magPlot : public QWidget
{
    Q_OBJECT
public:
    explicit magPlot(QWidget *parent = nullptr);
    void setField(float f);
    void setZoom(float z);
    void setFocusQuaternion(QQuaternion);
    void setPoints(QList<QVector3D>);
    void addPoint(QVector3D);
    void reset();

signals:

protected:

    void paintEvent(QPaintEvent *event) override;

    // override mouse events

    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:

    const float centerRadius = 2.0;
    const float dataPointSize = 0.5;
    const float highlightSize = 2.0;
    const float axisPointSize = 1.0;
    const float axisFontPixelSize = 4.0;

    float field; // magnetic field
    float zoom ; // zoom factor set by wheel


    QList<QVector3D> points; // data to plot
    QQuaternion focusQ;      // rotation to focal point
    QQuaternion savedQ;      // rotation of latest data
    QQuaternion rotationQ;   // added rotation through mouse or setFocusQuaternion

    // temporary state for mouse tracking

    QQuaternion old_rotationQ;
    bool m_isDragging = false;
    QPointF m_lastPos;


    // drawing helpers

    void drawAxis(QPainter *e, QVector3D pt, QColor color, QString& text);

    // if resize is true, points with -z are resized
    void drawPoint(QPainter *p, QVector3D pt, QColor color, float size);

};

#endif // MAGPLOT_H
