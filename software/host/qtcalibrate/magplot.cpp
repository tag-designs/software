
#include "magplot.h"
#include <QPainter>
#include <QPen>
#include <QtMinMax>

magPlot::magPlot(QFrame *parent) : QFrame{parent}
{
    setAutoFillBackground(true);
}

// change magnetic field
void magPlot::setField(float f)
{
    field = f;
    update();
}

// set rotation to focus
void magPlot::setFocusQuaternion(QQuaternion fq)
{
    rotationQ = fq;
    focusQ = savedQ*rotationQ;
    update();
}

void magPlot::setPoints(QList<QVector3D> pts)
{
    points = pts;
    update();
}

void magPlot::addPoint(QVector3D p)
{
    points.append(p);
    QVector3D v = p;
    v.normalize();

    // camera vector

    QVector3D camera = QVector3D(0,0,1);  // camera along z axis

    // compute rotation quaternion

    savedQ = QQuaternion::rotationTo(v,camera);
    focusQ = savedQ*rotationQ;

    update();
}

void magPlot::paintEvent(QPaintEvent *event)
{
    QFrame::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform,true);

    // fill background

    painter.fillRect(rect(), Qt::white);

    // translate zero point to center
    painter.translate(width()/2.0,height()/2.0);

    // set initial scaling

    float scale = zoom*height()/(field*2.5);
    painter.scale(scale,scale);

    // draw circle

    painter.setPen(QPen(Qt::gray, 0.1));
    QPointF center(0.0,0.0);
    painter.drawEllipse(center,field,field);

    // draw the axes

    drawAxes(&painter);

    // draw data

    QList<QVector3D>:: iterator i;
    for (i = points.begin(); i != points.end(); ++i){
        drawPoint(&painter, focusQ.rotatedVector(*i*field), Qt::magenta, 1.0);
    }
}

   // draw a point with given color and size.  If resize, rescale for points with z < 0

void magPlot::drawPoint(QPainter *p, QVector3D pt, QColor color, float size, bool resize)
{
    p->setPen(Qt::NoPen);
    if (pt.z()<0.0) {
        color.setAlphaF(0.2);    // translucent for negative z
        if (resize)
            size = size*0.7;     // smaller for negative z
    }
    p->setBrush(QBrush(color,Qt::SolidPattern));
    p->drawEllipse(QPointF(pt.x(),pt.y()),size/2,size/2);

}

  // draw a single axis with given color and end point (pt)

void magPlot::drawAxis(QPainter *p, QVector3D pt, QColor color){
    drawPoint(p,pt,color,1.5,false);
    p->setPen(QPen(color, 0.1));
    if (pt.z()<0.0)
        color.setAlphaF(0.2);
    p->drawLine(QPointF(pt.x(),pt.y()),QPointF(0.0,0.0));

}

   // draw axes rotated by focusQ
   // following QML Axis helper:The X axis is red, the Y axis is green, and the Z axis is blue.

void magPlot::drawAxes(QPainter *P)
{
    P->save();

    QVector3D x1(field,0.0,0.0);
    QVector3D y1(0.0,field,0.0);
    QVector3D z1(0.0,0.0,field);

    x1 = focusQ.rotatedVector(x1);
    y1 = focusQ.rotatedVector(y1);
    z1 = focusQ.rotatedVector(z1);

    drawAxis(P,x1,Qt::red);
    drawAxis(P,y1,Qt::green);
    drawAxis(P,z1,Qt::blue);

    P->restore();
}

// Mouse controls

void magPlot::wheelEvent(QWheelEvent *event)
{
    // Get the vertical scroll amount (positive for scrolling up, negative for down)
    int delta = event->angleDelta().y();
    if (delta > 0.0){
        zoom = zoom*1.15;
    }
    if (delta < 0.0){
        zoom = zoom/1.15;
    }
    update();

}

void magPlot::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        m_lastPos = event->position(); // Store the initial position
        m_isDragging = true; // Set a flag to indicate dragging
        old_rotationQ = rotationQ;
    }
    QFrame::mousePressEvent(event);
}

void magPlot::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::RightButton)) {
        QPointF delta = event->position() - m_lastPos;
        float yaw_change = qBound(-90.0,90.0*delta.x()/width(),90.0);
        float pitch_change = qBound(-90.0,-90.0*delta.y()/height(),90.0);
        setFocusQuaternion(QQuaternion::fromEulerAngles(pitch_change,yaw_change,0.0)*old_rotationQ);
    }
    QFrame::mouseMoveEvent(event);
}


void magPlot::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        m_isDragging = false; // Reset the dragging flag
    }
    QFrame::mouseReleaseEvent(event); // Call base class implementation
}
