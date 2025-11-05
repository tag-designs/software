
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

    // change text size

    QFont font = painter.font();
    font.setPixelSize(6);
    painter.setFont(font);

    // fill background

    painter.fillRect(rect(), Qt::white);

    // translate zero point to center

    painter.translate(width()/2.0,height()/2.0);

    // set initial scaling

    float scale = zoom*height()/(field*2.5);
    painter.scale(scale,scale);

    // Compute points

    QList<QVector3D> translated_points;
    QList<QVector3D>:: iterator i;
    for (i = points.begin(); i != points.end(); ++i){
        QVector3D pt = focusQ.rotatedVector(*i*field);
        // x up, y right, z down for NED system
        float x = pt.x();
        pt.setX(pt.y());
        pt.setY(-x);
        pt.setZ(-pt.z());
        translated_points.append(pt);
    }

    // Draw background points

    painter.save();
    for (i = translated_points.begin(); i != translated_points.end(); ++i){
        if ((*i).z() >= 0) {
            drawPoint(&painter, *i, Qt::darkMagenta, 1.0);
        }
    }
    painter.restore();

    // Compute Axes

    QVector3D x1(0.0,1.0,0.0);
    QVector3D y1(1.0,0.0,0.0);
    QVector3D z1(0.0,0.0,1.0);

    x1 = focusQ.rotatedVector(x1);
    y1 = focusQ.rotatedVector(y1);
    z1 = focusQ.rotatedVector(z1);

    QString xlabel = QString("x");
    QString ylabel = QString("y");
    QString zlabel = QString("z");

    // Draw Axes in background (positive z)

    if (x1.z() >= 0)
        drawAxis(&painter,x1,Qt::red,xlabel);
    if (y1.z() >= 0)
        drawAxis(&painter,y1,Qt::green,ylabel);
    if (z1.z() >= 0)
        drawAxis(&painter,z1,Qt::blue,zlabel);

    // draw sphere center
    QPointF center(0.0,0.0);
    painter.save();

    painter.setPen(Qt::NoPen);
    QRadialGradient radGrad(QPointF(0.0,0.0),1.5);
    radGrad.setColorAt(1.0,Qt::darkCyan);
    radGrad.setColorAt(0.0,Qt::cyan);
    painter.setBrush(QBrush(radGrad));
    painter.drawEllipse(center,2.0,2.0);

    painter.restore();


    // draw circle -- Use gradient fill based on very light gray

    painter.save();
    painter.setPen(QPen(Qt::gray, 0.1));

      // gradient

    radGrad.setCenterRadius(field);
    radGrad.setColorAt(1.0,QColor(240,240,240,60));
    radGrad.setColorAt(0.0,QColor(240,240,240,150));
    painter.setBrush(QBrush(radGrad));

    // draw boundary ellipse

    painter.drawEllipse(center,field,field);
    painter.restore();


    // draw the axes in forground -- z axis points down!

    if (x1.z() < 0)
        drawAxis(&painter,x1,Qt::red,xlabel);
    if (y1.z() < 0)
        drawAxis(&painter,y1,Qt::green,ylabel);
    if (z1.z() < 0)
        drawAxis(&painter,z1,Qt::blue,zlabel);

    // Draw foreground points (negative z)

    painter.save();
    for (i = translated_points.begin(); i != translated_points.end(); ++i){
        if ((*i).z() < 0) {
            drawPoint(&painter, *i, Qt::darkMagenta, 1.0);
        }
    }
    painter.restore();
}

   // draw a point with given color and size.

void magPlot::drawPoint(QPainter *p, QVector3D pt, QColor color, float size)
{
    p->setPen(Qt::NoPen);
    // scale based upon z distance
    size = size - pt.z()/(pt.length()*size*4.0);
    p->setBrush(QBrush(color,Qt::SolidPattern));
    p->drawEllipse(QPointF(pt.x(),-pt.y()),size/2,size/2);
}

  // draw a single axis with given color and end point (pt)

void magPlot::drawAxis(QPainter *p, QVector3D pt, QColor color, QString &text){
    QPointF pt2(pt.x(),-pt.y());
    p->save();
    QLineF line = QLineF(pt2*field, pt2*centerRadius);
    drawPoint(p,pt*field,color,1.5);
    p->setPen(QPen(color, 0.1));
    if (pt.z()<0.0)
        color.setAlphaF(0.2);
    p->drawLine(line); // QPointF(pt.x(),pt.y()),QPointF(0.0,0.0));
    //pt = pt*field*1.05;
    p->translate(pt2*field*1.05);
    //p->rotate(line.angle());
    QFontMetrics fm(p->font());
    QRect textRect = fm.boundingRect(text);
    p->translate(textRect.center());
    p->drawText(0.0,0.0,text);
    p->restore();

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
        float pitch_change = qBound(-90.0,90.0*delta.x()/width(),90.0);
        float yaw_change = qBound(-90.0,-90.0*delta.y()/height(),90.0);
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
