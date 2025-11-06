/*
 *  Qt quaternions are in ENU order
 *  Sensors are in NED order.  To convert from ENU to NED
 *     Qenu(w,x,y,z) -> Qned(w,y,x,-z)
 *
 *
 */


#include "magplot.h"
#include <QPainter>
#include <QPen>
#include <QtMinMax>

/*
static void ENU_NED(QQuaternion &qt)
{
    //qt = QQuaternion(qt.scalar(),qt.y(),qt.x(),-qt.z());
}
*/

magPlot::magPlot(QWidget *parent) : QWidget{parent}
{
    setAutoFillBackground(true);
    reset();
}

void magPlot::reset(){
    points.empty();
    field = 60.0;   // default field
    zoom = 0.8;
    focusQ = QQuaternion(1.0,0.0,0.0,0.0);
    savedQ = QQuaternion(1.0,0.0,0.0,0.0);
    rotationQ = QQuaternion(1.0,0.0,0.0,0.0);
};

// change magnetic field
void magPlot::setField(float f)
{
    field = f;
    update();
}

// set rotation to focus
void magPlot::setFocusQuaternion(QQuaternion fq)
{
    // save external rotation

    rotationQ = fq;

    // update composite rotation

    focusQ = rotationQ*savedQ;

    // redraw

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

    // camera vector along z axis

    QVector3D camera = QVector3D(0,0,-1);

    // compute rotation quaternion

    savedQ = QQuaternion::rotationTo(p,camera);

    // update total rotation

    focusQ = rotationQ*savedQ;

    // redraw

    update();
}

void magPlot::paintEvent(QPaintEvent *event)
{


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

    // Apply rotation to saved points

    QList<QVector3D> rotated_points;
    for (QVector3D point : points){
        QVector3D pt = focusQ.rotatedVector(point);
        pt.setY(-pt.y());
        rotated_points.append(pt);
    }

    // Draw background points [ z >= 0]

    painter.save();

    // highlight last point in list

    if (!rotated_points.isEmpty() && rotated_points.last().z() >= 0) // display location of last point added
        drawPoint(&painter,rotated_points.last(),Qt::yellow,highlightSize);

    // draw points

    for (QVector3D point : rotated_points){
        if (point.z() >= 0) {
            drawPoint(&painter, point, Qt::darkMagenta, dataPointSize);
        }
    }
    painter.restore();

    // Compute Axes

    QVector3D x1(1.0,0.0,0.0);
    QVector3D y1(0.0,1.0,0.0);
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
    painter.drawEllipse(center,centerRadius,centerRadius);
    painter.restore();

    // draw field circle -- Use gradient fill based on very light gray

    painter.save();
    painter.setPen(QPen(Qt::gray, 0.1));

    // gradient

    radGrad.setCenterRadius(field);
    radGrad.setColorAt(1.0,QColor(240,240,240,60));
    radGrad.setColorAt(0.0,QColor(240,240,240,180));
    painter.setBrush(QBrush(radGrad));

    // boundary ellipse

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

    // highlight last point

    if (!rotated_points.isEmpty() && rotated_points.last().z() < 0) // display location of last point added
        drawPoint(&painter,rotated_points.last(),Qt::yellow,highlightSize);

    // draw points
    for (QVector3D point :  rotated_points){
        if (point.z() < 0) {
            drawPoint(&painter,point, Qt::darkMagenta, dataPointSize);
        }
    }
    painter.restore();
    QWidget::paintEvent(event);  // call parent
}

   // draw a point with given color and size -- size
   // decreases with distance

void magPlot::drawPoint(QPainter *p, QVector3D pt, QColor color, float size)
{
    p->setPen(Qt::NoPen);

    if (pt.length() == 0.0)
        return;

    float size_scale = (4.0 - pt.z()/pt.length())/4.0;
    size = size * size_scale;//size - pt.z()/(pt.length()*size*4.0);
    p->setBrush(QBrush(color,Qt::SolidPattern));
    p->drawEllipse(QPointF(pt.x(),-pt.y()),size/2,size/2);
}

  // draw a single axis with given color and end point (pt)

void magPlot::drawAxis(QPainter *p, QVector3D pt, QColor color, QString &text){
    QPointF pt2(pt.x(),pt.y());
    pt.setY(-pt.y());
    p->save();

    // draw axis

    QLineF line = QLineF(pt2*field, pt2*centerRadius);
    drawPoint(p,pt*field,color,axisPointSize);
    p->setPen(QPen(color, 0.1));
    p->drawLine(line);

    // label
    /*
    QFont font = p->font();
    font.setPixelSize(axisFontPixelSize);
    p->setFont(font);
    p->translate(pt2*field*1.05);
    QFontMetrics fm(p->font());
    QRect textRect = fm.boundingRect(text);
    p->translate(textRect.center());
    p->drawText(0.0,0.0,text);
    */

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
    QWidget::wheelEvent(event);
}

void magPlot::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        m_lastPos = event->position(); // Store the initial position
        m_isDragging = true; // Set a flag to indicate dragging
        old_rotationQ = rotationQ;
    }
    QWidget::mousePressEvent(event);
}

void magPlot::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isDragging && (event->buttons() & Qt::RightButton)) {
        QPointF delta = event->position() - m_lastPos;
        float roll_change = qBound(-90.0,90.0*delta.x()/width(),90.0);
        float pitch_change = qBound(-90.0,-90.0*delta.y()/height(),90.0);
        QQuaternion tmpQ(QQuaternion::fromEulerAngles(-pitch_change, -roll_change,0.0));
        //ENU_NED(tmpQ);
        //tmpQ = QQuaternion(tmpQ.scalar(), tmpQ.y(), tmpQ.x(), -tmpQ.z());
        setFocusQuaternion(tmpQ*old_rotationQ);
    }
    QWidget::mouseMoveEvent(event);
}


void magPlot::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::RightButton) {
        m_isDragging = false; // Reset the dragging flag
    }
    QWidget::mouseReleaseEvent(event); // Call base class implementation
}

